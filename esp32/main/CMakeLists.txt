# ============================================================
# main 组件 CMake 配置
# ============================================================
# 告诉 ESP-IDF 这个组件包含哪些源文件、需要哪些依赖

# ---- 注册为 ESP-IDF 组件 ----
idf_component_register(
    SRCS
        "main.c"            # 程序入口
        "ble_recv.c"        # BLE 蓝牙接收
        "uart_tx.c"         # UART 串口发送
        "protocol.c"        # 通信协议（帧打包 + CRC）

    INCLUDE_DIRS
        "."                 # 头文件搜索路径（当前目录）

    REQUIRES
        nvs_flash           # NVS 存储（BLE 必需）
        bt                  # NimBLE 蓝牙协议栈（ESP-IDF v5 用 bt 组件）
        driver              # 外设驱动（UART、GPIO 等）
        freertos            # 实时操作系统
        esp_timer           # ESP 定时器
        esp_hw_support      # esp_chip_info
        spi_flash           # esp_flash_get_size
)
