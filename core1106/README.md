# ============================================================
# MyoSign Core1106 (RV1106) 开发文档
# ============================================================

> 版本: v1.0  更新日期: 2026-05-29
> 目标硬件: 幸狐 Core1106 (RV1106 G2) 嵌入式 Linux
> 开发语言: C11 + CMake，交叉编译到 ARM uClibc
> 仓库路径: `核心处理电路/core1106/`

---

## 目录

1. [系统全景](#1-系统全景)
2. [硬件与引脚](#2-硬件与引脚)
3. [软件架构](#3-软件架构)
4. [目录结构与每个文件做什么](#4-目录结构与每个文件做什么)
5. [事件总线与模块间通信](#5-事件总线与模块间通信)
6. [编译与部署](#6-编译与部署)
7. [运行时调试](#7-运行时调试)
8. [后续开发路线图（AI 模型集成）](#8-后续开发路线图)
9. [接口契约 — 模型团队需要交付什么](#9-接口契约)
10. [常见问题](#10-常见问题)

---

## 1. 系统全景

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                            MyoSign 双向交流系统                                   │
│                                                                                 │
│  聋人侧                                                              健听人侧      │
│  ─────                                                              ──────      │
│                                                                                 │
│   AD8232 ──→ 采集端 ESP32 ──BLE──▶ ESP32-S3 ──UART──▶ Core1106                   │
│   (前臂肌电)   (双通道ADC)         (本板,中继)         (本板,大脑)                  │
│                                                            │                    │
│                                                ┌───────────┼───────────┐        │
│                                                │           │           │        │
│                                                ▼           ▼           ▼        │
│                                            LCD 屏幕    喇叭(MAX98357A) 麦克风     │
│                                            (双区文字)   (说聋人想说的)   (听健听人) │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

两条端到端流水线：

| 流水线 | 起点 | 终点 | 关键 AI |
|---|---|---|---|
| **A 聋人 → 健听人** | 手势(EMG) | 喇叭语音 | EMG 分类 → 词序整理 → TTS |
| **B 健听人 → 聋人** | 麦克风音频 | LCD 文字 | VAD → ASR |

---

## 2. 硬件与引脚

详见 `SCH_Schematic1_2026-05-25.pdf`。本仓库代码相关的接口：

| 接口 | 设备 | 程序中位置 | 备注 |
|---|---|---|---|
| **业务 UART** (网络名 RX/TX) | 收 ESP32-S3 | `UART_DEV_BIZ` in `config.h` | 默认 `/dev/ttyS3`，按 SDK pinmux 改 |
| **调试 UART** (网络名 RX3/TX3) | 接 H4 排针、USB-TTL | 不在程序里打开 | 默认作为 console + shell |
| **SPI LCD** | H3 8-pin | `LCD_SPI_DEV` | `/dev/spidev0.0`，速率 40 MHz |
| **DC / RES / BLK** | LCD | `LCD_GPIO_*` | `/sys/class/gpio/gpioN/value` |
| **I²S0 OUT** | MAX98357A | `AUDIO_OUT_CARD` | ALSA `hw:0,0` |
| **I²S1 IN** | INMP441 (H1+H2) | `MIC_IN_CARD` | ALSA `hw:1,0` |
| **BAT_ADC** | TP4055 + 分压 | `BAT_ADC_PATH` | `/sys/bus/iio/.../in_voltage0_raw` |

> 真正部署时所有节点名和 GPIO 编号都要按 RV1106 Buildroot/Tiny SDK 的实际 `dmesg`、`ls /dev`、`cat /proc/asound/cards` 校准。请在 `include/config.h` 修改后重新编译。

---

## 3. 软件架构

**多线程 + 事件总线（pub/sub）**。每个模块自己一根 pthread，通过 `event_bus_publish` / `event_bus_pop` 通信，不共享可变状态。

```
                   ┌─────────────────────────────────────────┐
                   │            event_bus (queue per type)   │
                   └─────────────────────────────────────────┘
                       ▲    ▲    ▲    ▲    ▲    ▲    ▲    ▲
       ┌───────────────┘    │    │    │    │    │    │    └───────────────┐
       │              ┌─────┘    │    │    │    │    └──────┐             │
   uart_rx          mic_in      power │   tts   ui          asr           main
   (生产EMG_FRAME)  (生产MIC_PCM)(生产 │  (消费   (消费各种   (消费MIC_PCM)  (启停)
                                POWER) │   TTS_   显示事件)   生产ASR_TEXT)
                                       │   REQ)
                                  emg_infer
                                  (EMG_FRAME→GESTURE_TEXT→TTS_REQ)
                                       │
                                  sentence_fixer
                                  (GESTURE_TEXT→FIXED_TEXT)
```

线程优先级（Linux SCHED_FIFO，配置在 `config.h`）：
- uart_rx / mic_in 高（实时数据采集，不能丢）
- audio_out 高（避免欠载爆音）
- emg_infer / asr_infer 中（推理可有抖动）
- ui / power 低

---

## 4. 目录结构与每个文件做什么

```
core1106/
├── CMakeLists.txt                顶层构建
├── cmake/rv1106-toolchain.cmake  交叉编译工具链
├── include/                      头文件（公开 API）
│   ├── config.h                  ★ 所有引脚/路径/采样率/优先级
│   ├── log.h                     日志宏（LOGI/LOGW/LOGE）
│   ├── ring_buf.h                单生产单消费环形缓冲
│   ├── event_bus.h               线程安全 pub/sub
│   ├── protocol.h                帧结构 + CRC8 + 增量解析器
│   ├── uart_rx.h                 业务 UART 读线程
│   ├── lcd.h                     SPI LCD 驱动
│   ├── audio_out.h               ALSA 播放（MAX98357A）
│   ├── mic_in.h                  ALSA 录音（INMP441）
│   ├── power.h                   电池采样线程
│   ├── ui.h                      UI 渲染线程
│   ├── emg_infer.h               EMG 推理（stub）
│   ├── asr_engine.h              ASR（stub）
│   ├── tts_engine.h              TTS（stub + WAV回退）
│   └── sentence_fixer.h          语序整理（stub）
├── src/                          实现
│   ├── main.c                    启动 / 信号 / 优雅退出
│   ├── log.c
│   ├── ring_buf.c
│   ├── event_bus.c
│   ├── protocol.c                与 ESP32 帧一致的解帧器
│   ├── uart_rx.c
│   ├── lcd.c                     ST7789 初始化序列；blit；text 待 freetype
│   ├── audio_out.c               同步 PCM/WAV 播放
│   ├── mic_in.c                  PCM → EV_MIC_PCM 投递
│   ├── power.c                   30s 一次 ADC 采样
│   ├── ui.c                      上半屏聋人输出、下半屏健听人输出
│   ├── emg_infer.c   ★stub       滑动窗 + 投票 + emg_model_infer()
│   ├── asr_engine.c  ★stub       订阅 PCM；asr_decode_chunk()
│   ├── tts_engine.c  ★stub       优先放预录 WAV，否则 stub
│   └── sentence_fixer.c ★stub    词攒满或超时→fix→发布
└── assets/
    └── gesture_dict.json         手势词表（与模型 logits 对齐）
```

带 ★ 的 4 个文件是"待接 AI 模型"的位置，整体框架已就绪，只需替换函数体即可。

---

## 5. 事件总线与模块间通信

`include/event_bus.h` 定义了所有事件类型：

| 事件 | 生产者 | 消费者 | 载荷 |
|---|---|---|---|
| `EV_EMG_FRAME`     | uart_rx          | emg_infer | 原始 EMG 字节流（int16×N） |
| `EV_EMG_GESTURE`   | emg_infer (预留) | —         | int label |
| `EV_GESTURE_TEXT`  | emg_infer        | ui / sentence_fixer | UTF-8 词 |
| `EV_FIXED_TEXT`    | sentence_fixer   | ui / tts | UTF-8 整句 |
| `EV_TTS_REQUEST`   | emg_infer / fixer| tts_engine | UTF-8 文字 |
| `EV_MIC_PCM`       | mic_in           | asr_engine | int16 PCM 20ms |
| `EV_ASR_TEXT`      | asr_engine       | ui | UTF-8 句子 |
| `EV_POWER_UPDATE`  | power            | ui | `power_state_t` |
| `EV_DISPLAY_UPDATE`| 任何             | ui | 自定义 |
| `EV_SHUTDOWN`      | main             | 所有 | 无 |

**载荷由发布者 malloc/拷贝，消费者 pop 后必须 free。** 见 `event_bus.c`。

队列满时**丢最旧**（生产者优先），保证实时数据流不被卡。

---

## 6. 编译与部署

### 6.1 PC 仿真编译（验证逻辑）

```bash
cd 核心处理电路/core1106
cmake -B build -DENABLE_SIM=ON
cmake --build build -j
./build/myosign        # 所有硬件相关函数为空操作，但事件流和日志能跑
```

### 6.2 交叉编译到 RV1106

```bash
# 1) 准备 SDK，假设解压在 ~/rv1106-sdk
export RV1106_SDK=~/rv1106-sdk

cmake -B build-arm \
      -DCMAKE_TOOLCHAIN_FILE=cmake/rv1106-toolchain.cmake \
      -DRV1106_SDK=$RV1106_SDK
cmake --build build-arm -j

# 2) 推到设备（通过 adb / scp / U盘）
scp build-arm/myosign root@<ip>:/opt/myosign/bin/
scp -r assets/      root@<ip>:/opt/myosign/

# 3) 在设备上运行
ssh root@<ip>
mkdir -p /var/log/myosign
/opt/myosign/bin/myosign
```

### 6.3 开启 NPU/ASR/TTS

模型就绪后，加 CMake 选项：
```bash
cmake -B build-arm -DENABLE_NPU=ON -DENABLE_ASR=ON -DENABLE_TTS=ON ...
```
并把动态库（`librknnrt.so`、`libsherpa-onnx-c-api.so` 等）放到设备 `/usr/lib/`。

### 6.4 开机自启

`systemd` 服务示例 `/etc/systemd/system/myosign.service`：
```ini
[Unit]
Description=MyoSign Bidirectional Communicator
After=network.target sound.target

[Service]
ExecStart=/opt/myosign/bin/myosign
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
```
`systemctl enable myosign && systemctl start myosign`。

---

## 7. 运行时调试

### 7.1 看日志
程序写 `/var/log/myosign/myosign.log` + stderr。
- 实时跟踪：`tail -f /var/log/myosign/myosign.log`
- 改级别：编译加 `-DMYOSIGN_LOG_LEVEL=LOG_LEVEL_DEBUG`

### 7.2 验证 UART 通路
```bash
stty -F /dev/ttyS3 115200 raw
hexdump -C /dev/ttyS3
```
应能看到 `AA 01 ... 55` 帧。

### 7.3 验证音频回路
```bash
# 录 5 秒原始 PCM
arecord -D hw:1,0 -f S16_LE -c 1 -r 16000 -d 5 /tmp/test.wav
# 播回
aplay  -D hw:0,0 /tmp/test.wav
```

### 7.4 验证 LCD
临时把 `lcd_fill(0xF800)`（红）/ `0x07E0`（绿）写进 `main.c` 看屏。

### 7.5 调试串口登录
通过 H4 排针接 USB-TTL（RX3/TX3）查看 uboot + kernel log + 拿到 root shell。

---

## 8. 后续开发路线图

按优先级与依赖列出。粗体为"接入 AI 模型时需做的事"。

### Phase 0：链路打通（不依赖模型，**已就绪**）
- [x] ESP32-S3 ↔ Core1106 UART 协议帧解析
- [x] LCD 初始化与画面刷新框架
- [x] 麦克风采集线程
- [x] 喇叭播放（PCM/WAV）线程
- [x] 电池监测线程
- [x] 事件总线与多线程编排
- [ ] **集成 freetype + 中文子集字体** → 完善 `lcd_draw_text_utf8`
- [ ] 屏上做"双区滚动文本 + 状态栏"美化

### Phase 1：EMG 推理上线
1. 用 `肌电分类/EMG_data_1_3-22-2026/` 训练 baseline 模型（PyTorch CNN1D 或 LightGBM）
2. 导出 ONNX → `rknn-toolkit2` 转 RKNN
3. **改 `src/emg_infer.c` 的 `emg_model_infer()`**：加载 .rknn、做归一化、调 `rknn_run`、softmax
4. 用 `assets/gesture_dict.json` 校准索引
5. 实测端到端延迟，必要时调窗长 / 步长

### Phase 2：TTS 上线
两种路线选其一：
- **路线 A（最快）**：预录手势词的 WAV → 放 `/opt/myosign/voice/<词>.wav`，当前 `tts_engine.c` 已经支持
- **路线 B（更自然）**：集成 sherpa-onnx VITS 中文小模型 → **改 `tts_engine.c` 的 `tts_synth_to_pcm()`**

### Phase 3：ASR 上线
1. 选 sherpa-onnx 流式 zipformer 中文模型（量化后 < 50 MB）
2. 部署到 `/opt/myosign/models/asr/`
3. **改 `src/asr_engine.c` 的 `asr_decode_chunk()`**：流式喂 PCM、检测 endpoint、发 `EV_ASR_TEXT`
4. 在 `mic_in` 之前加 VAD（能量阈值或 Silero VAD）减少误触发

### Phase 4：语序整理上线
1. 收集"手语词序列 ↔ 自然汉语"训练对
2. 训练小型 seq2seq（Transformer 4 层 / GPT2-tiny / 微调 Qwen 0.5B 量化）
3. **改 `src/sentence_fixer.c` 的 `sentence_fix()`** 调用模型
4. 调批量条件（每多少词或多久触发一次重写）

### Phase 5：产品化
- 电源管理：低电关音频 / 屏休眠
- 看门狗 + 异常重启
- OTA 升级脚本
- 用户配置界面（音量、亮度、词表自定义）

---

## 9. 接口契约

> 给做 AI 模型的同学：你只需要交付符合下面接口的函数实现，业务代码全自动跑通。

### 9.1 EMG 分类模型

需要交付的 C 接口（替换 `src/emg_infer.c` 中的 `emg_model_infer`）：

```c
/**
 * @param  window     长度 EMG_WINDOW_SAMPLES * EMG_CHANNELS 的 int16 数组
 *                    布局: ch0, ch1, ch0, ch1, ... （时间顺序）
 * @param  n_samples  = EMG_WINDOW_SAMPLES
 * @param  conf_out   返回 [0,1] 置信度
 * @return            label ∈ [0, EMG_NUM_CLASSES) 或 -1（无可信结果）
 */
int emg_model_infer(const int16_t *window, int n_samples, float *conf_out);
```
**约束**：单次推理 < 50 ms；输出 label 索引必须与 `assets/gesture_dict.json` 顺序一致。

### 9.2 ASR 模型

需要交付的 C 接口（在 `src/asr_engine.c` 内调用，最终通过 `event_bus_publish(EV_ASR_TEXT, ...)` 上送）：

```c
/**
 * 流式喂 PCM；遇到 endpoint 时通过 event_bus_publish 发 EV_ASR_TEXT (UTF-8)。
 * @param pcm        16kHz / mono / S16LE，长度 n_samples
 */
void asr_decode_chunk(const int16_t *pcm, int n_samples);
```
**约束**：实时因子 < 0.5；中文识别 CER < 15%。

### 9.3 TTS 模型

```c
/**
 * @param  text     UTF-8 中文（短句，< 20 字）
 * @param  pcm_out  *pcm_out = malloc(...)；返回后由调用方 free
 * @param  n_out    采样点数（16kHz S16LE 单声道）
 * @return          true=成功
 */
bool tts_synth_to_pcm(const char *text, int16_t **pcm_out, size_t *n_out);
```
**约束**：< 1 秒生成 5 字短语。

### 9.4 语序整理模型

```c
/**
 * @param  raw  形如 "我 水 喝" 的空格分隔手语原序
 * @return      静态/堆内存 UTF-8 字符串（"我喝水"）。若无可改进，直接返回 raw。
 *
 * 当前实现是 stub（直接返回 raw）。模型集成后请改成：
 *   - 内部维护可重入状态（线程安全），或
 *   - 返回 thread-local static buffer
 */
const char *sentence_fix(const char *raw);
```

---

## 10. 常见问题

**Q1：编译报 `<alsa/asoundlib.h>` 找不到？**
A：交叉编译时要确认 RV1106 sysroot 里有 ALSA 头文件（一般 `output/host/.../sysroot/usr/include/alsa/`）。或临时加 `-DENABLE_SIM=ON` 跑仿真。

**Q2：UART 收不到数据？**
- 用 `dmesg | grep tty` 确认业务串口节点名
- 用 `cat /proc/tty/drivers` 验证已绑定
- 关掉 console 抢占 → `/etc/inittab` 删去对应 getty
- 用万用表/逻辑分析仪测 RX 脚波形

**Q3：声音卡顿？**
- 增大 `AUDIO_OUT_PERIOD_MS` 到 40
- 把 audio_out 线程优先级调高到 70

**Q4：LCD 全白/全黑？**
- 检查 RES 复位时序（至少 10 ms 低再高）
- SPI 速率先降到 4 MHz 验证；OK 再加速
- 调换 DC 极性

**Q5：能不能不用 ALSA 直接操作 I²S？**
- 可以但不推荐。RV1106 SDK 已自带 ALSA，配好 device tree 后省去自己写 DMA 驱动的麻烦。

---

*（文档结束。代码与文档绑定演进，每次改 `config.h` 和模块接口时请同步更新本文。）*
