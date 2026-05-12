#include "mqtt_client_app.h"
#include "app_config.h"
#include "local_record_db.h"

#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_APP";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;


/*
 *  MQTT 发布记录映射表
 *
 * esp_mqtt_client_publish() 返回的是 msg_id。
 * SQLite 中的记录使用 record_id。
 *
 * 为了在 MQTT_EVENT_PUBLISHED 回调中知道是哪条 SQLite 记录发布成功，
 * 这里用一个小表保存：
 *
 *      msg_id → record_id
 *
 * 本科毕设场景消息量较小，使用固定数组即可。
 */
#define MQTT_PENDING_MAP_SIZE       10

typedef struct {
    bool used;
    int msg_id;
    int record_id;
} mqtt_pending_map_t;

static mqtt_pending_map_t s_pending_map[MQTT_PENDING_MAP_SIZE];


/*
 * 保存 msg_id 和 record_id 的映射关系。
 */
static void pending_map_add(int msg_id, int record_id)
{
    for (int i = 0; i < MQTT_PENDING_MAP_SIZE; i++) {
        if (!s_pending_map[i].used) {
            s_pending_map[i].used = true;
            s_pending_map[i].msg_id = msg_id;
            s_pending_map[i].record_id = record_id;

            ESP_LOGI(TAG,
                     "Pending map add: msg_id=%d, record_id=%d",
                     msg_id,
                     record_id);

            return;
        }
    }

    ESP_LOGW(TAG, "Pending map full, record_id=%d may not be confirmed",
             record_id);
}


/*
 * 根据 msg_id 查找 record_id，并移除映射。
 */
static int pending_map_take_record_id(int msg_id)
{
    for (int i = 0; i < MQTT_PENDING_MAP_SIZE; i++) {
        if (s_pending_map[i].used &&
            s_pending_map[i].msg_id == msg_id) {
            int record_id = s_pending_map[i].record_id;

            memset(&s_pending_map[i], 0, sizeof(s_pending_map[i]));

            return record_id;
        }
    }

    return -1;
}


/*
 * 清空映射表。
 *
 * 当 MQTT 断开时，之前正在发布的消息可能无法确认，
 * 此时数据库里 uploaded=2 的记录会在下次启动或重连后恢复为 uploaded=0。
 */
static void pending_map_clear(void)
{
    memset(s_pending_map, 0, sizeof(s_pending_map));
}


/*
 *  MQTT 事件处理函数
 */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "MQTT connected");

            /*
             * 发布在线状态。
             */
            mqtt_client_app_publish_status("online");

            /*
             * 订阅云平台命令 Topic。
             * 后续可以扩展：
             * 1. 远程开锁；
             * 2. 用户同步；
             * 3. 参数配置；
             * 4. 远程重启。
             */
            if (s_client != NULL) {
                esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_CMD, MQTT_QOS);
                ESP_LOGI(TAG, "Subscribed topic: %s", MQTT_TOPIC_CMD);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            pending_map_clear();

            /*
             * MQTT 断开时，把数据库中“发布中”的记录恢复为“待上传”。
             * 这样网络恢复后可以继续补传。
             */
            local_record_db_reset_publishing();

            ESP_LOGW(TAG, "MQTT disconnected");
            break;

        case MQTT_EVENT_PUBLISHED: {
            /*
             * QoS1 消息发布完成后会收到该事件。
             * 此时可以安全地把 SQLite 记录标记为 uploaded=1。
             */
            int record_id = pending_map_take_record_id(event->msg_id);

            ESP_LOGI(TAG,
                     "MQTT published, msg_id=%d, record_id=%d",
                     event->msg_id,
                     record_id);

            if (record_id > 0) {
                local_record_db_mark_uploaded(record_id);
            }

            break;
        }

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG,
                     "MQTT data received, topic=%.*s, data=%.*s",
                     event->topic_len,
                     event->topic,
                     event->data_len,
                     event->data);

            /*
             * 这里暂时只打印云端命令。
             * 如果后续需要后台用户管理，可以解析 event->data。
             */
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT error");

            /*
             * 出现错误时，恢复发布中的记录，等待下次补传。
             */
            local_record_db_reset_publishing();
            break;

        default:
            break;
    }
}


/*
 *  初始化 MQTT 客户端
 */
esp_err_t mqtt_client_app_init(void)
{
    pending_map_clear();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(
        s_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register mqtt event failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "MQTT client initialized, broker=%s", MQTT_BROKER_URI);

    return ESP_OK;
}


/*
 *  启动 MQTT
 */
esp_err_t mqtt_client_app_start(void)
{
    if (s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_mqtt_client_start(s_client);
}


/*
 *  停止 MQTT
 */
esp_err_t mqtt_client_app_stop(void)
{
    if (s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_connected = false;

    return esp_mqtt_client_stop(s_client);
}


bool mqtt_client_app_is_connected(void)
{
    return s_connected;
}


/*
 *  发布门禁事件
 *
 * 注意：
 * 这里 publish 成功只表示消息进入 MQTT 发送流程。
 * 真正确认成功需要等待 MQTT_EVENT_PUBLISHED。
 */
esp_err_t mqtt_client_app_publish_event(int record_id, const char *json)
{
    if (s_client == NULL || json == NULL || strlen(json) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(
        s_client,
        MQTT_TOPIC_EVENT,
        json,
        0,
        MQTT_QOS,
        MQTT_RETAIN
    );

    if (msg_id < 0) {
        ESP_LOGW(TAG, "MQTT publish event failed, record_id=%d", record_id);
        return ESP_FAIL;
    }

    /*
     * 保存 msg_id 和 record_id 的映射关系。
     * 后续 MQTT_EVENT_PUBLISHED 触发时，才能知道要更新哪条数据库记录。
     */
    pending_map_add(msg_id, record_id);

    ESP_LOGI(TAG,
             "MQTT event publish queued, msg_id=%d, record_id=%d",
             msg_id,
             record_id);

    return ESP_OK;
}


/*
 *  发布设备状态
 */
esp_err_t mqtt_client_app_publish_status(const char *status)
{
    if (s_client == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    char json[128] = {0};

    snprintf(json,
             sizeof(json),
             "{\"device_id\":\"%s\",\"status\":\"%s\"}",
             DEVICE_ID,
             status);

    int msg_id = esp_mqtt_client_publish(
        s_client,
        MQTT_TOPIC_STATUS,
        json,
        0,
        MQTT_QOS,
        MQTT_RETAIN
    );

    if (msg_id < 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MQTT status publish queued, msg_id=%d", msg_id);

    return ESP_OK;
}