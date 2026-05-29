# MyoSign 核心处理电路

基于肌电信号的手语双向交流系统，聋人通过前臂肌电识别手势，转换为语音或文字输出。

## 系统架构

```
AD8232 → 采集端ESP32 ──BLE──→ ESP32-S3 ──UART──→ Core1106 (RV1106)
  肌电采集                     BLE中继               AI推理 + LCD + 音频
```

## 目录结构

```
├── esp32/           ESP32-S3 固件（ESP-IDF），BLE 接收 + 协议封装 + UART 转发
├── core1106/        Core1106 主控程序（C11 + CMake），多线程事件总线 + AI 推理
├── PCB信息/         原理图、PCB 布局、BOM 表、贴片坐标
└── Core1106-Footprint*/  幸狐 Core1106 模块封装库
```

## 快速开始

- **ESP32-S3**：详见 `esp32/README.md`
- **Core1106**：详见 `core1106/README.md`

## 硬件栈

| 模块 | 芯片 | 作用 |
|------|------|------|
| 肌电采集 | AD8232 + ESP32 | 双通道肌电信号采集 |
| BLE 中继 | ESP32-S3 | 接收 BLE 数据，串口转发 |
| AI 推理 | RV1106 G2 (Core1106) | EMG 分类、ASR、TTS |
| 显示 | ST7789 SPI LCD | 双区文字显示 |
| 音频输入 | INMP441 (I²S) | 麦克风拾音 |
| 音频输出 | MAX98357A (I²S) | 语音播报 |
