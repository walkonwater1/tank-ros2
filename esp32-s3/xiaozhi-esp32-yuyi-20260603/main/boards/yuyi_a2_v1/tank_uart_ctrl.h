/*
 * tank-ros2: 串口控制链路
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 UART1 控制口，启动接收任务与 500ms 看门狗
void TankUartCtrlInit(void);

#ifdef __cplusplus
}
#endif
