#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <stdbool.h>

#include "esp_err.h"

/*
 *  事件日志模块
 *
 * 新方案：
 * 1. record_event() 只负责记录事件；
 * 2. 所有事件先写入 CSV
 * 3. MQTT 上传任务定时查询 CSV 中未上传记录；
 * 4. 上传确认后再更新数据库状态。
 */

esp_err_t event_log_init(void);

void record_event(const char *event_type,
                  const char *user_id,
                  const char *method,
                  bool alarm);

#endif /* EVENT_LOG_H */