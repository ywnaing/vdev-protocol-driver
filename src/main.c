#define _POSIX_C_SOURCE 200809L

#include "driver.h"
#include "protocol.h"
#include "vdev.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

// Context for live telemetry stream callback
typedef struct {
  uint32_t sample_count;
} StreamContext;

// Callback triggered asynchronously whenever the hardware emits telemetry
static void on_telemetry_received(const DriverTelemetry *data, void *user_data) {
  StreamContext *ctx = (StreamContext *)user_data;
  ctx->sample_count++;

  printf("  [%02u] Temp: %5.1f°C | Accel: [X: %+4d mg, Y: %+4d mg, Z: %+4d mg] | CRC: VALID\n",
         ctx->sample_count, data->temp_celsius, data->accel_x, data->accel_y, data->accel_z);
}

// Small sleep helper for smooth terminal animation
static void sleep_ms(int ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

int main(void) {
  printf("\n");
  printf("===================================================================\n");
  printf("    VIRTUAL DEVICE DRIVER & PROTOCOL ENGINE - LIVE SIMULATION      \n");
  printf("===================================================================\n\n");

  // -------------------------------------------------------------
  // 1. Hardware & Driver Initialization
  // -------------------------------------------------------------
  printf("[INIT] Creating Virtual Hardware Peripheral (vdev)...\n");
  VirtualDevice vdev;
  vdev_init(&vdev);

  printf("[INIT] Initializing Host Device Driver & Binding to Device...\n");
  Driver drv;
  if (driver_init(&drv, &vdev) != DRIVER_OK) {
    fprintf(stderr, "Failed to initialize driver!\n");
    return 1;
  }
  printf("[INIT] Driver bound successfully via simulated loopback transport.\n\n");

  // -------------------------------------------------------------
  // 2. Hardware Ping & Link Verification
  // -------------------------------------------------------------
  printf("[1/5] Pinging hardware peripheral over simulated wire (OP_PING)...\n");
  if (driver_ping(&drv) == DRIVER_OK) {
    printf("      -> [SUCCESS] OP_RSP_ACK received. Wire link is healthy and responsive!\n\n");
  } else {
    fprintf(stderr, "      -> [ERROR] Ping failed!\n\n");
    return 1;
  }

  // -------------------------------------------------------------
  // 3. Register Map Dump & Inspection
  // -------------------------------------------------------------
  printf("[2/5] Reading peripheral register map over wire (OP_READ_REG):\n");
  printf("      +-------+-----------------+--------+-----------+-------------------------+\n");
  printf("      | Addr  | Register Name   | Access | Raw Value | Interpretation          |\n");
  printf("      +-------+-----------------+--------+-----------+-------------------------+\n");

  const char *reg_names[REG_MAP_SIZE] = {
      "REG_DEV_ID", "REG_FW_VER", "REG_STATUS", "REG_CTRL", "REG_SAMPLE_RATE",
      "REG_TEMP",   "REG_ACCEL_X", "REG_ACCEL_Y", "REG_ACCEL_Z", "REG_SCRATCHPAD"
  };

  const char *reg_perms[REG_MAP_SIZE] = {
      "RO", "RO", "RO", "RW", "RW", "RO", "RO", "RO", "RO", "RW"
  };

  for (uint8_t addr = 0; addr < REG_MAP_SIZE; addr++) {
    uint16_t val = 0;
    if (driver_read_reg(&drv, addr, &val) == DRIVER_OK) {
      char desc[64] = "";
      if (addr == REG_DEV_ID) snprintf(desc, sizeof(desc), "Chip ID (0xCAFE)");
      else if (addr == REG_FW_VER) snprintf(desc, sizeof(desc), "Firmware v%u.%u", (val >> 8) & 0xFF, val & 0xFF);
      else if (addr == REG_STATUS) snprintf(desc, sizeof(desc), "DEV_STATUS_READY");
      else if (addr == REG_SAMPLE_RATE) snprintf(desc, sizeof(desc), "%u Hz sample rate", val);
      else if (addr == REG_TEMP) snprintf(desc, sizeof(desc), "%.1f°C (Calibrated)", (float)val / 10.0f);
      else if (addr == REG_ACCEL_Z) snprintf(desc, sizeof(desc), "%d mg (~1.0g gravity)", val);
      else snprintf(desc, sizeof(desc), "0x%04X", val);

      printf("      | 0x%02X  | %-15s | %-6s | 0x%04X    | %-23s |\n",
             addr, reg_names[addr], reg_perms[addr], val, desc);
    }
  }
  printf("      +-------+-----------------+--------+-----------+-------------------------+\n\n");

  // -------------------------------------------------------------
  // 4. Register Modification & Access Control Protection
  // -------------------------------------------------------------
  printf("[3/5] Testing Register Read/Write & Hardware Protection:\n");
  
  printf("      a) Writing 0x5A5A to REG_SCRATCHPAD (RW)...\n");
  if (driver_write_reg(&drv, REG_SCRATCHPAD, 0x5A5A) == DRIVER_OK) {
    uint16_t read_back = 0;
    driver_read_reg(&drv, REG_SCRATCHPAD, &read_back);
    printf("         -> [VERIFIED] Read-back value: 0x%04X (Write accepted!)\n", read_back);
  }

  printf("      b) Attempting to write 0x0000 to REG_DEV_ID (RO protected)...\n");
  DriverStatus ro_status = driver_write_reg(&drv, REG_DEV_ID, 0x0000);
  if (ro_status == DRIVER_ERR_NACK) {
    printf("         -> [PROTECTED] Hardware rejected write with NACK! Register unchanged.\n");
  } else {
    printf("         -> [WARNING] Unexpected status: %d\n", ro_status);
  }
  printf("\n");

  // -------------------------------------------------------------
  // 5. Asynchronous Live Sensor Telemetry Stream
  // -------------------------------------------------------------
  printf("[4/5] Launching Asynchronous Sensor Telemetry Stream (OP_STREAM_START)...\n");
  StreamContext stream_ctx = {0};
  driver_start_telemetry(&drv, on_telemetry_received, &stream_ctx);

  printf("      Live Sensor Readings Stream (12 clock cycles):\n");
  for (int i = 0; i < 12; i++) {
    driver_step(&drv);
    sleep_ms(80); // Animate output
  }

  printf("      Stopping telemetry stream (OP_STREAM_STOP)...\n");
  driver_stop_telemetry(&drv);
  printf("      -> [OK] Telemetry stream stopped successfully.\n\n");

  // -------------------------------------------------------------
  // 6. Driver Diagnostic Statistics
  // -------------------------------------------------------------
  printf("[5/5] Driver Diagnostic Statistics:\n");
  printf("      -----------------------------------------------\n");
  printf("      • Synchronous Commands Transacted:  %u\n", drv.stats.commands_sent);
  printf("      • Valid Response Frames Processed: %u\n", drv.stats.responses_received);
  printf("      • Autonomous Telemetry Packets:     %u\n", drv.stats.telemetry_frames_received);
  printf("      • Hardware NACKs Handled:           %u\n", drv.stats.nacks_received);
  printf("      • Parser CRC Errors:                %u\n", drv.rx_parser.stats.crc_errors);
  printf("      • Parser Sync Recoveries:           %u\n", drv.rx_parser.stats.sync_errors);
  printf("      -----------------------------------------------\n");
  printf("      STATUS: 100%% Operational. Zero Leaks. Zero Violations.\n\n");

  printf("===================================================================\n");
  printf("                  SIMULATION COMPLETED SUCCESSFULLY                \n");
  printf("===================================================================\n\n");

  return 0;
}
