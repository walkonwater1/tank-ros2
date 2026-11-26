#include "tsp_mqtt_client.h"
#include "tank_control.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "tsp_mqtt";

#define TSP_BROKER_URI       "mqtt://47.108.223.180:1883"
#define TSP_ROBOT_TYPE       "esp32-robot"
#define TSP_ROBOT_ID         "nex_001"
#define TSP_CLIENT_ID        TSP_ROBOT_TYPE "-" TSP_ROBOT_ID
#define TSP_STATUS_TOPIC     TSP_ROBOT_TYPE "/" TSP_ROBOT_ID "/status"
#define TSP_TELEMETRY_TOPIC  TSP_ROBOT_TYPE "/" TSP_ROBOT_ID "/up/telemetry"
#define TSP_COMMAND_TOPIC    TSP_ROBOT_TYPE "/" TSP_ROBOT_ID "/down/cmd"
#define TSP_COMMAND_ACK_TOPIC TSP_ROBOT_TYPE "/" TSP_ROBOT_ID "/up/cmd_ack"
#define TSP_ONLINE_PAYLOAD   "{\"robot_id\":\"" TSP_ROBOT_ID "\",\"online\":true}"
#define TSP_OFFLINE_PAYLOAD  "{\"robot_id\":\"" TSP_ROBOT_ID "\",\"online\":false}"

static esp_mqtt_client_handle_t s_client;
static TaskHandle_t s_start_task;
static TaskHandle_t s_telemetry_task;
static atomic_bool s_connected;
// Telemetry cadence, changeable from the platform via set_report_period.
// 1..15 s; the console owns the UI defaults and the platform re-validates.
static atomic_int s_report_period_ms = 1000;

#define TSP_RECENT_COMMAND_COUNT 8
#define TSP_COMMAND_ID_MAX_LENGTH 64

typedef struct {
    bool used;
    char cmd_id[TSP_COMMAND_ID_MAX_LENGTH + 1];
    bool ok;
    char err[24];
    char reply[64];
} tsp_recent_command_t;

static tsp_recent_command_t s_recent_commands[TSP_RECENT_COMMAND_COUNT];
static size_t s_recent_command_next;

static void TspPublishCommandAck(esp_mqtt_client_handle_t client, const char *cmd_id,
                                 bool ok, const char *err, const char *reply)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to allocate command ack");
        return;
    }
    cJSON_AddStringToObject(root, "cmd_id", cmd_id);
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddStringToObject(root, "err", err == NULL ? "" : err);
    // The console renders `reply` verbatim for query/setter commands
    // (`volume=55`, `period=2s`).  Omit it when there is nothing to say.
    if (reply != NULL && reply[0] != '\0') {
        cJSON_AddStringToObject(root, "reply", reply);
    }

    char *payload = cJSON_PrintUnformatted(root);
    if (payload != NULL) {
        int message_id = esp_mqtt_client_publish(client, TSP_COMMAND_ACK_TOPIC,
                                                 payload, 0, 0, 0);
        ESP_LOGI(TAG, "Command ack: cmd_id=%s ok=%d err=%s reply=%s msg_id=%d",
                 cmd_id, ok, err == NULL ? "" : err,
                 reply == NULL ? "" : reply, message_id);
        cJSON_free(payload);
    }
    cJSON_Delete(root);
}

static tsp_recent_command_t *TspFindRecentCommand(const char *cmd_id)
{
    for (size_t i = 0; i < TSP_RECENT_COMMAND_COUNT; ++i) {
        if (s_recent_commands[i].used &&
            strcmp(s_recent_commands[i].cmd_id, cmd_id) == 0) {
            return &s_recent_commands[i];
        }
    }
    return NULL;
}

static void TspRememberCommand(const char *cmd_id, bool ok, const char *err,
                               const char *reply)
{
    tsp_recent_command_t *entry = &s_recent_commands[s_recent_command_next];
    s_recent_command_next = (s_recent_command_next + 1) % TSP_RECENT_COMMAND_COUNT;
    entry->used = true;
    strlcpy(entry->cmd_id, cmd_id, sizeof(entry->cmd_id));
    entry->ok = ok;
    strlcpy(entry->err, err == NULL ? "" : err, sizeof(entry->err));
    strlcpy(entry->reply, reply == NULL ? "" : reply, sizeof(entry->reply));
}

static int TspClampDuration(int duration_ms)
{
    if (duration_ms < 100) {
        return 100;
    }
    if (duration_ms > 3000) {
        return 3000;
    }
    return duration_ms;
}

static void TspHandleCommand(esp_mqtt_client_handle_t client, const char *payload,
                             size_t payload_length)
{
    cJSON *root = cJSON_ParseWithLength(payload, payload_length);
    if (root == NULL) {
        ESP_LOGW(TAG, "Ignoring malformed command JSON");
        return;
    }

    cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    cJSON *cmd_id_item = cJSON_GetObjectItemCaseSensitive(root, "cmd_id");
    cJSON *args = cJSON_GetObjectItemCaseSensitive(root, "args");
    if (!cJSON_IsString(cmd_item) || cmd_item->valuestring == NULL ||
        !cJSON_IsString(cmd_id_item) || cmd_id_item->valuestring == NULL ||
        cmd_id_item->valuestring[0] == '\0' ||
        strlen(cmd_id_item->valuestring) > TSP_COMMAND_ID_MAX_LENGTH) {
        ESP_LOGW(TAG, "Ignoring command with invalid envelope");
        cJSON_Delete(root);
        return;
    }

    const char *cmd = cmd_item->valuestring;
    const char *cmd_id = cmd_id_item->valuestring;
    tsp_recent_command_t *recent = TspFindRecentCommand(cmd_id);
    if (recent != NULL) {
        ESP_LOGW(TAG, "Duplicate command: cmd_id=%s", cmd_id);
        TspPublishCommandAck(client, cmd_id, recent->ok, recent->err, recent->reply);
        cJSON_Delete(root);
        return;
    }

    bool ok = false;
    const char *err = "bad_args";
    char reply[64] = "";

    if (strcmp(cmd, "stop_move") == 0) {
        ok = TankStop();
        err = ok ? "" : "internal_error";
    } else if (strcmp(cmd, "move") == 0) {
        if (!cJSON_IsObject(args)) {
            err = "bad_args";
        } else {
            cJSON *direction = cJSON_GetObjectItemCaseSensitive(args, "direction");
            cJSON *distance = cJSON_GetObjectItemCaseSensitive(args, "distance");
            cJSON *speed = cJSON_GetObjectItemCaseSensitive(args, "speed");
            if (cJSON_IsString(direction) && direction->valuestring != NULL &&
                cJSON_IsNumber(distance) && distance->valuedouble >= 0.1 &&
                distance->valuedouble <= 1.0 &&
                (speed == NULL || (cJSON_IsNumber(speed) && speed->valuedouble > 0 &&
                                   speed->valuedouble <= 0.3))) {
                double speed_value = speed == NULL ? 0.3 : speed->valuedouble;
                int duration_ms = TspClampDuration((int)(distance->valuedouble /
                                                          speed_value * 1000.0));
                if (strcmp(direction->valuestring, "forward") == 0) {
                    ok = TankMoveForwardFor(duration_ms);
                } else if (strcmp(direction->valuestring, "back") == 0) {
                    ok = TankMoveBackwardFor(duration_ms);
                } else {
                    err = "unsupported_direction";
                }
                if (ok) {
                    err = "";
                } else if (strcmp(err, "unsupported_direction") != 0) {
                    err = "internal_error";
                }
            }
        }
    } else if (strcmp(cmd, "turn") == 0) {
        if (!cJSON_IsObject(args)) {
            err = "bad_args";
        } else {
            cJSON *angle = cJSON_GetObjectItemCaseSensitive(args, "angle_deg");
            if (cJSON_IsNumber(angle) && angle->valuedouble != 0 &&
                angle->valuedouble >= -180 && angle->valuedouble <= 180) {
                double absolute_angle = angle->valuedouble < 0
                    ? -angle->valuedouble : angle->valuedouble;
                int duration_ms = TspClampDuration((int)(absolute_angle * 25.0));
                ok = angle->valuedouble > 0
                    ? TankTurnLeftFor(duration_ms)
                    : TankTurnRightFor(duration_ms);
                err = ok ? "" : "internal_error";
            }
        }
    } else if (strcmp(cmd, "get_volume") == 0) {
        int volume = TankGetVolume();
        ok = true;
        err = "";
        if (volume >= 0) {
            snprintf(reply, sizeof(reply), "volume=%d", volume);
        } else {
            snprintf(reply, sizeof(reply), "volume_unknown");
        }
    } else if (strcmp(cmd, "set_volume") == 0) {
        if (!cJSON_IsObject(args)) {
            err = "bad_args";
        } else {
            cJSON *value = cJSON_GetObjectItemCaseSensitive(args, "value");
            if (value == NULL) {
                err = "missing_value";
            } else if (!cJSON_IsNumber(value)) {
                err = "bad_args";
            } else {
                double volume = value->valuedouble;
                if (volume < 0 || volume > 100 || (double)(int)volume != volume) {
                    err = "out_of_range";
                } else if (!TankSetVolume((int)volume)) {
                    err = "internal_error";
                } else {
                    ok = true;
                    err = "";
                    snprintf(reply, sizeof(reply), "volume=%d", (int)volume);
                }
            }
        }
    } else if (strcmp(cmd, "dance") == 0) {
        // Enqueued to the motion task; the call itself does not block.
        TankDance();
        ok = true;
        err = "";
    } else if (strcmp(cmd, "upgrade_firmware") == 0) {
        // TSP-triggered OTA.  The download/write/reboot runs in a separate task;
        // this handler only validates and starts it, then acks immediately.
        if (!cJSON_IsObject(args)) {
            err = "bad_args";
        } else {
            cJSON *url = cJSON_GetObjectItemCaseSensitive(args, "url");
            if (url == NULL) {
                err = "missing_value";
            } else if (!cJSON_IsString(url) || url->valuestring == NULL) {
                err = "bad_args";
            } else if (url->valuestring[0] == '\0') {
                err = "missing_value";
            } else if (strlen(url->valuestring) > 255) {
                err = "out_of_range";
            } else if (strncmp(url->valuestring, "http://", 7) != 0 &&
                       strncmp(url->valuestring, "https://", 8) != 0) {
                err = "bad_args";
            } else if (!TankStartFirmwareUpgrade(url->valuestring)) {
                err = "ota_in_progress";
            } else {
                ok = true;
                err = "";
                snprintf(reply, sizeof(reply), "ota=started");
            }
        }
    } else if (strcmp(cmd, "set_report_period") == 0) {
        if (!cJSON_IsObject(args)) {
            err = "bad_args";
        } else {
            cJSON *seconds = cJSON_GetObjectItemCaseSensitive(args, "seconds");
            if (seconds == NULL) {
                err = "missing_value";
            } else if (!cJSON_IsNumber(seconds)) {
                err = "bad_args";
            } else {
                double period = seconds->valuedouble;
                if (period < 1 || period > 15 || (double)(int)period != period) {
                    err = "out_of_range";
                } else {
                    atomic_store(&s_report_period_ms, (int)period * 1000);
                    ok = true;
                    err = "";
                    snprintf(reply, sizeof(reply), "period=%ds", (int)period);
                }
            }
        }
    } else {
        err = "no_handler";
    }

    TspRememberCommand(cmd_id, ok, err, reply);
    TspPublishCommandAck(client, cmd_id, ok, err, reply);
    ESP_LOGI(TAG, "Command handled: cmd=%s cmd_id=%s ok=%d", cmd, cmd_id, ok);
    cJSON_Delete(root);
}

static void TspAddHealth(cJSON *modules, cJSON *health, const char *key,
                         const char *state, const char *reason)
{
    cJSON_AddStringToObject(modules, key, state);
    cJSON *item = cJSON_CreateObject();
    if (item == NULL) {
        return;
    }
    cJSON_AddStringToObject(item, "state", state);
    cJSON_AddStringToObject(item, "reason", reason);
    cJSON_AddItemToObject(health, key, item);
}

// ---------------------------------------------------------------------------
// ESP32 board resources for the console's "性能监测" block.
//
// CPU: FreeRTOS run-time stats (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS +
//      ESP timer).  We look at the idle tasks' counter delta, so the number is
//      "how much of the two cores was NOT idle", 0..100%.
// RAM: heap_caps internal SRAM + PSRAM, in KB.
// Flash: running app image length / app partition size; chip size for context.
//        The image length is read once via esp_image_verify and cached.
// ---------------------------------------------------------------------------
#define TSP_TASK_STATUS_MAX 64
static TaskStatus_t s_task_status[TSP_TASK_STATUS_MAX];
static uint32_t s_prev_idle_rt = 0;
static int64_t s_prev_cpu_us = 0;
static bool s_cpu_prev_valid = false;

static uint32_t s_app_used_kb = 0;
static bool s_app_used_valid = false;

static int TspReadCpuPercent(void)
{
    const UBaseType_t count = uxTaskGetSystemState(s_task_status, TSP_TASK_STATUS_MAX, NULL);
    if (count == 0) {
        return -1;
    }

#if configNUMBER_OF_CORES > 1
    const TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCore(0);
    const TaskHandle_t idle1 = xTaskGetIdleTaskHandleForCore(1);
#else
    const TaskHandle_t idle0 = xTaskGetIdleTaskHandle();
    const TaskHandle_t idle1 = NULL;
#endif

    uint32_t idle = 0;
    for (UBaseType_t i = 0; i < count; i++) {
        if (s_task_status[i].xHandle == idle0 || s_task_status[i].xHandle == idle1) {
            idle += s_task_status[i].ulRunTimeCounter;
        }
    }

    const int64_t now = esp_timer_get_time();
    int pct = -1;
    if (s_cpu_prev_valid && now > s_prev_cpu_us) {
        const int64_t wall_us = now - s_prev_cpu_us;
        const uint32_t idle_delta = idle - s_prev_idle_rt;
        const double cores = (double)configNUMBER_OF_CORES;
        // idle_delta is the sum of idle time on all cores; busy% = 100 - idle%.
        double idle_pct = ((double)idle_delta / ((double)wall_us * cores)) * 100.0;
        double busy = 100.0 - idle_pct;
        if (busy < 0.0) busy = 0.0;
        if (busy > 100.0) busy = 100.0;
        pct = (int)(busy + 0.5);
    }

    s_prev_idle_rt = idle;
    s_prev_cpu_us = now;
    s_cpu_prev_valid = true;
    return pct;
}

static void TspReadHostStats(int *cpu_pct,
                             int *mem_used_kb, int *mem_total_kb, int *mem_min_free_kb,
                             int *psram_used_kb, int *psram_total_kb,
                             int *flash_used_kb, int *flash_total_kb, int *flash_chip_kb)
{
    if (cpu_pct != NULL) {
        *cpu_pct = TspReadCpuPercent();
    }

    const size_t mem_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    const size_t mem_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t mem_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    if (mem_total_kb != NULL) *mem_total_kb = (int)(mem_total / 1024);
    if (mem_used_kb != NULL) *mem_used_kb = (int)((mem_total - mem_free) / 1024);
    if (mem_min_free_kb != NULL) *mem_min_free_kb = (int)(mem_min / 1024);

    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_total_kb != NULL) *psram_total_kb = psram_total ? (int)(psram_total / 1024) : 0;
    if (psram_used_kb != NULL) {
        *psram_used_kb = psram_total ? (int)((psram_total - psram_free) / 1024) : 0;
    }

    uint32_t flash_size = 0;
    if (flash_chip_kb != NULL) {
        *flash_chip_kb = esp_flash_get_size(NULL, &flash_size) == ESP_OK
            ? (int)(flash_size / 1024) : 0;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (flash_total_kb != NULL) {
        *flash_total_kb = running != NULL ? (int)(running->size / 1024) : 0;
    }
    if (flash_used_kb != NULL) {
        *flash_used_kb = 0;
        if (running != NULL) {
            if (!s_app_used_valid) {
                // Read/verify the running image once; it cannot change at runtime.
                esp_image_metadata_t meta = {0};
                esp_partition_pos_t pos = {
                    .offset = running->address,
                    .size = running->size,
                };
                if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) == ESP_OK) {
                    s_app_used_kb = meta.image_len / 1024;
                    s_app_used_valid = true;
                }
            }
            if (s_app_used_valid) {
                *flash_used_kb = (int)s_app_used_kb;
            }
        }
    }
}

// One telemetry frame.  The console treats every group as conditional:
// a missing key means "this robot does not report it", never zero.
static void TspPublishTelemetry(esp_mqtt_client_handle_t client, uint32_t seq)
{
    char ip[32] = "";
    esp_netif_ip_info_t ip_info = {0};
    esp_netif_t *wifi_if = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (wifi_if != NULL && esp_netif_get_ip_info(wifi_if, &ip_info) == ESP_OK &&
        ip_info.ip.addr != 0) {
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ip_info.ip));
    }

    int voltage_mv = 0;
    int percentage = -1;
    bool charging = false;
    bool discharging = false;
    bool battery_full = false;
    int charge_pin = -1;
    int done_pin = -1;
    TankGetPowerStatus(&voltage_mv, &percentage, &charging, &discharging,
                       &battery_full, &charge_pin, &done_pin);

    char ssid[33] = "";
    char wifi_ip[32] = "";
    int rssi = 0;
    int channel = 0;
    bool wifi_connected = false;
    bool config_mode = false;
    TankGetWifiStatus(ssid, sizeof(ssid), &rssi, &channel,
                      wifi_ip, sizeof(wifi_ip), &wifi_connected, &config_mode);

    const char *tool_names[32];
    const size_t tool_count = TankGetMcpToolNames(tool_names, 32);
    const int volume = TankGetVolume();
    const bool mqtt_connected = atomic_load(&s_connected);

    cJSON *root = cJSON_CreateObject();
    cJSON *modules = cJSON_CreateObject();
    cJSON *health = cJSON_CreateObject();
    if (root == NULL || modules == NULL || health == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(modules);
        cJSON_Delete(health);
        ESP_LOGE(TAG, "Failed to allocate telemetry JSON");
        return;
    }

    cJSON_AddStringToObject(root, "robot_id", TSP_ROBOT_ID);
    cJSON_AddStringToObject(root, "firmware_version", esp_app_get_description()->version);
    cJSON_AddStringToObject(root, "ota_state",
                            TankIsFirmwareUpgrading() ? "upgrading" : "idle");
    cJSON_AddNumberToObject(root, "seq", (double)seq);
    cJSON_AddNumberToObject(root, "tick", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "motor_count", 0);
    if (ip[0] != '\0') {
        cJSON_AddStringToObject(root, "ip", ip);
    }

    // Battery / charger.  `battery_soc` is omitted (not sent as 0) when the
    // ADC is not calibrated; the console then shows voltage only.
    if (voltage_mv > 0) {
        cJSON_AddNumberToObject(root, "battery_voltage_mv", (double)voltage_mv);
    }
    if (percentage >= 0) {
        cJSON_AddNumberToObject(root, "battery_soc", (double)percentage);
    }
    cJSON_AddBoolToObject(root, "battery_charging", charging);
    cJSON_AddBoolToObject(root, "battery_discharging", discharging);
    cJSON_AddBoolToObject(root, "battery_full", battery_full);
    if (charge_pin >= 0) {
        cJSON_AddNumberToObject(root, "battery_charge_pin", charge_pin);
    }
    if (done_pin >= 0) {
        cJSON_AddNumberToObject(root, "battery_done_pin", done_pin);
    }
    cJSON_AddStringToObject(root, "battery_source", "adc");

    if (volume >= 0) {
        cJSON_AddNumberToObject(root, "volume", (double)volume);
    }

    // Wi-Fi / provisioning.  In AP provisioning mode the robot has no route
    // to the broker, so this frame normally will not be seen live; the console
    // still uses the last known values as a diagnosis hint.
    cJSON *wifi = cJSON_CreateObject();
    if (wifi != NULL) {
        cJSON_AddStringToObject(wifi, "mode",
                                config_mode ? "config" : (wifi_connected ? "sta" : "offline"));
        cJSON_AddBoolToObject(wifi, "connected", wifi_connected);
        if (ssid[0] != '\0') {
            cJSON_AddStringToObject(wifi, "ssid", ssid);
        }
        if (wifi_connected) {
            cJSON_AddNumberToObject(wifi, "rssi", (double)rssi);
            cJSON_AddNumberToObject(wifi, "channel", (double)channel);
        }
        if (wifi_ip[0] != '\0') {
            cJSON_AddStringToObject(wifi, "ip", wifi_ip);
        }
        cJSON_AddItemToObject(root, "wifi", wifi);
    }

    // MCP service: ready flag + registered tool names.
    cJSON *mcp = cJSON_CreateObject();
    if (mcp != NULL) {
        cJSON_AddBoolToObject(mcp, "ready", tool_count > 0);
        cJSON *tools = cJSON_CreateStringArray(tool_names, (int)tool_count);
        if (tools != NULL) {
            cJSON_AddItemToObject(mcp, "tools", tools);
        }
        cJSON_AddItemToObject(root, "mcp", mcp);
    }

    // Board resources (CPU / internal RAM / PSRAM / flash).  The console's
    // 性能监测 block uses the `host` object; G1 uses MB fields, ESP32 uses KB.
    int cpu_pct = -1, mem_used_kb = 0, mem_total_kb = 0, mem_min_free_kb = 0;
    int psram_used_kb = 0, psram_total_kb = 0;
    int flash_used_kb = 0, flash_total_kb = 0, flash_chip_kb = 0;
    TspReadHostStats(&cpu_pct, &mem_used_kb, &mem_total_kb, &mem_min_free_kb,
                     &psram_used_kb, &psram_total_kb,
                     &flash_used_kb, &flash_total_kb, &flash_chip_kb);

    cJSON *host = cJSON_CreateObject();
    if (host != NULL) {
        if (cpu_pct >= 0) {
            cJSON_AddNumberToObject(host, "cpu_pct", cpu_pct);
        }
        if (mem_total_kb > 0) {
            cJSON_AddNumberToObject(host, "mem_used_kb", mem_used_kb);
            cJSON_AddNumberToObject(host, "mem_total_kb", mem_total_kb);
        }
        if (mem_min_free_kb > 0) {
            cJSON_AddNumberToObject(host, "mem_min_free_kb", mem_min_free_kb);
        }
        if (psram_total_kb > 0) {
            cJSON_AddNumberToObject(host, "psram_used_kb", psram_used_kb);
            cJSON_AddNumberToObject(host, "psram_total_kb", psram_total_kb);
        }
        // Only report the flash pair when the image length is known; a
        // `used=0 / total=3.9 MB` line would look like a full disk.
        if (flash_total_kb > 0 && flash_used_kb > 0) {
            cJSON_AddNumberToObject(host, "flash_used_kb", flash_used_kb);
            cJSON_AddNumberToObject(host, "flash_total_kb", flash_total_kb);
        }
        if (flash_chip_kb > 0) {
            cJSON_AddNumberToObject(host, "flash_chip_kb", flash_chip_kb);
        }
        if (cJSON_GetArraySize(host) > 0) {
            cJSON_AddItemToObject(root, "host", host);
        } else {
            cJSON_Delete(host);
        }
    }

    char reason[96];
    TspAddHealth(modules, health, "speech",
                 volume >= 0 ? "ok" : "degraded",
                 volume >= 0 ? "audio codec ready" : "audio codec unavailable");
    TspAddHealth(modules, health, "action", "ok", "motion queue ready");

    snprintf(reason, sizeof(reason), "%u MCP tools", (unsigned)tool_count);
    TspAddHealth(modules, health, "mcp",
                 tool_count > 0 ? "ok" : "degraded", reason);

    if (wifi_connected) {
        snprintf(reason, sizeof(reason), "%s rssi %d dBm",
                 ssid[0] != '\0' ? ssid : "connected", rssi);
    } else {
        snprintf(reason, sizeof(reason), "%s",
                 config_mode ? "provisioning AP" : "not connected");
    }
    TspAddHealth(modules, health, "wifi",
                 wifi_connected ? "ok" : "degraded", reason);

    TspAddHealth(modules, health, "cloud",
                 mqtt_connected ? "ok" : "degraded",
                 mqtt_connected ? "mqtt connected" : "mqtt disconnected");
    TspAddHealth(modules, health, "report", "ok", "reporting");

    cJSON_AddItemToObject(root, "modules", modules);
    cJSON_AddItemToObject(root, "health", health);

    char *payload = cJSON_PrintUnformatted(root);
    if (payload != NULL) {
        int message_id = esp_mqtt_client_publish(client, TSP_TELEMETRY_TOPIC,
                                                 payload, 0, 0, 0);
        if (message_id < 0) {
            ESP_LOGW(TAG, "Failed to enqueue telemetry seq=%" PRIu32, seq);
        } else if (seq == 1 || seq % 30 == 0) {
            ESP_LOGI(TAG, "Telemetry reporting: seq=%" PRIu32
                     " battery=%dmV/%d%% wifi=%s mcp_tools=%u",
                     seq, voltage_mv, percentage,
                     wifi_connected ? "connected" : "offline",
                     (unsigned)tool_count);
        }
        cJSON_free(payload);
    }
    cJSON_Delete(root);
}

static void TspTelemetryTask(void *arg)
{
    uint32_t sequence = 0;
    (void)arg;

    while (true) {
        int period_ms = atomic_load(&s_report_period_ms);
        if (period_ms < 1000) {
            period_ms = 1000;
        } else if (period_ms > 15000) {
            period_ms = 15000;
        }
        vTaskDelay(pdMS_TO_TICKS(period_ms));

        if (!atomic_load(&s_connected) || s_client == NULL) {
            continue;
        }
        TspPublishTelemetry(s_client, ++sequence);
    }
}

static void TspMqttEventHandler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        atomic_store(&s_connected, true);
        int subscribe_id = esp_mqtt_client_subscribe(event->client,
                                                     TSP_COMMAND_TOPIC, 1);
        int message_id = esp_mqtt_client_publish(event->client, TSP_STATUS_TOPIC,
                                                 TSP_ONLINE_PAYLOAD, 0, 0, 1);
        ESP_LOGI(TAG, "TSP connected: robot_type=%s robot_id=%s status_msg_id=%d command_sub_id=%d",
                 TSP_ROBOT_TYPE, TSP_ROBOT_ID, message_id, subscribe_id);
        break;
    }
    case MQTT_EVENT_DATA:
        if (event->topic_len == (int)strlen(TSP_COMMAND_TOPIC) &&
            memcmp(event->topic, TSP_COMMAND_TOPIC, event->topic_len) == 0) {
            if (event->current_data_offset == 0 &&
                event->total_data_len == event->data_len) {
                TspHandleCommand(event->client, event->data, event->data_len);
            } else {
                ESP_LOGW(TAG, "Ignoring fragmented command payload (%d/%d bytes)",
                         event->data_len, event->total_data_len);
            }
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        atomic_store(&s_connected, false);
        ESP_LOGW(TAG, "TSP disconnected; automatic reconnect is enabled");
        break;
    case MQTT_EVENT_ERROR:
        atomic_store(&s_connected, false);
        ESP_LOGW(TAG, "TSP transport error; waiting for reconnect");
        break;
    default:
        break;
    }
}

static esp_err_t TspMqttStartClient(void)
{
    const esp_mqtt_client_config_t config = {
        .broker.address.uri = TSP_BROKER_URI,
        .credentials.client_id = TSP_CLIENT_ID,
        .session.last_will = {
            .topic = TSP_STATUS_TOPIC,
            .msg = TSP_OFFLINE_PAYLOAD,
            .qos = 0,
            .retain = 1,
        },
        .session.keepalive = 60,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .network.reconnect_timeout_ms = 5000,
    };

    s_client = esp_mqtt_client_init(&config);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize TSP MQTT client");
        return ESP_FAIL;
    }

    atomic_store(&s_connected, false);
    if (xTaskCreate(TspTelemetryTask, "tsp_telemetry", 6144, NULL, 4,
                    &s_telemetry_task) != pdPASS) {
        s_telemetry_task = NULL;
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        ESP_LOGE(TAG, "Failed to create TSP telemetry task");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY,
                                                   TspMqttEventHandler, NULL);
    if (err == ESP_OK) {
        err = esp_mqtt_client_start(s_client);
    }
    if (err != ESP_OK) {
        vTaskDelete(s_telemetry_task);
        s_telemetry_task = NULL;
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        ESP_LOGE(TAG, "Failed to start TSP MQTT client: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "TSP MQTT started: %s client_id=%s", TSP_BROKER_URI, TSP_CLIENT_ID);
    return ESP_OK;
}

static void TspMqttStartTask(void *arg)
{
    // The board is constructed before esp_netif/LWIP is ready. Wait until the
    // normal Wi-Fi startup has completed before allowing MQTT to resolve/connect.
    vTaskDelay(pdMS_TO_TICKS(6000));
    esp_err_t err = TspMqttStartClient();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Deferred TSP MQTT startup failed: %s", esp_err_to_name(err));
    }
    s_start_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t TspMqttStart(void)
{
    if (s_client != NULL || s_start_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xTaskCreate(TspMqttStartTask, "tsp_mqtt_start", 4096, NULL, 4,
                    &s_start_task) != pdPASS) {
        s_start_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void TspMqttStop(void)
{
    if (s_start_task != NULL) {
        vTaskDelete(s_start_task);
        s_start_task = NULL;
    }
    if (s_client == NULL) {
        return;
    }

    atomic_store(&s_connected, false);
    if (s_telemetry_task != NULL) {
        vTaskDelete(s_telemetry_task);
        s_telemetry_task = NULL;
    }
    esp_mqtt_client_publish(s_client, TSP_STATUS_TOPIC, TSP_OFFLINE_PAYLOAD, 0, 0, 1);
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
}
