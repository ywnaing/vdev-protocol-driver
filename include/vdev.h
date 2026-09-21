#ifndef VDEV_H
#define VDEV_H

#include "protocol.h"
#include <stdbool.h>
#include <stdint.h>

// -------------------------------------------------------------
// Register Addresses
// -------------------------------------------------------------
#define REG_DEV_ID 0x00
#define REG_FW_VER 0x01
#define REG_STATUS 0x02
#define REG_CTRL 0x03
#define REG_SAMPLE_RATE 0x04
#define REG_TEMP 0x05
#define REG_ACCEL_X 0x06
#define REG_ACCEL_Y 0x07
#define REG_ACCEL_Z 0x08
#define REG_SCRATCHPAD 0x09
#define REG_MAP_SIZE 10

// -------------------------------------------------------------
// Status & Control Bitflags
// -------------------------------------------------------------
#define DEV_STATUS_READY (1 << 0)
#define DEV_STATUS_BUSY (1 << 1)
#define DEV_STATUS_STREAMING (1 << 2)
#define DEV_STATUS_ERROR (1 << 3)

#define DEV_CTRL_ENABLE (1 << 0)
#define DEV_CTRL_RESET (1 << 1)
#define DEV_CTRL_STREAM_ON (1 << 2)

// -------------------------------------------------------------
// Register Metadata & Device Struct
// -------------------------------------------------------------
typedef enum { PERM_RO, PERM_RW } RegPerm;

typedef struct {
  uint8_t address;
  const char *name;
  RegPerm permission;
  uint16_t default_val;
} RegisterDef;

typedef struct {
  uint16_t registers[REG_MAP_SIZE];
  uint8_t tx_seq;
  uint32_t tick_count;
} VirtualDevice;

// -------------------------------------------------------------
// Device APIs
// -------------------------------------------------------------
void vdev_init(VirtualDevice *dev);
void vdev_reset(VirtualDevice *dev);

bool vdev_read_reg(const VirtualDevice *dev, uint8_t addr, uint16_t *out_val);
bool vdev_write_reg(VirtualDevice *dev, uint8_t addr, uint16_t val);

// Process an incoming request frame and generate an outgoing response frame
bool vdev_process_frame(VirtualDevice *dev, const Frame *req, Frame *resp);

// Advance physics clock by 1 tick; returns true if a telemetry frame was
// generated
bool vdev_tick(VirtualDevice *dev, Frame *telemetry_frame);

#endif // VDEV_H
