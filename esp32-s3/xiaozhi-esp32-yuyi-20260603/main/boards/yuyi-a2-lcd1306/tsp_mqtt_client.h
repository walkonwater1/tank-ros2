#ifndef TSP_MQTT_CLIENT_H
#define TSP_MQTT_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the independent TSP connection: presence, telemetry, and the small
// command subset this chassis supports (move / turn / stop_move / volume /
// report period).  It never bypasses the local motion queue or its timeouts.
esp_err_t TspMqttStart(void);
void TspMqttStop(void);

// ---- Board snapshots used by the TSP telemetry builder --------------------
// Implemented in the board .cc so tsp_mqtt_client.c stays C-only.
bool TankGetPowerStatus(int *voltage_mv, int *percentage,
                        bool *charging, bool *discharging,
                        bool *full, int *charge_pin, int *done_pin);
int TankGetVolume(void);                 // -1 when the codec is not ready
bool TankSetVolume(int value);           // value must be 0..100
bool TankGetWifiStatus(char *ssid, size_t ssid_cap,
                       int *rssi, int *channel,
                       char *ip, size_t ip_cap,
                       bool *connected, bool *config_mode);
size_t TankGetMcpToolNames(const char **names, size_t max);

// Start a background firmware upgrade from an HTTP(S) URL.  Returns false when
// an upgrade is already running or the task could not be created.
bool TankStartFirmwareUpgrade(const char *url);
int TankGetDeviceState(void);   // DeviceState enum value (7 = upgrading)
bool TankIsFirmwareUpgrading(void);

#ifdef __cplusplus
}
#endif

#endif  // TSP_MQTT_CLIENT_H
