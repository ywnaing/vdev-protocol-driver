#include "vdev.h"
#include "protocol.h"
#include <stdint.h>

const RegisterDef register_definitions[REG_MAP_SIZE] = {
    {REG_DEV_ID, "REG_DEV_ID", PERM_RO, 0xCAFE},
    {REG_FW_VER, "REG_FW_VER", PERM_RO, 0x0100},
    {REG_STATUS, "REG_STATUS", PERM_RO, 0x0001},
    {REG_CTRL, "REG_CTRL", PERM_RW, 0x0000},
    {REG_SAMPLE_RATE, "REG_SAMPLE_RATE", PERM_RW, 10},
    {REG_TEMP, "REG_TEMP", PERM_RO, 250}, // 25.0°C
    {REG_ACCEL_X, "REG_ACCEL_X", PERM_RO, 0},
    {REG_ACCEL_Y, "REG_ACCEL_Y", PERM_RO, 0},
    {REG_ACCEL_Z, "REG_ACCEL_Z", PERM_RO, 1000}, // 1.0g
    {REG_SCRATCHPAD, "REG_SCRATCHPAD", PERM_RW, 0x0000}};

void vdev_init(VirtualDevice *dev) { vdev_reset(dev); }

bool vdev_read_reg(const VirtualDevice *dev, uint8_t addr, uint16_t *out_val) {

  if (addr >= REG_MAP_SIZE) {
    return false;
  }

  *out_val = dev->registers[addr];

  return true;
}

bool vdev_write_reg(VirtualDevice *dev, uint8_t addr, uint16_t val) {
  if (addr >= REG_MAP_SIZE) {
    return false;
  }

  if (register_definitions[addr].permission == PERM_RO) {
    return false;
  }

  dev->registers[addr] = val;
  return true;
}

void vdev_reset(VirtualDevice *dev) {
  if (!dev)
    return;
  for (int i = 0; i < REG_MAP_SIZE; i++) {
    dev->registers[i] = register_definitions[i].default_val;
  }
  dev->tx_seq = 0;
  dev->tick_count = 0;
}

bool vdev_process_frame(VirtualDevice *dev, const Frame *req, Frame *resp) {
  resp->header.sync = PROTO_SYNC_WORD;
  resp->header.seq = dev->tx_seq++;

  switch (req->header.opcode) {
  case OP_PING:
    resp->header.opcode = OP_RSP_ACK;
    resp->header.length = 0;
    return true;
  case OP_RESET:
    vdev_reset(dev);
    resp->header.opcode = OP_RSP_ACK;
    resp->header.length = 0;
    return true;
  case OP_GET_STATUS: {
    uint16_t status_val = dev->registers[REG_STATUS];
    resp->header.opcode = OP_RSP_ACK;

    resp->payload[0] = (status_val >> 8) & 0xFF;
    resp->payload[1] = status_val & 0xFF;

    resp->header.length = 2;
    return true;
  }
  case OP_READ_REG: {
    uint8_t addr = req->payload[0];

    uint16_t out_val;
    if (vdev_read_reg(dev, addr, &out_val)) {
      resp->header.opcode = OP_RSP_DATA;

      resp->payload[0] = addr;
      resp->payload[1] = (out_val >> 8) & 0xFF;
      resp->payload[2] = out_val & 0xFF;

      resp->header.length = 3;
    } else {
      resp->header.opcode = OP_RSP_NACK;
      resp->header.length = 1;
      resp->payload[0] = PROTO_ERR_REG_OUT_OF_BOUNDS;
    }
    return true;
  }
  case OP_WRITE_REG: {
    uint8_t addr = req->payload[0];
    uint8_t high = req->payload[1];
    uint8_t low = req->payload[2];

    uint16_t val = (high << 8) | low;

    if (vdev_write_reg(dev, addr, val)) {
      if (addr == REG_CTRL) {
        if (val == DEV_CTRL_RESET) {
          vdev_reset(dev);
        } else if (val == DEV_CTRL_STREAM_ON) {
          dev->registers[REG_STATUS] |= DEV_STATUS_STREAMING;
        }
      }
      resp->header.opcode = OP_RSP_ACK;
      resp->header.length = 0;
    } else {
      resp->header.opcode = OP_RSP_NACK;
      resp->payload[0] = PROTO_ERR_REG_OUT_OF_BOUNDS;
      resp->header.length = 1;
    }

    return true;
  }
  case OP_STREAM_START:
    dev->registers[REG_STATUS] |= DEV_STATUS_STREAMING;
    resp->header.opcode = OP_RSP_ACK;
    resp->header.length = 0;
    return true;

  case OP_STREAM_STOP:
    dev->registers[REG_STATUS] &= ~DEV_STATUS_STREAMING;
    resp->header.opcode = OP_RSP_ACK;
    resp->header.length = 0;
    return true;
  default:
    resp->header.opcode = OP_RSP_NACK;
    resp->payload[0] = PROTO_ERR_UNKNOWN_OP;
    resp->header.length = 1;
    return true;
  }
}
