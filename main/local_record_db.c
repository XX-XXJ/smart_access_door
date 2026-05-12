#include "local_record_db.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_spiffs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "LOCAL_CSV";

#define SPIFFS_BASE_PATH            "/spiffs"

#define CSV_LINE_MAX                1024
#define CSV_JSON_MAX                512

static SemaphoreHandle_t s_file_mutex = NULL;
static int s_next_record_id = 1;


typedef struct {
    int id;
    int uploaded;
    int retry_count;
    char json[CSV_JSON_MAX];
} csv_record_t;


typedef enum {
    UPDATE_MARK_PUBLISHING = 0,
    UPDATE_MARK_UPLOADED,
    UPDATE_INCREMENT_RETRY,
    UPDATE_RESET_PUBLISHING
} update_mode_t;


/*
 *  SPIFFS 初始化
 *
 * CSV 文件保存在 SPIFFS 分区中。
 */
static esp_err_t spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0;
    size_t used = 0;

    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS total=%d, used=%d", total, used);
    } else {
        ESP_LOGW(TAG, "SPIFFS info failed: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}


/*
 *  写 CSV 表头
 */
static void write_csv_header(FILE *fp)
{
    fprintf(fp, "id,uploaded,retry_count,json\n");
}


/*
 *  判断是否为 CSV 表头
 */
static bool is_csv_header(const char *line)
{
    if (line == NULL) {
        return false;
    }

    return strncmp(line, "id,uploaded,retry_count,json", 28) == 0;
}


/*
 *  写一条 CSV 记录
 *
 * CSV 中 json 字段使用双引号包裹。
 * JSON 内部的双引号会写成两个双引号。
 */
static esp_err_t write_record_line(FILE *fp, const csv_record_t *rec)
{
    if (fp == NULL || rec == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    fprintf(fp,
            "%d,%d,%d,\"",
            rec->id,
            rec->uploaded,
            rec->retry_count);

    for (int i = 0; rec->json[i] != '\0'; i++) {
        char ch = rec->json[i];

        if (ch == '"') {
            fputc('"', fp);
            fputc('"', fp);
        } else if (ch == '\r' || ch == '\n') {
            fputc(' ', fp);
        } else {
            fputc(ch, fp);
        }
    }

    fprintf(fp, "\"\n");

    return ESP_OK;
}


/*
 *  解析 CSV 中的 json 字段
 */
static esp_err_t parse_csv_json_field(const char *p,
                                      char *out,
                                      size_t out_size)
{
    if (p == NULL || out == NULL || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0] = '\0';

    /*
     * 正常格式：
     *      "json..."
     */
    if (*p == '"') {
        p++;

        size_t pos = 0;

        while (*p != '\0' && pos + 1 < out_size) {
            if (*p == '"') {
                /*
                 * CSV 转义：
                 *      "" 表示 JSON 中的一个 "
                 */
                if (*(p + 1) == '"') {
                    out[pos++] = '"';
                    p += 2;
                    continue;
                }

                /*
                 * 单独一个 " 表示字段结束。
                 */
                break;
            }

            if (*p == '\r' || *p == '\n') {
                break;
            }

            out[pos++] = *p;
            p++;
        }

        out[pos] = '\0';
        return ESP_OK;
    }

    /*
     * 兼容非引号格式。
     */
    size_t pos = 0;

    while (*p != '\0' &&
           *p != '\r' &&
           *p != '\n' &&
           pos + 1 < out_size) {
        out[pos++] = *p++;
    }

    out[pos] = '\0';

    return ESP_OK;
}


/*
 *  解析一行 CSV 记录
 *
 * 格式：
 *      id,uploaded,retry_count,"json"
 */
static esp_err_t parse_record_line(const char *line, csv_record_t *rec)
{
    if (line == NULL || rec == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (is_csv_header(line)) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(rec, 0, sizeof(csv_record_t));

    char *end = NULL;

    rec->id = (int)strtol(line, &end, 10);
    if (end == NULL || *end != ',') {
        return ESP_FAIL;
    }

    const char *p = end + 1;

    rec->uploaded = (int)strtol(p, &end, 10);
    if (end == NULL || *end != ',') {
        return ESP_FAIL;
    }

    p = end + 1;

    rec->retry_count = (int)strtol(p, &end, 10);
    if (end == NULL || *end != ',') {
        return ESP_FAIL;
    }

    p = end + 1;

    return parse_csv_json_field(p, rec->json, sizeof(rec->json));
}


/*
 *  确保 CSV 文件存在
 */
static esp_err_t ensure_csv_file(void)
{
    FILE *fp = fopen(LOCAL_RECORD_CSV_PATH, "r");
    if (fp != NULL) {
        fclose(fp);
        return ESP_OK;
    }

    fp = fopen(LOCAL_RECORD_CSV_PATH, "w");
    if (fp == NULL) {
        ESP_LOGE(TAG,
                 "Create CSV failed: %s, errno=%d",
                 LOCAL_RECORD_CSV_PATH,
                 errno);
        return ESP_FAIL;
    }

    write_csv_header(fp);
    fclose(fp);

    ESP_LOGI(TAG, "CSV file created: %s", LOCAL_RECORD_CSV_PATH);

    return ESP_OK;
}


/*
 *  扫描 CSV，计算下一个 record_id
 */
static int scan_next_record_id(void)
{
    FILE *fp = fopen(LOCAL_RECORD_CSV_PATH, "r");
    if (fp == NULL) {
        return 1;
    }

    char line[CSV_LINE_MAX];
    int max_id = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        csv_record_t rec;

        if (parse_record_line(line, &rec) == ESP_OK) {
            if (rec.id > max_id) {
                max_id = rec.id;
            }
        }
    }

    fclose(fp);

    return max_id + 1;
}


/*
 *  重写 CSV 文件
 *
 * CSV 文件不适合原地修改某一行。
 * 因此修改 uploaded / retry_count 时采用：
 *
 *      原文件 -> 临时文件 -> 删除原文件 -> 重命名临时文件
 */
static esp_err_t rewrite_records(update_mode_t mode, int target_id)
{
    FILE *src = fopen(LOCAL_RECORD_CSV_PATH, "r");
    if (src == NULL) {
        ESP_LOGE(TAG, "Open CSV read failed, errno=%d", errno);
        return ESP_FAIL;
    }

    FILE *tmp = fopen(LOCAL_RECORD_TMP_PATH, "w");
    if (tmp == NULL) {
        ESP_LOGE(TAG, "Open CSV tmp failed, errno=%d", errno);
        fclose(src);
        return ESP_FAIL;
    }

    write_csv_header(tmp);

    char line[CSV_LINE_MAX];
    bool changed = false;

    while (fgets(line, sizeof(line), src) != NULL) {
        if (is_csv_header(line)) {
            continue;
        }

        csv_record_t rec;

        if (parse_record_line(line, &rec) != ESP_OK) {
            ESP_LOGW(TAG, "Skip invalid CSV line");
            continue;
        }

        switch (mode) {
            case UPDATE_MARK_PUBLISHING:
                if (rec.id == target_id) {
                    rec.uploaded = 2;
                    changed = true;
                }
                break;

            case UPDATE_MARK_UPLOADED:
                if (rec.id == target_id) {
                    rec.uploaded = 1;
                    changed = true;
                }
                break;

            case UPDATE_INCREMENT_RETRY:
                if (rec.id == target_id) {
                    rec.retry_count++;
                    rec.uploaded = 0;
                    changed = true;
                }
                break;

            case UPDATE_RESET_PUBLISHING:
                if (rec.uploaded == 2) {
                    rec.uploaded = 0;
                    changed = true;
                }
                break;

            default:
                break;
        }

        write_record_line(tmp, &rec);
    }

    fclose(src);

    if (fclose(tmp) != 0) {
        ESP_LOGE(TAG, "Close tmp CSV failed, errno=%d", errno);
        return ESP_FAIL;
    }

    /*
     * SPIFFS rename 覆盖行为不一定可靠，
     * 所以先删除原文件，再重命名临时文件。
     */
    remove(LOCAL_RECORD_CSV_PATH);

    if (rename(LOCAL_RECORD_TMP_PATH, LOCAL_RECORD_CSV_PATH) != 0) {
        ESP_LOGE(TAG, "Rename tmp CSV failed, errno=%d", errno);
        return ESP_FAIL;
    }

    if (!changed) {
        ESP_LOGW(TAG,
                 "CSV rewrite finished, but no record changed, target=%d",
                 target_id);
    }

    return ESP_OK;
}


/*
 *  初始化本地 CSV 记录模块
 */
esp_err_t local_record_db_init(void)
{
    if (s_file_mutex == NULL) {
        s_file_mutex = xSemaphoreCreateMutex();
        if (s_file_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_ERROR_CHECK(spiffs_init());

    if (xSemaphoreTake(s_file_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ensure_csv_file();
    if (ret == ESP_OK) {
        s_next_record_id = scan_next_record_id();
    }

    xSemaphoreGive(s_file_mutex);

    if (ret != ESP_OK) {
        return ret;
    }

    /*
     * 上次掉电时可能有 uploaded=2 的记录，
     * 启动后恢复成 uploaded=0，等待重新补传。
     */
    ret = local_record_db_reset_publishing();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Reset publishing records failed");
    }

    ESP_LOGI(TAG,
             "CSV local record initialized, path=%s, next_id=%d",
             LOCAL_RECORD_CSV_PATH,
             s_next_record_id);

    return ESP_OK;
}


/*
 *  插入一条事件记录
 */
esp_err_t local_record_db_insert_event(const char *event_type,
                                       const char *user_id,
                                       const char *method,
                                       bool alarm,
                                       const char *time_str,
                                       int64_t uptime_ms)
{
    if (xSemaphoreTake(s_file_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    csv_record_t rec;
    memset(&rec, 0, sizeof(rec));

    rec.id = s_next_record_id++;
    rec.uploaded = 0;
    rec.retry_count = 0;

    snprintf(rec.json,
             sizeof(rec.json),
             "{"
             "\"record_id\":%d,"
             "\"device_id\":\"%s\","
             "\"event_type\":\"%s\","
             "\"user_id\":\"%s\","
             "\"method\":\"%s\","
             "\"alarm\":%s,"
             "\"time\":\"%s\","
             "\"uptime_ms\":%lld"
             "}",
             rec.id,
             DEVICE_ID,
             event_type ? event_type : "unknown",
             user_id ? user_id : "unknown",
             method ? method : "unknown",
             alarm ? "true" : "false",
             time_str ? time_str : "unknown",
             (long long)uptime_ms);

    FILE *fp = fopen(LOCAL_RECORD_CSV_PATH, "a");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Open CSV append failed, errno=%d", errno);
        xSemaphoreGive(s_file_mutex);
        return ESP_FAIL;
    }

    esp_err_t ret = write_record_line(fp, &rec);

    if (fclose(fp) != 0) {
        ESP_LOGE(TAG, "Close CSV append failed, errno=%d", errno);
        ret = ESP_FAIL;
    }

    xSemaphoreGive(s_file_mutex);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "Event saved to CSV, id=%d, json=%s",
                 rec.id,
                 rec.json);
    }

    return ret;
}


/*
 *  获取最早一条未上传记录
 */
esp_err_t local_record_db_get_pending(pending_record_t *record)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(record, 0, sizeof(pending_record_t));

    if (xSemaphoreTake(s_file_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    FILE *fp = fopen(LOCAL_RECORD_CSV_PATH, "r");
    if (fp == NULL) {
        xSemaphoreGive(s_file_mutex);
        return ESP_FAIL;
    }

    char line[CSV_LINE_MAX];

    while (fgets(line, sizeof(line), fp) != NULL) {
        csv_record_t rec;

        if (parse_record_line(line, &rec) != ESP_OK) {
            continue;
        }

        if (rec.uploaded == 0) {
            record->id = rec.id;

            snprintf(record->json,
                     sizeof(record->json),
                     "%s",
                     rec.json);

            fclose(fp);
            xSemaphoreGive(s_file_mutex);

            return ESP_OK;
        }
    }

    fclose(fp);
    xSemaphoreGive(s_file_mutex);

    return ESP_ERR_NOT_FOUND;
}


/*
 *  标记记录正在发布
 */
esp_err_t local_record_db_mark_publishing(int id)
{
    if (xSemaphoreTake(s_file_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = rewrite_records(UPDATE_MARK_PUBLISHING, id);

    xSemaphoreGive(s_file_mutex);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Record marked publishing, id=%d", id);
    }

    return ret;
}


/*
 *  标记记录已上传
 */
esp_err_t local_record_db_mark_uploaded(int id)
{
    if (xSemaphoreTake(s_file_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = rewrite_records(UPDATE_MARK_UPLOADED, id);

    xSemaphoreGive(s_file_mutex);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Record marked uploaded, id=%d", id);
    }

    return ret;
}


/*
 *  恢复 uploaded=2 的记录
 */
esp_err_t local_record_db_reset_publishing(void)
{
    if (xSemaphoreTake(s_file_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = rewrite_records(UPDATE_RESET_PUBLISHING, -1);

    xSemaphoreGive(s_file_mutex);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Publishing records reset to pending");
    }

    return ret;
}


/*
 *  增加重试次数
 */
esp_err_t local_record_db_increment_retry(int id)
{
    if (xSemaphoreTake(s_file_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = rewrite_records(UPDATE_INCREMENT_RETRY, id);

    xSemaphoreGive(s_file_mutex);

    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "Record retry incremented, id=%d", id);
    }

    return ret;
}


/*
 *  统计待上传记录数量
 */
int local_record_db_count_pending(void)
{
    int count = 0;

    if (xSemaphoreTake(s_file_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return 0;
    }

    FILE *fp = fopen(LOCAL_RECORD_CSV_PATH, "r");
    if (fp == NULL) {
        xSemaphoreGive(s_file_mutex);
        return 0;
    }

    char line[CSV_LINE_MAX];

    while (fgets(line, sizeof(line), fp) != NULL) {
        csv_record_t rec;

        if (parse_record_line(line, &rec) == ESP_OK) {
            if (rec.uploaded == 0) {
                count++;
            }
        }
    }

    fclose(fp);
    xSemaphoreGive(s_file_mutex);

    return count;
}