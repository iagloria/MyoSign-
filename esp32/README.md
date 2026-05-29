# MyoSign ESP32-S3 嵌入式固件 — 完整开发文档

> **版本**: v1.0  
> **更新时间**: 2026-05-25  
> **目标芯片**: 乐鑫 ESP32-S3 (R8)  
> **开发框架**: ESP-IDF v5.x + NimBLE + FreeRTOS  
> **硬件板**: MyoSign 核心电路板 (嘉立创 EDA 设计)

---

## 目录

1. [系统概述](#1-系统概述)
2. [硬件引脚映射](#2-硬件引脚映射)
3. [软件架构](#3-软件架构)
4. [数据流详解](#4-数据流详解)
5. [串口通信协议](#5-串口通信协议)
6. [文件结构](#6-文件结构)
7. [编译与烧录](#7-编译与烧录)
8. [调试指南](#8-调试指南)
9. [常见问题排查](#9-常见问题排查)

---

## 1. 系统概述

### 1.1 这块 ESP32-S3 在系统中的角色

```
┌──────────────────┐      BLE 蓝牙       ┌──────────────────┐      UART 串口       ┌──────────────────┐
│  采集端 ESP32    │ ──────────────────→ │  本板 ESP32-S3   │ ──────────────────→ │    Core1106      │
│  + AD8232 肌电   │   推送肌电数据包     │  (你写的代码)     │   单向发送 TX       │  RV1106 G2 模块  │
│  + BLE Client    │                     │  BLE Server      │                     │  AI推理+输出     │
└──────────────────┘                     └──────────────────┘                     └──────────────────┘
```

**ESP32-S3 只做一件事**: BLE 接收肌电数据 → 打包成协议帧 → UART 发给 Core1106

**ESP32-S3 不做的事**:
- 不做 AI 推理（Core1106 负责）
- 不控制 LCD（Core1106 负责）
- 不控制音频输出（Core1106 负责）
- 不接收 UART 回传数据（单向通信，只发不收）

### 1.2 为什么用 ESP-IDF 而不是 Arduino

| 对比项 | Arduino | ESP-IDF |
|--------|---------|---------|
| FreeRTOS 多任务 | 有限支持 | 原生完整支持 |
| BLE 蓝牙栈 | 简单但功能受限 | NimBLE/Bluedrid 完整栈 |
| 内存管理 | 自动（不可控） | 手动（可精细控制） |
| 调试工具 | Serial.println | JTAG + GDB + 系统日志 |
| 生产可靠性 | 一般 | 工业级 |
| 本项目适合度 | ✗ | ✓ |

---

## 2. 硬件引脚映射

> **注意**: 以下引脚编号需要根据你的实际原理图确认！标注 `⚠ 待确认` 的请在烧录前核对。

### 2.1 UART（连接 Core1106）

| 功能 | ESP32-S3 GPIO | 原理图网络名 | 说明 |
|------|--------------|-------------|------|
| TX | GPIO17 ⚠ 待确认 | RX3 | ESP32 发送 → Core1106 接收 |
| RX | GPIO16 ⚠ 待确认 | TX3 | 单向通信，可以不接 |
| 波特率 | 115200 | — | 可调，需与 Core1106 一致 |

### 2.2 BLE 天线

| 功能 | 说明 |
|------|------|
| 天线匹配 | U25 (RFANT5220110A0T) 片上已焊接 |
| 天线类型 | PCB 板载天线 / IPEX 外接（取决于你的板子设计） |

### 2.3 其他重要引脚

| 功能 | GPIO | 说明 |
|------|------|------|
| BOOT 按钮 | GPIO0 ⚠ 待确认 | 按住上电进入下载模式 |
| RST 按钮 | EN (CHIP_PU) | 复位 |
| 电源指示灯 | PWR (LED) | 3.3V 供电正常时亮起 |

### 2.4 如何在原理图中确认引脚

打开你的 `SCH_Schematic1_2026-05-25.pdf`，找到 ESP32-S3 (U27) 附近：
1. 找到标有 `RX3` / `TX3` 的网络标签
2. 追踪它们连接到 ESP32-S3 的哪个 GPIO
3. 修改 `main/config.h` 中对应的 `#define`

---

## 3. 软件架构

### 3.1 FreeRTOS 任务划分

```
┌─────────────────────────────────────────────────────────┐
│                    app_main() 入口                       │
│  1. 初始化 NVS（BLE 需要）                              │
│  2. 创建消息队列 emg_data_q                              │
│  3. 创建 2 个 FreeRTOS 任务                             │
│  4. 启动调度器，不再返回                                 │
└──────────┬──────────────────────────┬───────────────────┘
           │                          │
           ▼                          ▼
┌─────────────────────┐    ┌─────────────────────┐
│ ble_recv_task       │    │ uart_tx_task        │
│ 优先级: 5 (高)      │    │ 优先级: 4 (中)      │
│ 栈大小: 4096        │    │ 栈大小: 4096        │
│                     │    │                     │
│ 循环执行:           │    │ 循环执行:           │
│ 1. NimBLE 事件处理  │    │ 1. 阻塞等队列数据   │
│ 2. BLE 收到数据包   │    │ 2. 打包协议帧       │
│    → 放入 emg_data_q│    │ 3. UART 发送        │
└─────────┬───────────┘    └──────────▲──────────┘
           │                          │
           └──── emg_data_q ──────────┘
              (FreeRTOS 消息队列)
```

### 3.2 优先级设计原则

- **BLE 任务优先级更高 (5)**：BLE 数据到达是异步的，必须及时接收，否则采集端会因缓冲区满而丢包
- **UART 任务优先级稍低 (4)**：只要队列不为空就一直发送，不会被饿死
- 两个任务之间唯一的通信方式是 `emg_data_q` 队列，不存在共享全局变量

---

## 4. 数据流详解

### 4.1 完整数据流（从采集端到 Core1106）

```
采集端 ESP32                         本板 ESP32-S3                         Core1106
────────────                        ──────────────                        ────────
                                                                          
AD8232 采集肌电                                                     
  ↓  ADC 采样                                                             
  ↓  2 通道 × 200Hz                                                    
  ↓                                                                       
原始数据包                                                              
  ↓  BLE GATT Write                                                       
  ↓  (NimBLE 协议栈)                                                       
  ↓                                                                       
  ──── 蓝牙无线 ────→  BLE GATT 回调                                       
                       ble_recv_task                                    
                         ↓  memcpy 到队列                                
                       emg_data_q                                       
                         ↓  xQueueReceive                               
                       uart_tx_task                                     
                         ↓  protocol_send()                             
                         ↓  打包: 0xAA + CMD + LEN + DATA + CRC + 0x55   
                         ↓  uart_write_bytes()                          
                         ──── UART 串口 ────→  UART RX 接收                
                                               protocol_parse()           
                                               AI 推理                    
                                               MAX98357A 语音播报         
                                               LCD 显示文字              
```

### 4.2 各环节延迟估算

| 环节 | 延迟 | 说明 |
|------|------|------|
| BLE 传输 | 5-20ms | 取决于 BLE 连接间隔 |
| 队列传递 | <1ms | FreeRTOS 队列是内存拷贝，极快 |
| 协议打包 | <1ms | 简单内存操作 + CRC 计算 |
| UART 发送 | ~50ms | 115200bps 下 512 字节 ≈ 44ms |
| **ESP32 总延迟** | **~70ms** | 可接受范围 |

---

## 5. 串口通信协议

### 5.1 帧格式

```
┌──────┬──────┬──────┬──────────┬──────┬──────┐
│ 0xAA │ CMD  │ LEN  │  DATA    │ CRC8 │ 0x55 │
│ 1B   │ 1B   │ 2B   │  N 字节  │ 1B   │ 1B   │
└──────┴──────┴──────┴──────────┴──────┴──────┘
 帧头    命令   长度    载荷      校验   帧尾
```

| 字段 | 大小 | 说明 |
|------|------|------|
| **帧头** | 1B | 固定 `0xAA`，表示一帧开始 |
| **CMD** | 1B | 命令类型，肌电数据 = `0x01` |
| **LEN** | 2B | DATA 字段的字节数（大端序，先高后低） |
| **DATA** | N字节 | 实际肌电数据载荷 |
| **CRC8** | 1B | CRC-8 校验，覆盖 CMD+LEN+DATA（不含帧头帧尾） |
| **帧尾** | 1B | 固定 `0x55`，表示一帧结束 |

### 5.2 命令类型

| CMD | 方向 | 含义 |
|-----|------|------|
| `0x01` | ESP32 → Core1106 | 肌电原始数据 |
| `0x02` | 保留 | 未来扩展：控制命令 |
| `0x03` | 保留 | 未来扩展：状态查询 |

### 5.3 示例：发送 200 字节肌电数据

```
原始数据: 200 字节的 ADC 采样值（假设全为 0x12）

打包后的帧（共 206 字节）:
AA  01  00 C8  12 12 12 ... (200个) ... 12  3F  55
│   │   └───   └─ LEN = 0x00C8 = 200 ──┘   │   │
│   │                                      │   └─ 帧尾
│   └─ CMD = 肌电数据                       └─ CRC8
└─ 帧头
```

### 5.4 Core1106 端需要实现的解析逻辑（伪代码）

```c
// Core1106 串口接收端（仅供参考，不在本仓库范围）
#define BUF_SIZE 1024
uint8_t rx_buf[BUF_SIZE];
uint16_t rx_idx = 0;

void uart_rx_byte(uint8_t byte) {
    rx_buf[rx_idx++] = byte;

    // 检测帧尾 0x55
    if (byte == 0x55 && rx_idx >= 6) {
        // 从后往前找帧头 0xAA
        uint16_t head = rx_idx - 6 - rx_buf[rx_idx - 3]; // LEN
        if (head >= 0 && rx_buf[head] == 0xAA) {
            uint8_t cmd  = rx_buf[head + 1];
            uint16_t len = (rx_buf[head + 2] << 8) | rx_buf[head + 3];
            uint8_t crc  = rx_buf[head + 4 + len];
            uint8_t *data = &rx_buf[head + 4];

            if (crc == crc8(data - 1, len + 3)) { // 校验 CMD+LEN+DATA
                process_emg_data(data, len);       // 丢给 AI 推理
            }
        }
        rx_idx = 0; // 重置缓冲区
    }
}
```

---

## 6. 文件结构

```
esp32/                          ← 项目根目录
│
├── README.md                   ← 本文件（你正在读的）
├── CMakeLists.txt              ← ESP-IDF 顶层 CMake 配置
├── sdkconfig.defaults          ← 默认 Kconfig 配置（引脚、BLE等）
│
└── main/                       ← 主程序组件
    ├── CMakeLists.txt          ← main 组件的 CMake 配置
    ├── config.h                ← 【重要】全局配置（引脚、队列、协议）
    ├── main.c                  ← 程序入口 app_main()
    ├── ble_recv.h              ← BLE 接收模块头文件
    ├── ble_recv.c              ← BLE 接收模块实现（NimBLE GATT Server）
    ├── uart_tx.h               ← UART 发送模块头文件
    ├── uart_tx.c               ← UART 发送模块实现
    ├── protocol.h              ← 通信协议头文件（帧结构、CRC）
    └── protocol.c              ← 通信协议实现（打包、校验）
```

### 6.1 各文件职责速查

| 文件 | 职责 | 你改得多吗 |
|------|------|-----------|
| `config.h` | 引脚定义、队列大小、波特率 | **经常改**（调试时调整参数） |
| `main.c` | 创建队列和任务，启动调度器 | 基本不改 |
| `ble_recv.c` | BLE 初始化、GATT Service、接收回调 | **需要改**（与采集端协议对齐） |
| `uart_tx.c` | UART 初始化、数据发送 | 基本不改 |
| `protocol.c` | 帧组包、CRC8 计算 | 基本不改 |
| `CMakeLists.txt` | 编译配置 | 加新文件时改 |

---

## 7. 编译与烧录

### 7.1 环境准备

```bash
# 1. 安装 ESP-IDF v5.x（以 v5.2 为例）
# Windows PowerShell:
git clone -b v5.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
install.bat esp32s3

# 每次打开新终端都要执行:
export.bat

# 2. 确认工具链
idf.py --version
# 应输出: ESP-IDF v5.2.x
```

### 7.2 编译

```bash
# 进入项目目录
cd D:\桌面\比赛\肌电识别\核心处理电路\esp32

# 设置目标芯片为 ESP32-S3
idf.py set-target esp32s3

# 配置项目（可选，首次或改引脚时执行）
idf.py menuconfig

# 编译
idf.py build
```

### 7.3 烧录

```bash
# 通过 USB Type-C 烧录（U26 接口，标注 "esp32烧录"）
idf.py -p COM3 flash       # COM3 替换为你的实际串口号

# 查看设备管理器确认串口号:
# Win+R → devmgmt.msc → 端口(COM和LPT)

# 如果烧录失败，按住 BOOT 键再上电，进入下载模式
idf.py -p COM3 flash monitor   # 烧录 + 打开串口监视器
```

### 7.4 串口监视

```bash
idf.py -p COM3 monitor
# 按 Ctrl+] 退出监视器
```

---

## 8. 调试指南

### 8.1 日志级别

在 `sdkconfig.defaults` 中设置：
```
CONFIG_LOG_DEFAULT_LEVEL_INFO=y   # 正常调试用 INFO
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y  # 详细调试用 DEBUG
CONFIG_LOG_DEFAULT_LEVEL_WARN=y   # 发布时用 WARN
```

### 8.2 关键调试点

| 现象 | 检查方法 | 日志关键词 |
|------|---------|-----------|
| BLE 连不上 | `idf.py monitor` 看 BLE 广播日志 | `BLE advertising started` |
| BLE 收不到数据 | GATT write 回调是否触发 | `EMG data received` |
| 队列阻塞 | 队列是否满 | `Queue full` (如果加了日志) |
| UART 没输出 | 用逻辑分析仪测 TX 引脚 | — |

### 8.3 用逻辑分析仪验证 UART

这是排查通信问题最有效的方法：

```
1. 逻辑分析仪 CH0 接 ESP32-S3 的 UART TX 引脚
2. 波特率设为 115200
3. 触发条件: 下降沿（起始位）
4. 观察是否有 0xAA...0x55 的完整帧输出
```

### 8.4 用 nRF Connect 手机 App 调试 BLE

1. 手机安装 [nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile)
2. 扫描 BLE 设备，找到 `MyoSign_Core`
3. 连接后查看 GATT Service 和 Characteristic
4. 模拟采集端向 Characteristic 写入数据，看 ESP32 日志是否有输出

---

## 9. 常见问题排查

### Q1: 编译报错 "NimBLE not found"
- 确认 `sdkconfig.defaults` 中有 `CONFIG_BT_ENABLED=y` 和 `CONFIG_BT_NIMBLE_ENABLED=y`
- 重新执行 `idf.py set-target esp32s3` 让 Kconfig 生效

### Q2: BLE 广播了但手机搜不到
- 检查天线匹配电路是否焊接正确（U25 RFANT5220110A0T）
- ESP32-S3 的 PCB 天线区域不能覆铜
- 确认没有金属物体遮挡天线

### Q3: UART 发送的数据 Core1106 收不到
- 确认 TX/RX 交叉连接：ESP32 TX → Core1106 RX
- 确认两端波特率一致（都用 115200）
- 用逻辑分析仪测 ESP32 TX 脚是否有波形
- 检查 Core1106 端是否已启动 UART 接收程序

### Q4: 肌电数据延迟很大
- 降低 BLE 连接间隔（Connection Interval），建议 20ms
- 增大 UART 波特率到 256000 或更高
- 减小每包数据量（比如从 512 字节降到 256 字节）

### Q5: ESP32-S3 发热严重
- WiFi 和 BLE 同时开启时功耗较大，本项目只开 BLE，关闭 WiFi
- 确保 `sdkconfig` 中 `CONFIG_ESP_WIFI_ENABLED=n`

---

## 附录 A: 完整引脚速查表

| GPIO | 功能 | 方向 | 连接目标 | 确认状态 |
|------|------|------|---------|---------|
| GPIO17 | UART1 TX | OUT | Core1106 RX | ⚠ 待确认 |
| GPIO16 | UART1 RX | IN | Core1106 TX | ⚠ 待确认（可不接） |
| GPIO0 | BOOT 按钮 | IN | 按键到 GND | ⚠ 待确认 |
| EN | 复位 | IN | RST 按键 | 固定 |

> **请在烧录前逐一确认以上引脚，对照原理图修改 `config.h`！**

---

## 附录 B: 与采集端 ESP32 的 BLE 协议约定

采集端需要以以下方式发送数据：

| 参数 | 值 |
|------|-----|
| BLE 角色 | Client（连接本板 Server） |
| Service UUID | `0x1800` (可自定义) |
| Characteristic UUID | `0x2A00` (可自定义) |
| 操作方式 | GATT Write (No Response) |
| 单包大小 | ≤ 512 字节 |
| 数据格式 | 原始 ADC 采样值（int16 数组） |
| 通道数 | 2 (channel 0 + channel 1 交替) |

---

*文档结束。如有疑问，对照代码中的注释和本文档的对应章节排查。*
