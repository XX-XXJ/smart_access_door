#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/i2c.h"

/*
 *  一、系统基础配置
 */

#define DEVICE_ID                   "door_001"

/*
 * USE_SERVO_LOCK = 1：使用舵机模拟开锁
 * USE_SERVO_LOCK = 0：使用继电器控制电磁锁
 */
#define USE_SERVO_LOCK              1

#define NORMAL_PASSWORD             "123456"//正常密码
#define DURESS_PASSWORD             "654321"//胁迫密码
#define ADMIN_PASSWORD              "888888"//管理员密码

#define PASSWORD_MAX_LEN            16
#define MAX_FAIL_COUNT              5
#define LOCKOUT_TIME_MS             60000
#define UNLOCK_HOLD_MS              3000


/*
 *  二、Wi-Fi 配置
 */

#define WIFI_SSID                   "Xiaomi50"
#define WIFI_PASSWORD               "xj520666"
#define WIFI_MAXIMUM_RETRY          5
#define WIFI_CONNECT_TIMEOUT_MS     15000


/*
 *  三、MQTT 云平台配置
 *
 * 局域网测试：
 *      mqtt://10.161.234.114:1883
 *
 * 云服务器测试：
 *      mqtt://你的云服务器IP:1883
 *
 */

#define MQTT_BROKER_URI             "mqtt://10.161.234.114:1883"
#define MQTT_USERNAME               "door_user"
#define MQTT_PASSWORD               "door_pass"

#define MQTT_TOPIC_EVENT            "door/door_001/event"
#define MQTT_TOPIC_STATUS           "door/door_001/status"
#define MQTT_TOPIC_CMD              "door/door_001/cmd"

#define MQTT_QOS                    1
#define MQTT_RETAIN                 0

/*
 * MQTT 补传任务执行间隔。
 * 每隔 MQTT_UPLOAD_INTERVAL_MS 查询一次 CSV 中未上传记录。
 */
#define MQTT_UPLOAD_INTERVAL_MS     3000


/*
 *  四、时间同步配置
 */

#define TIMEZONE_STRING             "CST-8"
#define NTP_SERVER_NAME             "pool.ntp.org"



/*
 *  五、CSV 本地记录缓存配置
 *
 * 所有开锁记录先保存到 SPIFFS 中的 CSV 文件。
 *
 * uploaded 字段：
 *      0：未上传
 *      1：已上传
 *      2：发布中，等待 MQTT_EVENT_PUBLISHED 确认
 */

#define LOCAL_RECORD_CSV_PATH       "/spiffs/door_records.csv"
#define LOCAL_RECORD_TMP_PATH       "/spiffs/door_records.tmp"

#define LOCAL_RECORD_MAX_RETRY      10

/*
 *  六、OLED + PCF8574T 共用 I2C0
 *
 * OLED 和 PCF8574T 共用 I2C0：
 *
 *      SDA = GPIO8
 *      SCL = GPIO9
 *
 * 注意：
 *      OV2640 摄像头的 SCCB 不要再用 I2C0，
 *      摄像头 SCCB 使用 I2C1。
 */

#define OLED_I2C_PORT_NUM           I2C_NUM_0
#define OLED_SDA_GPIO               GPIO_NUM_8
#define OLED_SCL_GPIO               GPIO_NUM_9
#define OLED_I2C_ADDR               0x3C
#define OLED_I2C_FREQ_HZ            400000

#define OLED_WIDTH                  128
#define OLED_HEIGHT                 64

/*
 *  七、PCF8574T 矩阵键盘配置
 *
 * PCF8574T 侧边 4 针：
 *      VCC -> 3.3V
 *      GND -> GND
 *      SDA -> GPIO8
 *      SCL -> GPIO9
 *
 * PCF8574T 上方 P0~P7：
 *      P0~P3 -> 矩阵键盘行
 *      P4~P7 -> 矩阵键盘列
 *
 * INT：
 *      PCF8574T INT -> GPIO4
 *      低电平有效，用于 Deep Sleep 唤醒。
 */

#define KEYPAD_USE_PCF8574          1

#define PCF8574_I2C_PORT_NUM        OLED_I2C_PORT_NUM
#define PCF8574_I2C_ADDR            0x20

#define PCF8574_ROW_0_BIT           0
#define PCF8574_ROW_1_BIT           1
#define PCF8574_ROW_2_BIT           2
#define PCF8574_ROW_3_BIT           3

#define PCF8574_COL_0_BIT           4
#define PCF8574_COL_1_BIT           5
#define PCF8574_COL_2_BIT           6
#define PCF8574_COL_3_BIT           7

#define PCF8574_INT_GPIO            GPIO_NUM_48
#define POWER_WAKE_GPIO             PCF8574_INT_GPIO


/*
 *  八、Deep Sleep 功耗配置
 */

#define POWER_IDLE_SLEEP_MS         120000 //两分钟进入休眠
#define POWER_TIMER_WAKEUP_SEC      300

/*
 *  九、MFRC522 RFID 模块 GPIO
 */

#define RFID_SCK_GPIO               GPIO_NUM_21
#define RFID_MOSI_GPIO              GPIO_NUM_38
#define RFID_MISO_GPIO              GPIO_NUM_39
#define RFID_CS_GPIO                GPIO_NUM_42//SDA
#define RFID_RST_GPIO               GPIO_NUM_47

#define RFID_SPI_CLOCK_HZ           1000000
#define RFID_POLL_INTERVAL_MS       300


/*
 *  十、AS608 指纹模块 UART 配置
 */

#define FINGER_UART_PORT            UART_NUM_1
#define FINGER_TX_GPIO              GPIO_NUM_1//接模块RX
#define FINGER_RX_GPIO              GPIO_NUM_2//接模块TX
#define FINGER_BAUD_RATE            57600
#define FINGER_UART_BUF_SIZE        256
#define FINGER_PASSWORD             0x00000000
#define FINGER_POLL_INTERVAL_MS     500


/*
 *  十一、门锁、蜂鸣器、防拆 GPIO
 */

#define SERVO_GPIO                  GPIO_NUM_40
//#define RELAY_GPIO                  GPIO_NUM_7//继电器
#define BUZZER_GPIO                 GPIO_NUM_41
#define TAMPER_GPIO                 GPIO_NUM_20


/*
 *  十二、舵机参数
 */

#define SERVO_MIN_US                500
#define SERVO_MAX_US                2500
#define SERVO_LOCK_ANGLE            0
#define SERVO_UNLOCK_ANGLE          90


/*
 *  十三、OV2640 摄像头配置
 *
 * 摄像头模块引脚：
 *
 * 上排：
 *      GND   SCL   SDA   D0   D2   D4   D6   PCLK   PWDN
 *
 * 下排：
 *      3.3V  VSYNC HREF  RST  D1   D3   D5   D7     FLASH
 *
 * 注意：
 *      OLED + PCF8574T 使用 I2C0：GPIO8 / GPIO9
 *      摄像头 SCCB 单独使用 I2C1：GPIO17 / GPIO18加外接电阻3.3V上拉
 */

#define CAMERA_SCCB_I2C_PORT_NUM    I2C_NUM_1

#define CAMERA_XCLK_GPIO            GPIO_NUM_NC

#define CAMERA_SIOD_GPIO            GPIO_NUM_17
#define CAMERA_SIOC_GPIO            GPIO_NUM_18

#define CAMERA_D0_GPIO              GPIO_NUM_4
#define CAMERA_D1_GPIO              GPIO_NUM_16
#define CAMERA_D2_GPIO              GPIO_NUM_5
#define CAMERA_D3_GPIO              GPIO_NUM_11
#define CAMERA_D4_GPIO              GPIO_NUM_6
#define CAMERA_D5_GPIO              GPIO_NUM_12
#define CAMERA_D6_GPIO              GPIO_NUM_7
#define CAMERA_D7_GPIO              GPIO_NUM_13

#define CAMERA_VSYNC_GPIO           GPIO_NUM_10
#define CAMERA_HREF_GPIO            GPIO_NUM_14
#define CAMERA_PCLK_GPIO            GPIO_NUM_15

/*
 * 如果 PWDN 直接接 GND，RST 直接接 3.3V，
 * 这里就配置为 GPIO_NUM_NC。
 */
#define CAMERA_PWDN_GPIO            GPIO_NUM_NC
#define CAMERA_RESET_GPIO           GPIO_NUM_NC

/*
 * 独立模块杜邦线连接时，建议先用 10MHz 测试。
 * 稳定后再考虑 20MHz。
 */
#define CAMERA_XCLK_FREQ_HZ         24000000

/*
 *  十四、TFLM 人脸识别配置
 */

#define FACE_CONFIDENCE_THRESHOLD   70
#define FACE_SUCCESS_COOLDOWN_MS    8000
#define FACE_UNKNOWN_COOLDOWN_MS    5000

#endif /* APP_CONFIG_H */