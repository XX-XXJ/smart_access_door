#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "esp_err.h"

/*
 *  功耗管理模块
 *
 * 功能：
 * 1. 记录用户活动；
 * 2. 长时间无操作后进入 Deep Sleep；
 * 3. 进入睡眠前关闭 MQTT 和 Wi-Fi；
 * 4. 配置 GPIO 和定时器唤醒。
 */

esp_err_t power_manager_init(void);

void power_manager_feed_activity(void);

void power_manager_enter_deep_sleep(void);

#endif /* POWER_MANAGER_H */