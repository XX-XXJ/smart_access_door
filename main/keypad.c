#include "keypad.h"
#include "app_config.h"

#include "esp_log.h"
#include "driver/i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "KEYPAD_PCF8574";


/*
 *  4×4 矩阵键盘按键映射
 */
static const char s_keymap[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};


/*
 *  PCF8574T 位定义
 *
 * 推荐连接：
 *      P0~P3：矩阵键盘行
 *      P4~P7：矩阵键盘列
 */
static const uint8_t s_row_bits[4] = {
    PCF8574_ROW_0_BIT,
    PCF8574_ROW_1_BIT,
    PCF8574_ROW_2_BIT,
    PCF8574_ROW_3_BIT
};

static const uint8_t s_col_bits[4] = {
    PCF8574_COL_0_BIT,
    PCF8574_COL_1_BIT,
    PCF8574_COL_2_BIT,
    PCF8574_COL_3_BIT
};


/*
 *  说明
 *
 * OLED 和 PCF8574T 共用 I2C0。
 *
 * OLED 的 ui_init() 已经执行：
 *      i2c_param_config()
 *      i2c_driver_install()
 *
 * 所以这里不能再次安装 I2C 驱动。
 * 否则 ESP-IDF 会报：
 *      i2c driver install error
 */
static esp_err_t keypad_i2c_init(void)
{
    return ESP_OK;
}


/*
 *  写 PCF8574T
 *
 * PCF8574T 是准双向 IO：
 *
 *      写 0：输出低电平
 *      写 1：释放引脚，可作为输入
 */
static esp_err_t pcf8574_write(uint8_t value)
{
    return i2c_master_write_to_device(PCF8574_I2C_PORT_NUM,
                                      PCF8574_I2C_ADDR,
                                      &value,
                                      1,
                                      pdMS_TO_TICKS(100));
}


/*
 *  读 PCF8574T
 */
static esp_err_t pcf8574_read(uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_read_from_device(PCF8574_I2C_PORT_NUM,
                                       PCF8574_I2C_ADDR,
                                       value,
                                       1,
                                       pdMS_TO_TICKS(100));
}


/*
 *  初始化键盘
 */
esp_err_t keypad_init(void)
{
    esp_err_t ret = keypad_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /*
     * 检测 PCF8574T 是否响应。
     *
     * 如果这里失败，通常是：
     * 1. PCF8574T 地址不对；
     * 2. SDA/SCL 接反；
     * 3. VCC/GND 问题；
     * 4. 模块跳帽导致地址不是 0x20。
     */
    ret = pcf8574_write(0xFF);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "PCF8574T not found, addr=0x%02X, err=%s",
                 PCF8574_I2C_ADDR,
                 esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG,
             "PCF8574T keypad initialized, addr=0x%02X",
             PCF8574_I2C_ADDR);

    return ESP_OK;
}


/*
 *  扫描矩阵键盘
 */
char keypad_get_key(void)
{
    for (int row = 0; row < 4; row++) {
        uint8_t output = 0xFF;

        /*
         * 当前行拉低。
         */
        output &= ~(1 << s_row_bits[row]);

        esp_err_t ret = pcf8574_write(output);
        if (ret != ESP_OK) {
            return '\0';
        }

        vTaskDelay(pdMS_TO_TICKS(2));

        uint8_t input = 0xFF;

        ret = pcf8574_read(&input);
        if (ret != ESP_OK) {
            return '\0';
        }

        for (int col = 0; col < 4; col++) {
            /*
             * 列线为低电平，说明该键被按下。
             */
            if ((input & (1 << s_col_bits[col])) == 0) {
                vTaskDelay(pdMS_TO_TICKS(20));

                uint8_t input_confirm = 0xFF;

                ret = pcf8574_read(&input_confirm);
                if (ret != ESP_OK) {
                    return '\0';
                }

                if ((input_confirm & (1 << s_col_bits[col])) == 0) {
                    /*
                     * 等待按键松开。
                     */
                    while (1) {
                        uint8_t input_release = 0xFF;

                        ret = pcf8574_read(&input_release);
                        if (ret != ESP_OK) {
                            break;
                        }

                        if ((input_release & (1 << s_col_bits[col])) != 0) {
                            break;
                        }

                        vTaskDelay(pdMS_TO_TICKS(10));
                    }

                    /*
                     * 扫描结束后释放所有 IO。
                     */
                    pcf8574_write(0xFF);

                    return s_keymap[row][col];
                }
            }
        }
    }

    pcf8574_write(0xFF);

    return '\0';
}


/*
 *  Deep Sleep 前准备键盘唤醒
 *
 * 睡眠前：
 *      P0~P3 = 0
 *      P4~P7 = 1
 *
 * 当用户按下任意键：
 *      某个列线被拉低；
 *      PCF8574T INT 输出低电平；
 *      ESP32-S3 被唤醒。
 */
esp_err_t keypad_prepare_sleep_wakeup(void)
{
    ESP_LOGI(TAG, "Prepare PCF8574T keypad wakeup");

    return pcf8574_write(0xF0);
}