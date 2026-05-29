/**
 * ============================================================
 * ble_recv.h — BLE 蓝牙接收模块头文件
 * ============================================================
 *
 * 本模块负责:
 *   1. 初始化 NimBLE 蓝牙协议栈
 *   2. 创建 GATT Service（肌电数据服务）
 *   3. 创建 GATT Characteristic（采集端写入的通道）
 *   4. 注册 GATT Write 回调（采集端发来数据时的处理函数）
 *   5. 启动 BLE 广播（让采集端能发现并连接本设备）
 *   6. 在 FreeRTOS 任务中循环处理 NimBLE 事件
 *
 * BLE 角色关系:
 *   本板 ESP32-S3:  GATT Server（提供服务，被动等待连接）
 *   采集端 ESP32:   GATT Client（主动连接，写入数据）
 *
 * 数据流:
 *   采集端 → GATT Write → NimBLE 栈 → emg_chr_write_cb() → emg_data_q → UART 任务
 */

#ifndef BLE_RECV_H
#define BLE_RECV_H

#include "config.h"

/**
 * @brief  BLE 接收任务入口函数
 * @param  arg  任务参数（未使用，传 NULL）
 *
 * 这个函数会被 xTaskCreate() 作为任务入口调用
 * 它会初始化 BLE 并进入无限循环处理 NimBLE 事件
 * 正常情况下永不返回
 */
void ble_recv_task(void *arg);

#endif /* BLE_RECV_H */
