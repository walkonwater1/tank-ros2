#include "tsp_mqtt_client.h"
#include "tank_control.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
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

#define TSP_RECENT_COMMAND_COUNT 8
#define TSP_COMMAND_ID_MAX_LENGTH 64

typedef struct {
    bool used;
    char cmd_id[TSP_COMMAND_ID_MAX_LENGTH + 1];
    bool ok;
    char err[24];
} tsp_recent_command_t;

static tsp_recent_command_t s_recent_commands[TSP_RECENT_COMMAND_COUNT];
static size_t s_recent_command_next;

static void TspPublishCommandAck(esp_mqtt_client_handle_t client, const char *cmd_id,
                                 bool ok, const char *err)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to allocate command ack");
        return;
    }
    cJSON_AddStringToObject(root, "cmd_id", cmd_id);
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddStringToObject(root, "err", err == NULL ? "" : err);

    char *payload = cJSON_PrintUnformatted(root);
    if (payload != NULL) {
        int message_id = esp_mqtt_client_publish(client, TSP_COMMAND_ACK_TOPIC,
                                                 payload, 0, 0, 0);
        ESP_LOGI(TAG, "Command ack: cmd_id=%s ok=%d err=%s msg_id=%d",
                 cmd_id, ok, err == NULL ? "" : err, message_id);
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

static void TspRememberCommand(const char *cmd_id, bool ok, const char *err)
{
    tsp_recent_command_t *entry = &s_recent_commands[s_recent_command_next];
    s_recent_command_next = (s_recent_command_next + 1) % TSP_RECENT_COMMAND_COUNT;
    entry->used = true;
    strlcpy(entry->cmd_id, cmd_id, sizeof(entry->cmd_id));
    entry->ok = ok;
    strlcpy(entry->err, err == NULL ? "" : err, sizeof(entry->err));
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
        TspPublishCommandAck(client, cmd_id, recent->ok, recent->err);
        cJSON_Delete(root);
        return;
    }

    bool ok = false;
    const char *err = "bad_args";

    if (strcmp(cmd, "stop_move") == 0) {
        ok = TankStop();
        err = ok ? "" : "internal_error";
    } else if (strcmp(cmd, "move") == 0 && cJSON_IsObject(args)) {
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
    } else if (strcmp(cmd, "turn") == 0 && cJSON_IsObject(args)) {
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
    } else if (strcmp(cmd, "move") != 0 && strcmp(cmd, "turn") != 0) {
        err = "no_handler";
    }

    TspRememberCommand(cmd_id, ok, err);
    TspPublishCommandAck(client, cmd_id, ok, err);
    ESP_LOGI(TAG, "Command handled: cmd=%s cmd_id=%s ok=%d", cmd, cmd_id, ok);
    cJSON_Delete(root);
}

static void TspTelemetryTask(void *arg)
{
    uint32_t sequence = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!atomic_load(&s_connected) || s_client == NULL) {
            continue;
        }

        esp_netif_ip_info_t ip_info = {0};
        esp_netif_t *wifi = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (wifi == NULL || esp_netif_get_ip_info(wifi, &ip_info) != ESP_OK ||
            ip_info.ip.addr == 0) {
            continue;
        }

        char payload[384];
        uint32_t seq = ++sequence;
        int length = snprintf(
            payload, sizeof(payload),
            "{\"robot_id\":\"" TSP_ROBOT_ID "\",\"seq\":%" PRIu32
            ",\"motor_count\":0,\"tick\":%" PRIu32
            ",\"ip\":\"" IPSTR "\",\"modules\":{\"cloud\":\"ok\",\"report\":\"ok\"},"
            "\"health\":{\"cloud\":{\"state\":\"ok\",\"reason\":\"mqtt connected\"},"
            "\"report\":{\"state\":\"ok\",\"reason\":\"reporting\"}}}",
            seq, (uint32_t)(esp_timer_get_time() / 1000000), IP2STR(&ip_info.ip));
        if (length < 0 || length >= (int)sizeof(payload)) {
            ESP_LOGE(TAG, "Telemetry payload buffer is too small");
            continue;
        }

        int message_id = esp_mqtt_client_publish(s_client, TSP_TELEMETRY_TOPIC,
                                                 payload, length, 0, 0);
        if (message_id < 0) {
            ESP_LOGW(TAG, "Failed to enqueue telemetry seq=%" PRIu32, seq);
        } else if (seq == 1 || seq % 30 == 0) {
            ESP_LOGI(TAG, "Telemetry reporting: seq=%" PRIu32 " topic=%s",
                     seq, TSP_TELEMETRY_TOPIC);
        }
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
    if (xTaskCreate(TspTelemetryTask, "tsp_telemetry", 4096, NULL, 4,
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
