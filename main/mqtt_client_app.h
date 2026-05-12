#ifndef MQTT_CLIENT_APP_H
#define MQTT_CLIENT_APP_H

#include <stdbool.h>

#include "esp_err.h"

/*
 *  MQTT 通信模块
 *
 * 职责：
 * 1. 连接 MQTT Broker；
 * 2. 发布门禁事件；
 * 3. 发布设备状态；
 * 4. 在 MQTT_EVENT_PUBLISHED 中确认记录上传成功；
 * 5. 支持断网后自动恢复。
 */

esp_err_t mqtt_client_app_init(void);

esp_err_t mqtt_client_app_start(void);

esp_err_t mqtt_client_app_stop(void);

bool mqtt_client_app_is_connected(void);

/*
 * 发布指定 SQLite 记录对应的 JSON。
 *
 * 参数：
 * record_id：SQLite 中 unlock_records 表的 id；
 * json：要发布的事件 JSON。
 */
esp_err_t mqtt_client_app_publish_event(int record_id, const char *json);

esp_err_t mqtt_client_app_publish_status(const char *status);

#endif /* MQTT_CLIENT_APP_H */