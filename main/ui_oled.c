#include "ui_oled.h"
#include "app_config.h"

#include <string.h>
#include <ctype.h>

#include "esp_log.h"
#include "driver/i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OLED";

static bool s_oled_ready = false;


/*
 *  5x7 简易字体表
 *
 * 为了减少 OLED 驱动依赖，这里使用简单英文字符点阵。
 */

static const uint8_t FONT_SPACE[5] = {0x00,0x00,0x00,0x00,0x00};
static const uint8_t FONT_0[5] = {0x3E,0x51,0x49,0x45,0x3E};
static const uint8_t FONT_1[5] = {0x00,0x42,0x7F,0x40,0x00};
static const uint8_t FONT_2[5] = {0x42,0x61,0x51,0x49,0x46};
static const uint8_t FONT_3[5] = {0x21,0x41,0x45,0x4B,0x31};
static const uint8_t FONT_4[5] = {0x18,0x14,0x12,0x7F,0x10};
static const uint8_t FONT_5[5] = {0x27,0x45,0x45,0x45,0x39};
static const uint8_t FONT_6[5] = {0x3C,0x4A,0x49,0x49,0x30};
static const uint8_t FONT_7[5] = {0x01,0x71,0x09,0x05,0x03};
static const uint8_t FONT_8[5] = {0x36,0x49,0x49,0x49,0x36};
static const uint8_t FONT_9[5] = {0x06,0x49,0x49,0x29,0x1E};

static const uint8_t FONT_A[5] = {0x7E,0x11,0x11,0x11,0x7E};
static const uint8_t FONT_B[5] = {0x7F,0x49,0x49,0x49,0x36};
static const uint8_t FONT_C[5] = {0x3E,0x41,0x41,0x41,0x22};
static const uint8_t FONT_D[5] = {0x7F,0x41,0x41,0x22,0x1C};
static const uint8_t FONT_E[5] = {0x7F,0x49,0x49,0x49,0x41};
static const uint8_t FONT_F[5] = {0x7F,0x09,0x09,0x09,0x01};
static const uint8_t FONT_G[5] = {0x3E,0x41,0x49,0x49,0x7A};
static const uint8_t FONT_H[5] = {0x7F,0x08,0x08,0x08,0x7F};
static const uint8_t FONT_I[5] = {0x00,0x41,0x7F,0x41,0x00};
static const uint8_t FONT_J[5] = {0x20,0x40,0x41,0x3F,0x01};
static const uint8_t FONT_K[5] = {0x7F,0x08,0x14,0x22,0x41};
static const uint8_t FONT_L[5] = {0x7F,0x40,0x40,0x40,0x40};
static const uint8_t FONT_M[5] = {0x7F,0x02,0x0C,0x02,0x7F};
static const uint8_t FONT_N[5] = {0x7F,0x04,0x08,0x10,0x7F};
static const uint8_t FONT_O[5] = {0x3E,0x41,0x41,0x41,0x3E};
static const uint8_t FONT_P[5] = {0x7F,0x09,0x09,0x09,0x06};
static const uint8_t FONT_Q[5] = {0x3E,0x41,0x51,0x21,0x5E};
static const uint8_t FONT_R[5] = {0x7F,0x09,0x19,0x29,0x46};
static const uint8_t FONT_S[5] = {0x46,0x49,0x49,0x49,0x31};
static const uint8_t FONT_T[5] = {0x01,0x01,0x7F,0x01,0x01};
static const uint8_t FONT_U[5] = {0x3F,0x40,0x40,0x40,0x3F};
static const uint8_t FONT_V[5] = {0x1F,0x20,0x40,0x20,0x1F};
static const uint8_t FONT_W[5] = {0x7F,0x20,0x18,0x20,0x7F};
static const uint8_t FONT_X[5] = {0x63,0x14,0x08,0x14,0x63};
static const uint8_t FONT_Y[5] = {0x07,0x08,0x70,0x08,0x07};
static const uint8_t FONT_Z[5] = {0x61,0x51,0x49,0x45,0x43};

static const uint8_t FONT_COLON[5] = {0x00,0x36,0x36,0x00,0x00};
static const uint8_t FONT_DASH[5] = {0x08,0x08,0x08,0x08,0x08};
static const uint8_t FONT_STAR[5] = {0x14,0x08,0x3E,0x08,0x14};
static const uint8_t FONT_HASH[5] = {0x14,0x7F,0x14,0x7F,0x14};
static const uint8_t FONT_SLASH[5] = {0x20,0x10,0x08,0x04,0x02};
static const uint8_t FONT_UNDERSCORE[5] = {0x40,0x40,0x40,0x40,0x40};


static const uint8_t *get_font(char ch)
{
    ch = (char)toupper((unsigned char)ch);

    switch (ch) {
        case ' ': return FONT_SPACE;
        case '0': return FONT_0;
        case '1': return FONT_1;
        case '2': return FONT_2;
        case '3': return FONT_3;
        case '4': return FONT_4;
        case '5': return FONT_5;
        case '6': return FONT_6;
        case '7': return FONT_7;
        case '8': return FONT_8;
        case '9': return FONT_9;

        case 'A': return FONT_A;
        case 'B': return FONT_B;
        case 'C': return FONT_C;
        case 'D': return FONT_D;
        case 'E': return FONT_E;
        case 'F': return FONT_F;
        case 'G': return FONT_G;
        case 'H': return FONT_H;
        case 'I': return FONT_I;
        case 'J': return FONT_J;
        case 'K': return FONT_K;
        case 'L': return FONT_L;
        case 'M': return FONT_M;
        case 'N': return FONT_N;
        case 'O': return FONT_O;
        case 'P': return FONT_P;
        case 'Q': return FONT_Q;
        case 'R': return FONT_R;
        case 'S': return FONT_S;
        case 'T': return FONT_T;
        case 'U': return FONT_U;
        case 'V': return FONT_V;
        case 'W': return FONT_W;
        case 'X': return FONT_X;
        case 'Y': return FONT_Y;
        case 'Z': return FONT_Z;

        case ':': return FONT_COLON;
        case '-': return FONT_DASH;
        case '*': return FONT_STAR;
        case '#': return FONT_HASH;
        case '/': return FONT_SLASH;
        case '_': return FONT_UNDERSCORE;

        default: return FONT_SPACE;
    }
}


/*
 *  OLED I2C 写命令
 */

static esp_err_t oled_write_command(uint8_t cmd)
{
    uint8_t data[2] = {
        0x00,
        cmd
    };

    return i2c_master_write_to_device(OLED_I2C_PORT_NUM,
                                      OLED_I2C_ADDR,
                                      data,
                                      sizeof(data),
                                      pdMS_TO_TICKS(100));
}


/*
 *  OLED I2C 写数据
 */

static esp_err_t oled_write_data(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > 128) {
        len = 128;
    }

    uint8_t buffer[129] = {0};

    buffer[0] = 0x40;
    memcpy(&buffer[1], data, len);

    return i2c_master_write_to_device(OLED_I2C_PORT_NUM,
                                      OLED_I2C_ADDR,
                                      buffer,
                                      len + 1,
                                      pdMS_TO_TICKS(100));
}


static void oled_set_pos(uint8_t page, uint8_t col)
{
    oled_write_command(0xB0 | page);
    oled_write_command(0x00 | (col & 0x0F));
    oled_write_command(0x10 | ((col >> 4) & 0x0F));
}


/*
 *  OLED 初始化
 */

esp_err_t ui_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(OLED_I2C_PORT_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(OLED_I2C_PORT_NUM,
                             I2C_MODE_MASTER,
                             0,
                             0,
                             0);

    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    oled_write_command(0xAE);
    oled_write_command(0x20);
    oled_write_command(0x10);
    oled_write_command(0xB0);
    oled_write_command(0xC8);
    oled_write_command(0x00);
    oled_write_command(0x10);
    oled_write_command(0x40);
    oled_write_command(0x81);
    oled_write_command(0x7F);
    oled_write_command(0xA1);
    oled_write_command(0xA6);
    oled_write_command(0xA8);
    oled_write_command(0x3F);
    oled_write_command(0xA4);
    oled_write_command(0xD3);
    oled_write_command(0x00);
    oled_write_command(0xD5);
    oled_write_command(0x80);
    oled_write_command(0xD9);
    oled_write_command(0xF1);
    oled_write_command(0xDA);
    oled_write_command(0x12);
    oled_write_command(0xDB);
    oled_write_command(0x40);
    oled_write_command(0x8D);
    oled_write_command(0x14);
    oled_write_command(0xAF);

    s_oled_ready = true;

    ui_clear();

    ESP_LOGI(TAG, "OLED initialized");

    return ESP_OK;
}


void ui_clear(void)
{
    if (!s_oled_ready) {
        return;
    }

    uint8_t zero[128] = {0};

    for (uint8_t page = 0; page < 8; page++) {
        oled_set_pos(page, 0);
        oled_write_data(zero, sizeof(zero));
    }
}


static void oled_draw_text(uint8_t page, const char *text)
{
    if (text == NULL) {
        return;
    }

    oled_set_pos(page, 0);

    for (int i = 0; text[i] != '\0' && i < 21; i++) {
        const uint8_t *font = get_font(text[i]);

        uint8_t data[6] = {
            font[0],
            font[1],
            font[2],
            font[3],
            font[4],
            0x00
        };

        oled_write_data(data, sizeof(data));
    }
}


void ui_show(const char *line1, const char *line2)
{
    if (!s_oled_ready) {
        return;
    }

    ui_clear();

    oled_draw_text(1, line1 ? line1 : "");
    oled_draw_text(4, line2 ? line2 : "");
}