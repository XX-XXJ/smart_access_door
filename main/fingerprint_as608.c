#include "fingerprint_as608.h"
#include "app_config.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/uart.h"

static const char *TAG = "AS608";

#define AS608_HEADER_HIGH           0xEF
#define AS608_HEADER_LOW            0x01

#define AS608_ADDR_0                0xFF
#define AS608_ADDR_1                0xFF
#define AS608_ADDR_2                0xFF
#define AS608_ADDR_3                0xFF

#define AS608_PACKET_COMMAND        0x01
#define AS608_PACKET_ACK            0x07

#define AS608_CMD_GET_IMAGE         0x01
#define AS608_CMD_IMAGE_2_TZ        0x02
#define AS608_CMD_SEARCH            0x04
#define AS608_CMD_REG_MODEL         0x05
#define AS608_CMD_STORE             0x06
#define AS608_CMD_DELETE            0x0C
#define AS608_CMD_VERIFY_PASSWORD   0x13

#define AS608_OK                    0x00
#define AS608_NO_FINGER             0x02
#define AS608_NOT_FOUND             0x09

#define AS608_CHAR_BUFFER_1         0x01
#define AS608_CHAR_BUFFER_2         0x02

#define AS608_TEMPLATE_START        0
#define AS608_TEMPLATE_COUNT        200

#define AS608_READ_TIMEOUT_MS       1000
#define AS608_ENROLL_TIMEOUT_MS     15000

static SemaphoreHandle_t s_fp_mutex = NULL;


static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}


static esp_err_t uart_read_exact(uint8_t *buf,
                                 int len,
                                 uint32_t timeout_ms)
{
    if (buf == NULL || len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int received = 0;
    int64_t deadline = now_ms() + timeout_ms;

    while (received < len && now_ms() < deadline) {
        int n = uart_read_bytes(FINGER_UART_PORT,
                                buf + received,
                                len - received,
                                pdMS_TO_TICKS(20));

        if (n > 0) {
            received += n;
        }
    }

    return received == len ? ESP_OK : ESP_ERR_TIMEOUT;
}


static esp_err_t as608_send_command(const uint8_t *payload,
                                    uint16_t payload_len)
{
    if (payload == NULL || payload_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t packet_len = payload_len + 2;
    uint16_t checksum = 0;

    uint8_t packet[64] = {0};

    if (sizeof(packet) < (size_t)(11 + payload_len)) {
        return ESP_ERR_INVALID_SIZE;
    }

    packet[0] = AS608_HEADER_HIGH;
    packet[1] = AS608_HEADER_LOW;

    packet[2] = AS608_ADDR_0;
    packet[3] = AS608_ADDR_1;
    packet[4] = AS608_ADDR_2;
    packet[5] = AS608_ADDR_3;

    packet[6] = AS608_PACKET_COMMAND;
    packet[7] = (uint8_t)(packet_len >> 8);
    packet[8] = (uint8_t)(packet_len & 0xFF);

    memcpy(&packet[9], payload, payload_len);

    checksum = packet[6] + packet[7] + packet[8];

    for (uint16_t i = 0; i < payload_len; i++) {
        checksum += payload[i];
    }

    packet[9 + payload_len] = (uint8_t)(checksum >> 8);
    packet[10 + payload_len] = (uint8_t)(checksum & 0xFF);

    uart_flush_input(FINGER_UART_PORT);

    int written = uart_write_bytes(FINGER_UART_PORT,
                                   (const char *)packet,
                                   11 + payload_len);

    return written == (11 + payload_len) ? ESP_OK : ESP_FAIL;
}


static esp_err_t as608_read_packet(uint8_t *packet_type,
                                   uint8_t *payload,
                                   uint16_t *payload_len,
                                   uint32_t timeout_ms)
{
    if (packet_type == NULL || payload == NULL || payload_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t header[9] = {0};

    esp_err_t ret = uart_read_exact(header, sizeof(header), timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (header[0] != AS608_HEADER_HIGH ||
        header[1] != AS608_HEADER_LOW) {
        return ESP_FAIL;
    }

    *packet_type = header[6];

    uint16_t packet_len = ((uint16_t)header[7] << 8) | header[8];
    if (packet_len < 2) {
        return ESP_FAIL;
    }

    uint16_t data_len = packet_len - 2;
    if (data_len > *payload_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t tail[128] = {0};

    if (packet_len > sizeof(tail)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ret = uart_read_exact(tail, packet_len, timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t recv_checksum =
        ((uint16_t)tail[data_len] << 8) | tail[data_len + 1];

    uint16_t calc_checksum = header[6] + header[7] + header[8];

    for (uint16_t i = 0; i < data_len; i++) {
        calc_checksum += tail[i];
    }

    if (recv_checksum != calc_checksum) {
        return ESP_FAIL;
    }

    memcpy(payload, tail, data_len);
    *payload_len = data_len;

    return ESP_OK;
}


static esp_err_t as608_command_ack(const uint8_t *cmd,
                                   uint16_t cmd_len,
                                   uint8_t *ack,
                                   uint16_t *ack_len,
                                   uint32_t timeout_ms)
{
    esp_err_t ret = as608_send_command(cmd, cmd_len);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t packet_type = 0;

    ret = as608_read_packet(&packet_type, ack, ack_len, timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (packet_type != AS608_PACKET_ACK || *ack_len == 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}


static esp_err_t as608_verify_password(void)
{
    uint8_t cmd[5] = {
        AS608_CMD_VERIFY_PASSWORD,
        (uint8_t)(FINGER_PASSWORD >> 24),
        (uint8_t)(FINGER_PASSWORD >> 16),
        (uint8_t)(FINGER_PASSWORD >> 8),
        (uint8_t)(FINGER_PASSWORD)
    };

    uint8_t ack[16] = {0};
    uint16_t ack_len = sizeof(ack);

    esp_err_t ret = as608_command_ack(cmd,
                                      sizeof(cmd),
                                      ack,
                                      &ack_len,
                                      AS608_READ_TIMEOUT_MS);

    if (ret != ESP_OK) {
        return ret;
    }

    return ack[0] == AS608_OK ? ESP_OK : ESP_FAIL;
}


static esp_err_t as608_get_image(void)
{
    uint8_t cmd[1] = {AS608_CMD_GET_IMAGE};
    uint8_t ack[16] = {0};
    uint16_t ack_len = sizeof(ack);

    esp_err_t ret = as608_command_ack(cmd,
                                      sizeof(cmd),
                                      ack,
                                      &ack_len,
                                      AS608_READ_TIMEOUT_MS);

    if (ret != ESP_OK) {
        return ret;
    }

    if (ack[0] == AS608_OK) {
        return ESP_OK;
    }

    if (ack[0] == AS608_NO_FINGER) {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_FAIL;
}


static esp_err_t as608_image_to_tz(uint8_t buffer_id)
{
    uint8_t cmd[2] = {
        AS608_CMD_IMAGE_2_TZ,
        buffer_id
    };

    uint8_t ack[16] = {0};
    uint16_t ack_len = sizeof(ack);

    esp_err_t ret = as608_command_ack(cmd,
                                      sizeof(cmd),
                                      ack,
                                      &ack_len,
                                      AS608_READ_TIMEOUT_MS);

    if (ret != ESP_OK) {
        return ret;
    }

    return ack[0] == AS608_OK ? ESP_OK : ESP_FAIL;
}


static esp_err_t as608_search(uint8_t buffer_id,
                              uint16_t *finger_id,
                              uint16_t *confidence)
{
    uint8_t cmd[6] = {
        AS608_CMD_SEARCH,
        buffer_id,
        (uint8_t)(AS608_TEMPLATE_START >> 8),
        (uint8_t)(AS608_TEMPLATE_START),
        (uint8_t)(AS608_TEMPLATE_COUNT >> 8),
        (uint8_t)(AS608_TEMPLATE_COUNT)
    };

    uint8_t ack[16] = {0};
    uint16_t ack_len = sizeof(ack);

    esp_err_t ret = as608_command_ack(cmd,
                                      sizeof(cmd),
                                      ack,
                                      &ack_len,
                                      AS608_READ_TIMEOUT_MS);

    if (ret != ESP_OK) {
        return ret;
    }

    if (ack[0] == AS608_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }

    if (ack[0] != AS608_OK || ack_len < 5) {
        return ESP_FAIL;
    }

    if (finger_id) {
        *finger_id = ((uint16_t)ack[1] << 8) | ack[2];
    }

    if (confidence) {
        *confidence = ((uint16_t)ack[3] << 8) | ack[4];
    }

    return ESP_OK;
}


static esp_err_t as608_create_model(void)
{
    uint8_t cmd[1] = {AS608_CMD_REG_MODEL};
    uint8_t ack[16] = {0};
    uint16_t ack_len = sizeof(ack);

    esp_err_t ret = as608_command_ack(cmd,
                                      sizeof(cmd),
                                      ack,
                                      &ack_len,
                                      AS608_READ_TIMEOUT_MS);

    if (ret != ESP_OK) {
        return ret;
    }

    return ack[0] == AS608_OK ? ESP_OK : ESP_FAIL;
}


static esp_err_t as608_store_model(uint8_t buffer_id,
                                   uint16_t finger_id)
{
    uint8_t cmd[4] = {
        AS608_CMD_STORE,
        buffer_id,
        (uint8_t)(finger_id >> 8),
        (uint8_t)(finger_id)
    };

    uint8_t ack[16] = {0};
    uint16_t ack_len = sizeof(ack);

    esp_err_t ret = as608_command_ack(cmd,
                                      sizeof(cmd),
                                      ack,
                                      &ack_len,
                                      AS608_READ_TIMEOUT_MS);

    if (ret != ESP_OK) {
        return ret;
    }

    return ack[0] == AS608_OK ? ESP_OK : ESP_FAIL;
}


static esp_err_t capture_to_buffer(uint8_t buffer_id,
                                   uint32_t timeout_ms)
{
    int64_t deadline = now_ms() + timeout_ms;

    while (now_ms() < deadline) {
        esp_err_t ret = as608_get_image();

        if (ret == ESP_OK) {
            return as608_image_to_tz(buffer_id);
        }

        if (ret != ESP_ERR_NOT_FOUND) {
            return ret;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    return ESP_ERR_TIMEOUT;
}


static esp_err_t wait_no_finger(uint32_t timeout_ms)
{
    int64_t deadline = now_ms() + timeout_ms;

    while (now_ms() < deadline) {
        esp_err_t ret = as608_get_image();

        if (ret == ESP_ERR_NOT_FOUND) {
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    return ESP_ERR_TIMEOUT;
}


esp_err_t fingerprint_as608_init(void)
{
    if (s_fp_mutex == NULL) {
        s_fp_mutex = xSemaphoreCreateMutex();
        if (s_fp_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    uart_config_t uart_config = {
        .baud_rate = FINGER_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(FINGER_UART_PORT,
                                        FINGER_UART_BUF_SIZE,
                                        0,
                                        0,
                                        NULL,
                                        0));

    ESP_ERROR_CHECK(uart_param_config(FINGER_UART_PORT, &uart_config));

    ESP_ERROR_CHECK(uart_set_pin(FINGER_UART_PORT,
                                 FINGER_TX_GPIO,
                                 FINGER_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t ret = as608_verify_password();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AS608 verify failed");
        return ret;
    }

    ESP_LOGI(TAG, "AS608 initialized");

    return ESP_OK;
}


esp_err_t fingerprint_as608_search(uint16_t *finger_id,
                                   uint16_t *confidence)
{
    if (s_fp_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_fp_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = as608_get_image();
    if (ret != ESP_OK) {
        xSemaphoreGive(s_fp_mutex);
        return ret;
    }

    ret = as608_image_to_tz(AS608_CHAR_BUFFER_1);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_fp_mutex);
        return ret;
    }

    ret = as608_search(AS608_CHAR_BUFFER_1,
                       finger_id,
                       confidence);

    xSemaphoreGive(s_fp_mutex);

    return ret;
}


esp_err_t fingerprint_as608_enroll(uint16_t finger_id)
{
    if (s_fp_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_fp_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Enroll start, id=%u", finger_id);

    esp_err_t ret = capture_to_buffer(AS608_CHAR_BUFFER_1,
                                      AS608_ENROLL_TIMEOUT_MS);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_fp_mutex);
        return ret;
    }

    ESP_LOGI(TAG, "Remove finger");

    ret = wait_no_finger(AS608_ENROLL_TIMEOUT_MS);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_fp_mutex);
        return ret;
    }

    ESP_LOGI(TAG, "Place same finger again");

    ret = capture_to_buffer(AS608_CHAR_BUFFER_2,
                            AS608_ENROLL_TIMEOUT_MS);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_fp_mutex);
        return ret;
    }

    ret = as608_create_model();
    if (ret != ESP_OK) {
        xSemaphoreGive(s_fp_mutex);
        return ret;
    }

    ret = as608_store_model(AS608_CHAR_BUFFER_1, finger_id);

    xSemaphoreGive(s_fp_mutex);

    return ret;
}


esp_err_t fingerprint_as608_delete(uint16_t finger_id)
{
    if (s_fp_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_fp_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t cmd[5] = {
        AS608_CMD_DELETE,
        (uint8_t)(finger_id >> 8),
        (uint8_t)(finger_id),
        0x00,
        0x01
    };

    uint8_t ack[16] = {0};
    uint16_t ack_len = sizeof(ack);

    esp_err_t ret = as608_command_ack(cmd,
                                      sizeof(cmd),
                                      ack,
                                      &ack_len,
                                      AS608_READ_TIMEOUT_MS);

    if (ret == ESP_OK && ack[0] == AS608_OK) {
        ret = ESP_OK;
    } else {
        ret = ESP_FAIL;
    }

    xSemaphoreGive(s_fp_mutex);

    return ret;
}