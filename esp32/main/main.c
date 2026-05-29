/**
 * ============================================================
 * main.c — MyoSign ESP32-S3 固件入口
 * ============================================================
 *
 * 【这是什么文件】
 * 这是整个固件的入口点。ESP32-S3 上电后执行的第一个用户代码就是 app_main()。
 *
 * 【app_main() 做了什么】
 *   1. 初始化 NVS（非易失性存储，BLE 协议栈需要）
 *   2. 创建 FreeRTOS 消息队列（BLE任务 和 UART任务 之间的桥梁）
 *   3. 创建 2 个 FreeRTOS 任务（BLE接收 + UART发送）
 *   4. app_main() 返回后，FreeRTOS 调度器接管，2 个任务开始并发运行
 *
 * 【任务之间的关系】
 *
 *   采集端 ESP32                 本板 ESP32-S3                   Core1106
 *   ───────────                 ──────────────                  ────────
 *                                      │
 *                              ┌───────┴───────┐
 *                              │   app_main()   │
 *                              │  1. nvs_init   │
 *                              │  2. 创建队列    │
 *                              │  3. 创建任务    │
 *                              └───┬───────┬───┘
 *                                  │       │
 *                    ┌─────────────┘       └──────────────┐
 *                    ▼                                     ▼
 *           ┌──────────────┐                      ┌──────────────┐
 *           │ ble_recv_task │                      │ uart_tx_task  │
 *           │ 优先级: 5     │    emg_data_q        │ 优先级: 4     │
 *           │               │ ←───────────────     │               │
 *           │ BLE GATT      │    消息队列           │ 取数据        │
 *           │ Server        │                      │ 打包协议帧    │
 *           │ 接收肌电数据  │                      │ UART 发送     │
 *           └──────┬───────┘                      └──────┬───────┘
 *                  │                                      │
 *           BLE 无线接收                           UART 有线发送
 *           从采集端 ESP32                          到 Core1106
 *
 * 【为什么用 FreeRTOS】
 *   如果没有 RTOS，你要自己写一个无限循环轮询（超级循环/Super Loop）:
 *     while(1) {
 *         if (ble_data_available()) process_ble();
 *         if (uart_ready())           send_uart();
 *     }
 *   这种方式的缺点:
 *     - BLE 处理可能阻塞 UART 发送，导致延迟
 *     - 代码耦合严重，一个模块出错影响全局
 *     - 难以精确控制优先级
 *
 *   FreeRTOS 的多任务方式:
 *     - 每个模块独立运行，互不干扰
 *     - 优先级确保 BLE 不会被 UART 阻塞
 *     - 队列提供线程安全的数据传输
 */

#include "config.h"
#include "ble_recv.h"
#include "uart_tx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "nvs_flash.h"

/* ============================================================
 * 静态标签
 * ============================================================ */
static const char *TAG = "MAIN";

/* ============================================================
 * 全局变量 — 消息队列句柄
 * ============================================================
 *
 * 这是整个系统唯一需要全局共享的变量。
 * ble_recv_task 向队列写入数据，uart_tx_task 从队列读取数据。
 * 队列句柄在 app_main() 中创建，在 ble_recv.c 和 uart_tx.c 中
 * 通过 extern 声明引用。
 *
 * 为什么用队列而不是全局数组？
 *   Queue 是线程安全的：FreeRTOS 内部用信号量/互斥锁保护
 *   全局数组不是线程安全的：多任务同时读写会导致数据错乱
 */
QueueHandle_t emg_data_q = NULL;

/* ============================================================
 * 系统初始化
 * ============================================================
 *
 * 在所有任务创建之前，需要初始化 ESP32-S3 的基础设施:
 *   1. NVS (Non-Volatile Storage): BLE 需要 NVS 存储配对信息
 *   2. 其他初始化（如日志系统）由 ESP-IDF 自动完成
 */
static void system_init(void)
{
    // ---- 初始化 NVS ----
    // NVS 是 ESP32 的 flash 存储分区，用于保存:
    //   - BLE 配对密钥
    //   - Wi-Fi 配置（本项目不用）
    //   - 其他需要掉电保存的数据
    //
    // 即使本项目不需要保存任何数据，NimBLE 协议栈也要求 NVS 已初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS 分区损坏或版本不匹配 → 擦除重建
        ESP_LOGW(TAG, "NVS partition corrupted, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "System initialized");
}

/* ============================================================
 * 创建消息队列
 * ============================================================
 *
 * 队列是 BLE 任务和 UART 任务之间的数据通道:
 *
 *   ble_recv_task ── xQueueSend ──→ [emg_data_q] ── xQueueReceive ──→ uart_tx_task
 *
 * 队列特性:
 *   - 先入先出 (FIFO): 最早放入的数据最早被取出
 *   - 线程安全: 多任务同时访问不会冲突
 *   - 阻塞支持: 读空时等数据，写满时丢弃（或等空间）
 */
static void create_queues(void)
{
    emg_data_q = xQueueCreate(
        EMG_DATA_Q_LEN,             // 队列深度: 最多缓存 8 包数据
        sizeof(emg_packet_t)        // 每包数据的大小
    );

    if (emg_data_q == NULL) {
        ESP_LOGE(TAG, "Failed to create emg_data_q! Out of memory?");
        // 队列创建失败是致命错误，重启系统
        esp_restart();
    }

    ESP_LOGI(TAG, "Queue created: %s (depth=%d, item_size=%d)",
             EMG_DATA_Q_NAME, EMG_DATA_Q_LEN, sizeof(emg_packet_t));
}

/* ============================================================
 * 创建 FreeRTOS 任务
 * ============================================================
 *
 * 每个 xTaskCreate 调用会:
 *   1. 在 FreeRTOS 堆中分配栈空间（大小由 STACK_SIZE 指定）
 *   2. 创建任务控制块 (TCB)，包含任务状态、优先级等
 *   3. 将任务加入就绪队列，等待调度器分配 CPU
 *
 * 任务创建顺序:
 *   先创建 BLE 任务（优先级高），再创建 UART 任务（优先级稍低）
 *   但创建顺序不影响运行顺序——调度器按优先级决定谁先运行
 */
static void create_tasks(void)
{
    BaseType_t ret;

    // ---- 1. 创建 BLE 接收任务 ----
    // 参数: 任务函数, 任务名(调试用), 栈大小, 参数, 优先级, 任务句柄
    ret = xTaskCreate(
        ble_recv_task,              // 任务函数入口
        "ble_recv",                 // 任务名称（调试时显示）
        BLE_TASK_STACK_SIZE,        // 栈大小（字节）
        NULL,                       // 传给任务的参数（不需要）
        BLE_TASK_PRIORITY,          // 优先级: 5 (高)
        NULL                        // 任务句柄（不需要保存）
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ble_recv_task!");
        esp_restart();
    }
    ESP_LOGI(TAG, "Task created: ble_recv (priority=%d, stack=%d)",
             BLE_TASK_PRIORITY, BLE_TASK_STACK_SIZE);

    // ---- 2. 创建 UART 发送任务 ----
    ret = xTaskCreate(
        uart_tx_task,               // 任务函数入口
        "uart_tx",                  // 任务名称
        UART_TASK_STACK_SIZE,       // 栈大小
        NULL,                       // 参数
        UART_TASK_PRIORITY,         // 优先级: 4 (中)
        NULL                        // 句柄
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create uart_tx_task!");
        esp_restart();
    }
    ESP_LOGI(TAG, "Task created: uart_tx (priority=%d, stack=%d)",
             UART_TASK_PRIORITY, UART_TASK_STACK_SIZE);
}

/* ============================================================
 * 打印系统信息
 * ============================================================
 *
 * 启动时打印芯片信息，方便调试和确认固件烧录正确
 */
static void print_system_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  MyoSign Core Firmware v1.0");
    ESP_LOGI(TAG, "  Chip: ESP32-S3 (rev %d)", chip_info.revision);
    ESP_LOGI(TAG, "  Cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "  Flash: %lu MB", (unsigned long)(flash_size / (1024 * 1024)));
    ESP_LOGI(TAG, "  FreeRTOS tick: %d Hz", configTICK_RATE_HZ);
    ESP_LOGI(TAG, "  BLE name: %s", BLE_DEVICE_NAME);
    ESP_LOGI(TAG, "  UART: port=%d, TX=GPIO%d, baud=%d",
             UART_1106_PORT, UART_1106_TX_PIN, UART_1106_BAUDRATE);
    ESP_LOGI(TAG, "==============================================");
}

/* ============================================================
 * app_main() — 程序入口
 * ============================================================
 *
 * 这是 ESP-IDF 框架要求的主函数，由启动代码自动调用。
 * 执行完毕后 FreeRTOS 调度器接管，开始运行我们创建的任务。
 *
 * app_main() 不是普通的 main() 函数！
 *   - 它运行在 main 任务中（FreeRTOS 默认创建）
 *   - app_main() 返回后 main 任务会被自动删除
 *   - 不要在 app_main() 中写无限循环（会阻塞调度器启动）
 *
 * 启动流程:
 *   ROM Bootloader → 2nd stage Bootloader → FreeRTOS 初始化 → app_main() → 调度器运行
 */
void app_main(void)
{
    // ---- 第 1 步: 初始化系统基础设施 ----
    system_init();

    // ---- 第 2 步: 打印芯片信息 ----
    print_system_info();

    // ---- 第 3 步: 创建消息队列 ----
    // 队列必须在任务之前创建，因为任务启动后就会使用队列
    create_queues();

    // ---- 第 4 步: 创建 FreeRTOS 任务 ----
    create_tasks();

    // ---- 第 5 步: app_main() 返回 ----
    // 返回后 FreeRTOS 调度器开始运行
    // ble_recv_task 和 uart_tx_task 开始并发执行
    ESP_LOGI(TAG, "app_main() done. FreeRTOS scheduler is now running.");
    ESP_LOGI(TAG, "Waiting for BLE connection and EMG data...");

    // ---- 补充: 如何在 main 任务中监控系统状态？ ----
    // 如果你想在 app_main() 中持续监控（比如打印运行时间）
    // 可以把 return 改成 while(1) { vTaskDelay(10000); }
    // 但注意这会增加一个额外任务的开销
}
