#include "camera_ov2640.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_idf_version.h"

#include "esp_camera.h"

static const char *TAG = "CAMERA";

static bool s_camera_ready = false;


esp_err_t camera_ov2640_init(void)
{
    /*
    * 摄像头硬复位。
    *
    * 如果 RST 接到了 CAMERA_RESET_GPIO，
    * 这里先主动拉低再拉高，确保 OV2640 从确定状态启动。
    */
    #if CAMERA_RESET_GPIO != GPIO_NUM_NC
    gpio_config_t rst_conf = {
        .pin_bit_mask = 1ULL << CAMERA_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&rst_conf);

    gpio_set_level(CAMERA_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(30));

    gpio_set_level(CAMERA_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    #endif

    /*
    * 如果 PWDN 使用 GPIO 控制，则确保摄像头不处于掉电状态。
    * 当前你如果把 PWDN 直接接 GND，这里不会执行。
    */
    #if CAMERA_PWDN_GPIO != GPIO_NUM_NC
    gpio_config_t pwdn_conf = {
        .pin_bit_mask = 1ULL << CAMERA_PWDN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&pwdn_conf);

    gpio_set_level(CAMERA_PWDN_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    #endif
    camera_config_t config = {
        .ledc_channel = LEDC_CHANNEL_1,
        .ledc_timer = LEDC_TIMER_1,

        .pin_d0 = CAMERA_D0_GPIO,
        .pin_d1 = CAMERA_D1_GPIO,
        .pin_d2 = CAMERA_D2_GPIO,
        .pin_d3 = CAMERA_D3_GPIO,
        .pin_d4 = CAMERA_D4_GPIO,
        .pin_d5 = CAMERA_D5_GPIO,
        .pin_d6 = CAMERA_D6_GPIO,
        .pin_d7 = CAMERA_D7_GPIO,

        .pin_xclk = CAMERA_XCLK_GPIO,
        .pin_pclk = CAMERA_PCLK_GPIO,
        .pin_vsync = CAMERA_VSYNC_GPIO,
        .pin_href = CAMERA_HREF_GPIO,

        .pin_sccb_sda = CAMERA_SIOD_GPIO,
        .pin_sccb_scl = CAMERA_SIOC_GPIO,

        .pin_pwdn = CAMERA_PWDN_GPIO,
        .pin_reset = CAMERA_RESET_GPIO,

        .xclk_freq_hz = CAMERA_XCLK_FREQ_HZ,

        /*
         * 人脸识别阶段推荐灰度 + 低分辨率。
         */
        .pixel_format = PIXFORMAT_GRAYSCALE,
        .frame_size = FRAMESIZE_QQVGA,

        .jpeg_quality = 12,
        .fb_count = 1,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        /*
         * 关键点：
         * 摄像头 SCCB 使用 I2C1。
         *
         * OLED + PCF8574T 已经使用 I2C0。
         * 如果摄像头也用 I2C0，会和 OLED/PCF8574T 的旧版 I2C 驱动端口冲突。
         */
        .sccb_i2c_port = CAMERA_SCCB_I2C_PORT_NUM,
#endif
    };

    esp_err_t ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(ret));
        s_camera_ready = false;
        return ret;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        sensor->set_vflip(sensor, 0);
        sensor->set_hmirror(sensor, 0);
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 0);
    }

    s_camera_ready = true;

    ESP_LOGI(TAG, "Camera initialized");

    return ESP_OK;
}


esp_err_t camera_ov2640_capture_test(void)
{
    if (!s_camera_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    camera_fb_t *fb = esp_camera_fb_get();

    if (fb == NULL) {
        ESP_LOGE(TAG, "Camera frame get failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "Capture OK: len=%u, width=%u, height=%u, format=%d",
             (unsigned int)fb->len,
             (unsigned int)fb->width,
             (unsigned int)fb->height,
             fb->format);

    esp_camera_fb_return(fb);

    return ESP_OK;
}