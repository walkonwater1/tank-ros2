/*
 * tank-ros2: 串口控制链路
 *
 * 协议: W,<left>,<right>\n   范围 -100~100，正值前进
 * 看门狗: 收到首帧后 500ms 无新帧自动停车
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "tank_control.h"
#include "tank_uart_ctrl.h"

#define TAG "tank_uart_ctrl"

#define TANK_UART_NUM           (UART_NUM_1)
#define TANK_UART_TX_PIN        (8)
#define TANK_UART_RX_PIN        (10)
#define TANK_UART_BAUD          (921600)
#define TANK_UART_RX_BUF_SIZE   (256)
#define TANK_UART_READ_CHUNK    (32)
#define TANK_UART_POLL_MS       (20)
#define TANK_CMD_TIMEOUT_MS     (500)
#define TANK_LINE_MAX           (31)

static TickType_t s_last_cmd_ticks = 0;

static int clamp_speed(int value)
{
    if (value > 100) return 100;
    if (value < -100) return -100;
    return value;
}

static void process_line(const char *line)
{
    int left = 0;
    int right = 0;

    if (sscanf(line, "W,%d,%d", &left, &right) != 2) {
        ESP_LOGW(TAG, "丢弃非法帧: %s", line);
        return;
    }

    left = clamp_speed(left);
    right = clamp_speed(right);

    if (TankSetWheelSpeeds(left, right)) {
        s_last_cmd_ticks = xTaskGetTickCount();
    }
}

static void TankUartTask(void *arg)
{
    (void)arg;
    char line[TANK_LINE_MAX + 1];
    size_t line_len = 0;
    uint8_t buf[TANK_UART_READ_CHUNK];

    ESP_LOGI(TAG, "控制串口任务启动");

    while (true) {
        const int count = uart_read_bytes(
            TANK_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(TANK_UART_POLL_MS));

        for (int i = 0; i < count; ++i) {
            const char c = (char)buf[i];

            if (c == '\n' || c == '\r') {
                if (line_len > 0) {
                    line[line_len] = '\0';
                    process_line(line);
                }
                line_len = 0;
                continue;
            }

            if (line_len < TANK_LINE_MAX) {
                line[line_len++] = c;
            } else {
                ESP_LOGW(TAG, "帧过长，丢弃缓冲区");
                line_len = 0;
            }
        }

        if (s_last_cmd_ticks != 0 &&
            (xTaskGetTickCount() - s_last_cmd_ticks) > pdMS_TO_TICKS(TANK_CMD_TIMEOUT_MS)) {
            TankStop();
            s_last_cmd_ticks = 0;
            ESP_LOGW(TAG, "命令超时 %dms，已停车", TANK_CMD_TIMEOUT_MS);
        }
    }
}

void TankUartCtrlInit(void)
{
    const uart_config_t config = {
        .baud_rate = TANK_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(TANK_UART_NUM, &config));
    ESP_ERROR_CHECK(uart_set_pin(
        TANK_UART_NUM, TANK_UART_TX_PIN, TANK_UART_RX_PIN,
        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(
        TANK_UART_NUM, TANK_UART_RX_BUF_SIZE, 0, 0, NULL, 0));

    xTaskCreate(TankUartTask, "tank_uart_ctrl", 3072, NULL, 5, NULL);

    ESP_LOGI(TAG, "控制串口就绪: UART%d TX=%d RX=%d baud=%d",
             TANK_UART_NUM, TANK_UART_TX_PIN, TANK_UART_RX_PIN, TANK_UART_BAUD);
}
