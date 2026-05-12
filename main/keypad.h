#ifndef KEYPAD_H
#define KEYPAD_H

#include "esp_err.h"

/*
 *  PCF8574T 矩阵键盘模块
 *
 * PCF8574T：
 *      P0~P3 -> 矩阵键盘行
 *      P4~P7 -> 矩阵键盘列
 *
 * INT：
 *      接到 ESP32-S3 GPIO4
 *      用于 Deep Sleep 低电平唤醒。
 */

esp_err_t keypad_init(void);

char keypad_get_key(void);

esp_err_t keypad_prepare_sleep_wakeup(void);

#endif /* KEYPAD_H */