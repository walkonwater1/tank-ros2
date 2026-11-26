#ifndef TSP_MQTT_CLIENT_H
#define TSP_MQTT_CLIENT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the independent TSP connection for presence and telemetry reporting.
// This client does not subscribe to commands or control the motors.
esp_err_t TspMqttStart(void);
void TspMqttStop(void);

#ifdef __cplusplus
}
#endif

#endif  // TSP_MQTT_CLIENT_H
