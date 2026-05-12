#ifndef WIFI_NET_H
#define WIFI_NET_H

#include <stdbool.h>

#include "esp_err.h"

/*
 *  Wi-Fi 联网模块
 *
 * 功能：
 * 1. 初始化 ESP32-S3 Wi-Fi；
 * 2. 使用 STA 模式连接路由器；
 * 3. 提供连接状态查询；
 * 4. 为 MQTT、NTP 等模块提供网络基础。
 */

esp_err_t wifi_net_init(void);

bool wifi_net_is_connected(void);

esp_err_t wifi_net_wait_connected(int timeout_ms);

#endif /* WIFI_NET_H */