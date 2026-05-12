#ifndef LOCAL_RECORD_DB_H
#define LOCAL_RECORD_DB_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 *  本地 CSV 记录缓存模块
 *
 * 本文件名仍然叫 local_record_db，是为了保持上层接口不变。
 * 实际底层已经从 SQLite 改成 CSV 文件。
 *
 * CSV 文件路径：
 *      /spiffs/door_records.csv
 *
 * CSV 字段：
 *      id,uploaded,retry_count,json
 *
 * uploaded 字段：
 *      0：未上传
 *      1：已上传
 *      2：正在发布，等待 MQTT_EVENT_PUBLISHED 确认
 */

typedef struct {
    int id;
    char json[512];
} pending_record_t;

esp_err_t local_record_db_init(void);

esp_err_t local_record_db_insert_event(const char *event_type,
                                       const char *user_id,
                                       const char *method,
                                       bool alarm,
                                       const char *time_str,
                                       int64_t uptime_ms);

esp_err_t local_record_db_get_pending(pending_record_t *record);

esp_err_t local_record_db_mark_publishing(int id);

esp_err_t local_record_db_mark_uploaded(int id);

esp_err_t local_record_db_reset_publishing(void);

esp_err_t local_record_db_increment_retry(int id);

int local_record_db_count_pending(void);

#endif /* LOCAL_RECORD_DB_H */