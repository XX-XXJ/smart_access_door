#ifndef ALARM_H
#define ALARM_H

#include "esp_err.h"

/*
 *  报警模块
 *
 * 功能：
 * 1. 蜂鸣器提示；
 * 2. 防拆开关检测；
 * 3. 触发防拆报警事件。
 */

esp_err_t alarm_init(void);

void buzzer_beep(int times, int duration_ms);

#endif /* ALARM_H */