/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define MOTOR_SPEED_MAX 100
#define MOTOR_SPEED_80  80
#define MOTOR_SPEED_60  60
#define MOTOR_SPEED_MIN 0

void motor_ledc_init(void);
void spark_bot_motion_control(float x, float y);
void set_motor_speed_coefficients(float coefficient);
void spark_bot_dance(void);

// tank-ros2: 直接设置左右履带速度，范围 -100~100，正值前进
void tank_set_wheel_speeds(int left, int right);


#ifdef __cplusplus
}
#endif

