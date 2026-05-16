#include "rfid_mfrc522.h"
#include "app_config.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

static const char *TAG = "RFID";

// 使用 SPI2 作为 MFRC522 的通信总线
#define RFID_SPI_HOST               SPI2_HOST

/* ---------- MFRC522 寄存器地址 ---------- */
#define CommandReg                  0x01
#define ComIEnReg                   0x02
#define ComIrqReg                   0x04
#define ErrorReg                    0x06
#define FIFODataReg                 0x09
#define FIFOLevelReg                0x0A
#define ControlReg                  0x0C
#define BitFramingReg               0x0D
#define ModeReg                     0x11
#define TxControlReg                0x14
#define TxASKReg                    0x15
#define TModeReg                    0x2A
#define TPrescalerReg               0x2B
#define TReloadRegH                 0x2C
#define TReloadRegL                 0x2D
#define VersionReg                  0x37

/* ---------- 命令码 ---------- */
#define PCD_IDLE                    0x00
#define PCD_TRANSCEIVE              0x0C
#define PCD_SOFTRESET               0x0F

/* ---------- ISO 14443 命令 ---------- */
#define PICC_REQIDL                 0x26
#define PICC_ANTICOLL_CL1           0x93

// SPI 设备句柄
static spi_device_handle_t s_spi = NULL;

/**
 * @brief 向 MFRC522 写入一个寄存器
 * @param reg   寄存器地址
 * @param value 写入值
 */
static esp_err_t mfrc522_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx_data[2] = {
        (uint8_t)((reg << 1) & 0x7E),
        value
    };

    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = tx_data,
    };

    return spi_device_transmit(s_spi, &trans);
}

/**
 * @brief 从 MFRC522 读取一个寄存器
 * @param reg   寄存器地址
 * @param value 输出值（读取结果）
 */
static esp_err_t mfrc522_read_reg(uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_data[2] = {
        (uint8_t)(((reg << 1) & 0x7E) | 0x80),
        0x00
    };

    uint8_t rx_data[2] = {0};

    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };

    esp_err_t ret = spi_device_transmit(s_spi, &trans);
    if (ret != ESP_OK) {
        return ret;
    }

    *value = rx_data[1];
    return ESP_OK;
}

/**
 * @brief 对寄存器位置 1（set bit mask）
 */
static esp_err_t mfrc522_set_bit_mask(uint8_t reg, uint8_t mask)
{
    uint8_t value;

    esp_err_t ret = mfrc522_read_reg(reg, &value);
    if (ret != ESP_OK) {
        return ret;
    }

    return mfrc522_write_reg(reg, value | mask);
}

/**
 * @brief 对寄存器位清零（clear bit mask）
 */
static esp_err_t mfrc522_clear_bit_mask(uint8_t reg, uint8_t mask)
{
    uint8_t value;

    esp_err_t ret = mfrc522_read_reg(reg, &value);
    if (ret != ESP_OK) {
        return ret;
    }

    return mfrc522_write_reg(reg, value & (~mask));
}

/**
 * @brief 开启天线
 */
static esp_err_t mfrc522_antenna_on(void)
{
    uint8_t value;

    esp_err_t ret = mfrc522_read_reg(TxControlReg, &value);
    if (ret != ESP_OK) {
        return ret;
    }

    if ((value & 0x03) != 0x03) {
        ret = mfrc522_set_bit_mask(TxControlReg, 0x03);
    }

    return ret;
}

/**
 * @brief 硬件复位 MFRC522（拉低 RST 引脚）
 */
static esp_err_t mfrc522_reset(void)
{
    gpio_set_level(RFID_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    gpio_set_level(RFID_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    return mfrc522_write_reg(CommandReg, PCD_SOFTRESET);
}

/**
 * @brief 核心通信函数：发送数据并接收应答
 *
 * @param command      PCD 命令（如 PCD_TRANSCEIVE）
 * @param send_data    发送数据缓冲区
 * @param send_len     发送长度
 * @param back_data    接收缓冲区
 * @param back_len     输入：缓冲区大小；输出：实际接收长度
 * @return esp_err_t
 *
 * @warning 此函数包含忙等待轮询，在无卡时可能阻塞 CPU 较长时间
 */
static esp_err_t mfrc522_to_card(uint8_t command,
                                 const uint8_t *send_data,
                                 uint8_t send_len,
                                 uint8_t *back_data,
                                 uint8_t *back_len)
{
    if (send_data == NULL || back_data == NULL || back_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t irq_en = 0x77;
    uint8_t wait_irq = 0x30;
    uint8_t irq_value = 0;
    uint8_t error_value = 0;

    mfrc522_write_reg(ComIEnReg, irq_en | 0x80);
    mfrc522_clear_bit_mask(ComIrqReg, 0x80);
    mfrc522_set_bit_mask(FIFOLevelReg, 0x80);
    mfrc522_write_reg(CommandReg, PCD_IDLE);

    for (uint8_t i = 0; i < send_len; i++) {
        mfrc522_write_reg(FIFODataReg, send_data[i]);
    }

    mfrc522_write_reg(CommandReg, command);
    mfrc522_set_bit_mask(BitFramingReg, 0x80);

    int timeout = 2000;

    do {
        mfrc522_read_reg(ComIrqReg, &irq_value);
        timeout--;
    } while (timeout > 0 &&
             !(irq_value & 0x01) &&
             !(irq_value & wait_irq));

    mfrc522_clear_bit_mask(BitFramingReg, 0x80);

    if (timeout <= 0) {
        return ESP_ERR_TIMEOUT;
    }

    mfrc522_read_reg(ErrorReg, &error_value);
    if (error_value & 0x1B) {
        return ESP_FAIL;
    }

    uint8_t fifo_level = 0;
    mfrc522_read_reg(FIFOLevelReg, &fifo_level);

    if (fifo_level == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (fifo_level > *back_len) {
        fifo_level = *back_len;
    }

    for (uint8_t i = 0; i < fifo_level; i++) {
        mfrc522_read_reg(FIFODataReg, &back_data[i]);
    }

    *back_len = fifo_level;

    return ESP_OK;
}

/**
 * @brief 寻卡（发送 REQA）
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 无卡
 */
static esp_err_t mfrc522_request(void)
{
    uint8_t req_cmd = PICC_REQIDL;
    uint8_t back_data[16] = {0};
    uint8_t back_len = sizeof(back_data);

    mfrc522_write_reg(BitFramingReg, 0x07);

    esp_err_t ret = mfrc522_to_card(PCD_TRANSCEIVE,
                                    &req_cmd,
                                    1,
                                    back_data,
                                    &back_len);

    if (ret != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

/**
 * @brief 防冲撞，获取卡片 UID
 * @param uid     输出 UID（4 字节）
 * @param uid_len 输出长度（4）
 */
static esp_err_t mfrc522_anticoll(uint8_t *uid, uint8_t *uid_len)
{
    if (uid == NULL || uid_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t send_data[2] = {
        PICC_ANTICOLL_CL1,
        0x20
    };

    uint8_t back_data[10] = {0};
    uint8_t back_len = sizeof(back_data);

    mfrc522_write_reg(BitFramingReg, 0x00);

    esp_err_t ret = mfrc522_to_card(PCD_TRANSCEIVE,
                                    send_data,
                                    sizeof(send_data),
                                    back_data,
                                    &back_len);

    if (ret != ESP_OK) {
        return ret;
    }

    if (back_len < 5) {
        return ESP_FAIL;
    }

    uint8_t bcc = back_data[0] ^
                  back_data[1] ^
                  back_data[2] ^
                  back_data[3];

    if (bcc != back_data[4]) {
        return ESP_FAIL;
    }

    memcpy(uid, back_data, 4);
    *uid_len = 4;

    return ESP_OK;
}


esp_err_t rfid_mfrc522_init(void)
{
    gpio_config_t rst_conf = {
        .pin_bit_mask = 1ULL << RFID_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&rst_conf));
    gpio_set_level(RFID_RST_GPIO, 1);

    spi_bus_config_t bus_cfg = {
        .miso_io_num = RFID_MISO_GPIO,
        .mosi_io_num = RFID_MOSI_GPIO,
        .sclk_io_num = RFID_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };

    esp_err_t ret = spi_bus_initialize(RFID_SPI_HOST,
                                       &bus_cfg,
                                       SPI_DMA_CH_AUTO);

    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = RFID_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = RFID_CS_GPIO,
        .queue_size = 1,
    };

    ret = spi_bus_add_device(RFID_SPI_HOST, &dev_cfg, &s_spi);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = mfrc522_reset();
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    mfrc522_write_reg(TModeReg, 0x8D);
    mfrc522_write_reg(TPrescalerReg, 0x3E);
    mfrc522_write_reg(TReloadRegL, 30);
    mfrc522_write_reg(TReloadRegH, 0);
    mfrc522_write_reg(TxASKReg, 0x40);
    mfrc522_write_reg(ModeReg, 0x3D);

    ret = mfrc522_antenna_on();
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t version = 0;
    mfrc522_read_reg(VersionReg, &version);

    ESP_LOGI(TAG, "MFRC522 initialized, version=0x%02X", version);

    return ESP_OK;
}


esp_err_t rfid_mfrc522_read_card_uid(uint8_t *uid, uint8_t *uid_len)
{
    if (uid == NULL || uid_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = mfrc522_request();
    if (ret != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    return mfrc522_anticoll(uid, uid_len);
}