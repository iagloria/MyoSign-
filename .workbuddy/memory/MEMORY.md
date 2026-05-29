# MyoSign 核心电路板 — 项目记忆

## 项目概述
- **项目名称**: MyoSign — 面向聋人群体的智能交流辅具
- **比赛**: 本科生创意组
- **指导老师**: 杨照辉、周晓馨
- **核心芯片**: ESP32-S3 (R8) + Core1106 (瑞芯微 RV1106 G2)

## 硬件架构（2026-05-25 修正）

### 电路板（嘉立创 EDA 设计，2页原理图 + PCB）
- **ESP32-S3 (U27)**: 主控 MCU，负责 BLE 蓝牙接收肌电数据 + UART 转发
- **Core1106 (U30)**: AI 协处理模块，负责肌电识别推理 + 语音输出 + LCD 显示
- **MAX98357A (U2)**: D 类音频功放，I2S 接口
- **TP4055 (U28)**: 锂电池充电管理
- **MT3608 (U29)**: DC-DC 升压（电池 → 5V）
- **ME6217 (U6)**: 5V → 3.3V LDO
- **模拟麦克风**: U3 端子 + L1/L2 磁珠滤波 + C3/C4 电容
- **LCD**: H3 8Pin SPI 接口

### 关键修正点
- ❌ ~~INMP441 数字麦克风~~ → ✅ 模拟驻极体麦克风
- ❌ ~~ESP32-C3 存在~~ → ✅ 只有 ESP32-S3
- ❌ ~~IMU 惯性单元~~ → ✅ 不需要 IMU
- ❌ ~~数字人手语动画~~ → ✅ 只有音频 + LCD 文字两个输出
- ❌ ~~双向 UART~~ → ✅ 单向: ESP32 → Core1106

### 用户工作范围
- 硬件电路设计（已完成原理图 + PCB）
- ESP32-S3 嵌入式代码（BLE 接收 + UART 转发）
- 不需要管: AI 识别模型 / Core1106 端软件

## 嵌入式代码架构（2026-05-25）

### 文件位置
- `esp32/` — 完整 ESP-IDF v5.x 项目

### 架构
- 框架: ESP-IDF v5.x + NimBLE + FreeRTOS
- 2 个任务: ble_recv_task (优先级5) + uart_tx_task (优先级4)
- 1 个队列: emg_data_q
- 数据流: BLE → 队列 → UART 协议打包 → Core1106

### 串口协议
- 帧格式: 0xAA + CMD(1B) + LEN(2B) + DATA + CRC8 + 0x55
- 命令: 0x01 = EMG_DATA

## 项目文件清单
- `SCH_Schematic1_2026-05-25.pdf` — 原理图（2页）
- `PCB_PCB1_2026-05-25.pdf` — PCB 设计（7层）
- `BOM_Board1_Schematic1_2026-05-25.xlsx` — BOM 表（51种元器件）
- `PickAndPlace_PCB1_2026_05_25.csv` — 贴片坐标
- `Core1106-PinOut.xls` — Core1106 113脚定义
- `Core1106.pdf` — Core1106 模块原理图（Altium 导出）
