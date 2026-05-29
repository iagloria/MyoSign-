/**
 * ============================================================
 * ble_recv.c — BLE 蓝牙接收模块实现
 * ============================================================
 *
 * 【重要说明】
 * 本模块使用 NimBLE 协议栈（而非 ESP-IDF 默认的 Bluedroid）
 * NimBLE 优点:
 *   - 代码量小（约 1/5）
 *   - 内存占用少（约 1/3）
 *   - 适合资源受限的 MCU
 *
 * BLE GATT 概念速览:
 *
 *   Profile（配置文件）
 *    └── Service（服务）         — 一组相关功能的集合，用 UUID 标识
 *         └── Characteristic（特征）— 具体的数据通道，有 Read/Write/Notify 权限
 *              ├── Value（值）    — 实际传输的数据
 *              └── Descriptor（描述符）— 额外元数据（如 CCCD）
 *
 *   本项目的 GATT 结构:
 *     Service: 0x1800 (肌电数据服务)
 *       └── Characteristic: 0x2A00 (肌电数据通道)
 *            └── 属性: Write (采集端写入) + Write No Response
 *
 * 调用链:
 *   采集端调用 GATT Write
 *     → NimBLE 协议栈接收
 *       → ble_gatt_access_cb (本文件的 emg_chr_access_cb)
 *         → os_mbuf_copydata 提取数据
 *           → xQueueSendFromISR 放入队列
 *             → uart_tx_task 从队列取出发送
 */

#include "ble_recv.h"
#include "protocol.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* ============================================================
 * 静态标签（用于 ESP_LOG 日志输出）
 * 所有日志都会带 "BLE_RECV" 前缀，方便过滤
 * ============================================================ */
static const char *TAG = "BLE_RECV";

/* ============================================================
 * 外部变量引用
 * emg_data_q 在 main.c 中创建，这里声明为 extern 使用
 * ============================================================ */
extern QueueHandle_t emg_data_q;

/* ============================================================
 * BLE 广播状态标志
 * 0 = 空闲, 1 = 正在广播, 2 = 已连接
 * ============================================================ */
static uint8_t ble_advertising = 0;

/* ============================================================
 * 静态函数声明（仅本文件内部使用）
 * ============================================================ */

// ---- GATT 回调: 采集端读写特征值时触发 ----
static int emg_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

// ---- GAP 事件: 连接/断开时触发 ----
static int emg_gap_event_cb(struct ble_gap_event *event, void *arg);

// ---- 启动 BLE 广播 ----
static void start_advertising(void);

// ---- 初始化 NimBLE 并配置 GATT Service ----
static void ble_init(void);

/* ============================================================
 * GATT Service 和 Characteristic 定义
 * ===========================================================*
 *
 * 我们的 Service 只有一个 Characteristic:
 *   - 权限: 只写 (BLE_GATT_CHR_F_WRITE)
 *   - 不需要 Read、Notify、Indicate（单向接收数据）
 */

// ---- GATT Service 定义 ----
// 这里定义一个空的 Service，UUID 在 ble_init() 中通过 ble_uuid 指定
static const struct ble_gatt_svc_def gatt_svcs[] = {

    // =============================================================
    // 肌电数据 Service
    // =============================================================
    {
        // Service 类型: 主服务 (Primary Service)
        .type = BLE_GATT_SVC_TYPE_PRIMARY,

        // Service UUID: 将在 ble_init() 中动态设置
        // .uuid 在后面的函数中通过 ble_uuid_init_from_buf 赋值
        .uuid = NULL,

        // 该 Service 包含的 Characteristic 数组
        .characteristics = (struct ble_gatt_chr_def[]) {

            // ---------------------------------------------------------
            // 肌电数据 Characteristic
            // ---------------------------------------------------------
            {
                // Characteristic UUID: 将在 ble_init() 中动态设置
                .uuid = NULL,

                // 访问回调: 采集端 Write 数据时触发
                .access_cb = emg_gatt_access_cb,

                // 权限标志:
                //   BLE_GATT_CHR_F_WRITE: 允许写入
                .flags = BLE_GATT_CHR_F_WRITE,

                // 没有 Descriptor（不需要 CCCD 等）
                .descriptors = NULL,
            },

            // Characteristic 数组结束标记（必须为空！）
            { 0, NULL, NULL, 0, NULL }
        },
    },

    // Service 数组结束标记（必须为空！）
    { 0, NULL, NULL, NULL }
};

/* ============================================================
 * GATT 访问回调 — 采集端发送肌电数据时触发
 * ============================================================
 *
 * 这是整个 BLE 模块最核心的函数！
 *
 * 触发时机: 采集端通过 BLE 向 Characteristic 写入数据时
 * 执行上下文: NimBLE 协议栈线程（不是 FreeRTOS 任务上下文！）
 *             因此必须用 FromISR 版本的队列操作
 *
 * 参数说明:
 *   conn_handle  — 连接句柄（哪个设备发来的）
 *   attr_handle  — 特征值句柄（哪个 Characteristic 被访问）
 *   ctxt         — 访问上下文，包含操作类型和实际数据
 *   arg          — 注册回调时传入的自定义参数（本例未使用）
 *
 * 返回值:
 *   0 = 成功, 非 0 = NimBLE 错误码
 */
static int emg_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // ---- 1. 判断操作类型 ----
    // 我们只关心 WRITE（采集端写入数据）
    // 采集端可能使用 Write Request 或 Write Command(No Response)
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {

        // ---- 2. 获取数据长度 ----
        // os_mbuf 是 NimBLE 的内存缓冲区管理结构
        // OS_MBUF_PKTLEN 返回整个 mbuf 链的总长度
        uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);

        // ---- 3. 长度校验 ----
        // 不能为 0，不能超过我们设定的最大包大小
        if (data_len == 0 || data_len > EMG_PACKET_MAX_LEN) {
            ESP_LOGW(TAG, "Invalid EMG data length: %d (max %d)", data_len, EMG_PACKET_MAX_LEN);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        // ---- 4. 准备队列数据 ----
        emg_packet_t pkt;
        pkt.len = data_len;

        // ---- 5. 从 NimBLE mbuf 拷贝数据到我们的缓冲区 ----
        // os_mbuf_copydata 将 mbuf 链中的数据扁平化拷贝到连续内存
        int rc = os_mbuf_copydata(ctxt->om, 0, data_len, pkt.data);
        if (rc != 0) {
            ESP_LOGE(TAG, "os_mbuf_copydata failed: %d", rc);
            return BLE_ATT_ERR_UNLIKELY;
        }

        // ---- 6. 放入 FreeRTOS 队列 ----
        // 注意:
        //   这里用 xQueueSendFromISR 而不是 xQueueSend
        //   因为 NimBLE 的回调可能在中断上下文中执行
        //   如果用 xQueueSend 会导致 assert 崩溃
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (xQueueSendFromISR(emg_data_q, &pkt, &xHigherPriorityTaskWoken) != pdPASS) {
            // 队列满了，丢弃数据（防止阻塞 BLE 协议栈）
            // 这说明 UART 发送速度跟不上 BLE 接收速度
            ESP_LOGW(TAG, "EMG data queue full! Dropping oldest data.");
        }

        // ---- 7. 如果需要任务切换，通知 FreeRTOS ----
        // 如果放入队列后，有一个更高优先级的任务（uart_tx_task）
        // 正在等待这个队列，需要立即切换
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }

        // ---- 8. 打印日志（DEBUG 级别，生产环境可注释掉）----
        ESP_LOGD(TAG, "EMG data received: %d bytes, queue free: %d",
                 data_len, uxQueueSpacesAvailable(emg_data_q));
    }

    // ---- 9. 返回成功 ----
    return 0;
}

/* ============================================================
 * GAP 事件回调 — 连接/断开/广播状态变化时触发
 * ============================================================
 *
 * GAP (Generic Access Profile) 负责 BLE 的连接管理
 * 这里我们关注:
 *   - BLE_GAP_EVENT_CONNECT:    设备连接成功
 *   - BLE_GAP_EVENT_DISCONNECT: 设备断开连接
 *
 * 连接断开后自动重新开始广播，等待下一次连接
 */
static int emg_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

        // ---- 设备连接成功 ----
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE connected! conn_handle=%d", event->connect.conn_handle);
            ble_advertising = 2;    // 标记为已连接状态
            break;

        // ---- 设备断开连接 ----
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE disconnected! reason=%d", event->disconnect.reason);
            ble_advertising = 0;    // 回到空闲状态
            start_advertising();    // 自动重新开始广播，等待下次连接
            break;

        // ---- 其他事件暂不处理 ----
        default:
            break;
    }

    return 0;
}

/* ============================================================
 * 启动 BLE 广播
 * ============================================================
 *
 * 广播参数:
 *   - 广播类型: 通用可连接 (BLE_GAP_CONN_MODE_UND, BLE_GAP_DISC_MODE_GEN)
 *   - 广播间隔: 30ms ~ 60ms (平衡发现速度和功耗)
 *   - 广播内容: 设备名称 + 外观类型
 *
 * 广播的作用:
 *   让采集端能在 BLE 扫描列表中看到 "MyoSign_Core"
 *   然后主动发起连接
 */
static void start_advertising(void)
{
    // ---- 如果已经在广播或已连接，不重复操作 ----
    if (ble_advertising != 0) {
        return;
    }

    // ---- 广播参数配置 ----
    struct ble_gap_adv_params adv_params = {
        .conn_mode      = BLE_GAP_CONN_MODE_UND,     // 可连接（不可定向）
        .disc_mode      = BLE_GAP_DISC_MODE_GEN,     // 通用可发现模式
        .itvl_min       = BLE_CONN_INTERVAL_MIN,     // 最小广播间隔
        .itvl_max       = BLE_CONN_INTERVAL_MAX,     // 最大广播间隔
    };

    // ---- 启动广播 ----
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                                &adv_params, emg_gap_event_cb, NULL);
    if (rc == 0) {
        ble_advertising = 1;  // 标记为正在广播
        ESP_LOGI(TAG, "BLE advertising started as '%s'", BLE_DEVICE_NAME);
    } else {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
    }
}

/* ============================================================
 * BLE 同步回调 — NimBLE Host 与 Controller 同步完成后触发
 * ===========================================================*
 *
 * 在 ble_init() 中被注册
 * 当 NimBLE 协议栈完全初始化后，自动调用此函数
 * 此时可以安全地启动广播
 */
static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host synced, starting advertising...");
    start_advertising();
}

/* ============================================================
 * NimBLE Host 任务 — 处理 BLE 协议栈事件
 * ============================================================
 *
 * 这个任务会阻塞在 nimble_port_run() 上
 * 当有 BLE 事件（数据到达、连接变化等）时自动唤醒处理
 *
 * 为什么需要单独的任务？
 *   NimBLE 需要一个独立的任务来运行其事件循环
 *   nimble_port_freertos_init() 会自动创建这个任务
 *   我们只需要在 ble_recv_task 中调用 nimble_port_run()
 */
static void nimble_host_task(void *arg)
{
    ESP_LOGI(TAG, "NimBLE host task started");

    // ---- 无限循环处理 NimBLE 事件 ----
    // nimble_port_run() 只在调用 nimble_port_stop() 时返回
    // 正常情况下永远不会返回
    nimble_port_run();

    // ---- 永远不应该执行到这里 ----
    ESP_LOGE(TAG, "NimBLE host task exited unexpectedly!");
    vTaskDelete(NULL);
}

/* ============================================================
 * BLE 初始化 — 配置 NimBLE 协议栈 + GATT Service
 * ============================================================
 *
 * 初始化步骤:
 *   1. 初始化 NimBLE 端口 (nimble_port_init)
 *   2. 配置 BLE 地址
 *   3. 设置设备名称
 *   4. 配置 GATT Service UUID 和 Characteristic UUID
 *   5. 注册 GATT Service (ble_gatts_count_cfg + ble_gatts_add_svcs)
 *   6. 设置同步回调
 *   7. 启动 NimBLE Host 任务
 */
static void ble_init(void)
{
    int rc;

    // ---- 1. 初始化 NimBLE 端口 ----
    // ESP32 使用 VHCI (Virtual HCI) 接口与 BLE Controller 通信
    esp_nimble_hci_init();
    nimble_port_init();

    // ---- 2. 设置 BLE 地址 ----
    // 使用公共地址模式（不使用随机地址，方便调试）
    ble_hs_cfg.sm_bonding        = 0;   // 不需要配对绑定
    ble_hs_cfg.sm_our_key_dist   = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    // ---- 3. 设置设备名称 ----
    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name: %d", rc);
        return;
    }

    // ---- 4. 配置 GATT Service UUID 和 Characteristic UUID ----
    // NimBLE 要求 UUID 是 ble_uuid_any_t 类型
    // 这里使用 16-bit UUID (BLE UUID 标准格式)
    static ble_uuid16_t svc_uuid = BLE_UUID16_INIT(BLE_EMG_SERVICE_UUID);
    static ble_uuid16_t chr_uuid = BLE_UUID16_INIT(BLE_EMG_CHAR_UUID);

    // 把 UUID 赋值给 gatt_svcs 结构体中的对应字段
    // 由于 gatt_svcs 是 const，这里要通过强制类型转换修改
    // 这是 NimBLE 的标准用法
    ((struct ble_gatt_svc_def *)gatt_svcs)[0].uuid =
        (ble_uuid_t *)&svc_uuid;
    ((struct ble_gatt_svc_def *)gatt_svcs)[0].characteristics[0].uuid =
        (ble_uuid_t *)&chr_uuid;

    // ---- 5. 注册 GATT Service ----
    // 第一步: 计算需要多少属性（attribute）
    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return;
    }

    // 第二步: 注册 Service 和 Characteristic
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "GATT Service registered: 0x%04X", BLE_EMG_SERVICE_UUID);

    // ---- 6. 设置同步回调 ----
    // 当 NimBLE Host 完成初始化后自动调用 ble_on_sync
    ble_hs_cfg.sync_cb = ble_on_sync;

    // ---- 7. 启动 NimBLE Host 任务 ----
    // nimble_port_freertos_init 创建一个 FreeRTOS 任务
    // 任务函数是 nimble_host_task，在该任务中运行 NimBLE 事件循环
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE initialization complete");
}

/* ============================================================
 * BLE 接收任务 — FreeRTOS 任务入口
 * ============================================================
 *
 * 这个任务在 main.c 的 app_main() 中通过 xTaskCreate 创建
 * 它负责:
 *   1. 调用 ble_init() 初始化 BLE
 *   2. 进入无限循环，保持任务存活
 *
 * 注意: NimBLE 的事件处理在 nimble_host_task 中运行
 *       这个任务只是用来保持模块上下文
 *
 * 为什么不让 nimble_host_task 直接做这个任务？
 *   因为 nimble_port_freertos_init 内部会创建自己的任务
 *   我们无法直接控制它的栈大小和优先级
 *   通过一个额外的包装任务，我们可以精细控制
 */
void ble_recv_task(void *arg)
{
    ESP_LOGI(TAG, "BLE recv task started (priority=%d, stack=%d)",
             BLE_TASK_PRIORITY, BLE_TASK_STACK_SIZE);

    // ---- 初始化 BLE ----
    ble_init();

    // ---- 主循环 ----
    // 理论上这个任务不需要做任何事
    // 所有 BLE 处理都在 nimble_host_task 和回调中完成
    // 但 FreeRTOS 任务不能退出，所以用一个无限循环保持存活
    while (1) {
        // 每 10 秒打印一次心跳，确认任务还活着
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGD(TAG, "BLE task heartbeat OK");
    }
}
