#ifndef DRIVER_H
#define DRIVER_H

#include "parser.h"
#include "vdev.h"
#include <stdbool.h>
#include <stdint.h>

// Driver Status / Error Codes
typedef enum {
  DRIVER_OK = 0,
  DRIVER_ERR_NULL_ARG,
  DRIVER_ERR_NACK,
  DRIVER_ERR_TIMEOUT,
  DRIVER_ERR_CRC,
  DRIVER_ERR_IO,
  DRIVER_ERR_UNEXPECTED_RESP
} DriverStatus;

// Converted Engineering Units
typedef struct {
  uint16_t raw_temp;
  float temp_celsius;
  int16_t accel_x;
  int16_t accel_y;
  int16_t accel_z;
} DriverTelemetry;

// Asynchronous Callback Signature
typedef void (*TelemetryCallback)(const DriverTelemetry *telemetry,
                                  void *user_data);

// Driver Statistics
typedef struct {
  uint32_t commands_sent;
  uint32_t responses_received;
  uint32_t telemetry_frames_received;
  uint32_t nacks_received;
} DriverStats;

// Driver Context / Handle
typedef struct {
  VirtualDevice *vdev;            // Bound hardware device
  Parser rx_parser;               // Driver RX streaming parser
  uint8_t next_seq;               // Sequence counter for requests
  TelemetryCallback telemetry_cb; // Registered telemetry callback
  void *cb_user_data;             // User context passed to callback
  DriverStats stats;              // Performance & diagnostic stats
} Driver;

// Public Driver APIs
DriverStatus driver_init(Driver *drv, VirtualDevice *vdev);
DriverStatus driver_ping(Driver *drv);
DriverStatus driver_read_reg(Driver *drv, uint8_t addr, uint16_t *out_val);
DriverStatus driver_write_reg(Driver *drv, uint8_t addr, uint16_t val);
DriverStatus driver_start_telemetry(Driver *drv, TelemetryCallback cb,
                                    void *user_data);
DriverStatus driver_stop_telemetry(Driver *drv);

// Hardware time-step simulation & wire polling
// Simulates passing of time: triggers a device tick and processes any arriving
// wire bytes
DriverStatus driver_step(Driver *drv);

#endif // DRIVER_H
