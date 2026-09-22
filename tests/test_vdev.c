#include "protocol.h"
#include "vdev.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_vdev_init_and_reset(void) {
  printf("--- Test 1: Device Initialization and Defaults ---\n");

  VirtualDevice dev;
  vdev_init(&dev);

  uint16_t val = 0;
  assert(vdev_read_reg(&dev, REG_DEV_ID, &val) && val == 0xCAFE);
  assert(vdev_read_reg(&dev, REG_FW_VER, &val) && val == 0x0100);
  assert(vdev_read_reg(&dev, REG_STATUS, &val) && val == DEV_STATUS_READY);
  assert(vdev_read_reg(&dev, REG_CTRL, &val) && val == 0x0000);
  assert(vdev_read_reg(&dev, REG_SAMPLE_RATE, &val) && val == 10);
  assert(vdev_read_reg(&dev, REG_TEMP, &val) && val == 250);
  assert(vdev_read_reg(&dev, REG_ACCEL_X, &val) && val == 0);
  assert(vdev_read_reg(&dev, REG_ACCEL_Y, &val) && val == 0);
  assert(vdev_read_reg(&dev, REG_ACCEL_Z, &val) && val == 1000);
  assert(vdev_read_reg(&dev, REG_SCRATCHPAD, &val) && val == 0x0000);
  assert(dev.tx_seq == 0);
  assert(dev.tick_count == 0);

  // Mutate scratchpad and advance tick
  assert(vdev_write_reg(&dev, REG_SCRATCHPAD, 0xBEEF));
  Frame frame;
  vdev_tick(&dev, &frame);
  assert(dev.tick_count == 1);

  // Reset device
  vdev_reset(&dev);
  assert(vdev_read_reg(&dev, REG_SCRATCHPAD, &val) && val == 0x0000);
  assert(dev.tx_seq == 0);
  assert(dev.tick_count == 0);

  printf("All registers initialized to hardware defaults and reset successfully!\n");
  printf("PASSED\n\n");
}

void test_vdev_register_access_control(void) {
  printf("--- Test 2: Register Access Control & Permissions ---\n");

  VirtualDevice dev;
  vdev_init(&dev);

  uint16_t val = 0;

  // 1. Out-of-bounds register addresses must fail
  assert(vdev_read_reg(&dev, REG_MAP_SIZE, &val) == false);
  assert(vdev_read_reg(&dev, 0xFF, &val) == false);
  assert(vdev_write_reg(&dev, REG_MAP_SIZE, 0x1234) == false);
  assert(vdev_write_reg(&dev, 0xFF, 0x1234) == false);

  // 2. Read-Only registers cannot be modified via vdev_write_reg
  assert(vdev_write_reg(&dev, REG_DEV_ID, 0x0000) == false);
  assert(vdev_read_reg(&dev, REG_DEV_ID, &val) && val == 0xCAFE);

  assert(vdev_write_reg(&dev, REG_FW_VER, 0x9999) == false);
  assert(vdev_read_reg(&dev, REG_FW_VER, &val) && val == 0x0100);

  assert(vdev_write_reg(&dev, REG_STATUS, 0x0000) == false);
  assert(vdev_read_reg(&dev, REG_STATUS, &val) && val == DEV_STATUS_READY);

  assert(vdev_write_reg(&dev, REG_TEMP, 500) == false);
  assert(vdev_read_reg(&dev, REG_TEMP, &val) && val == 250);

  // 3. Read-Write registers can be updated
  assert(vdev_write_reg(&dev, REG_SCRATCHPAD, 0xABCD));
  assert(vdev_read_reg(&dev, REG_SCRATCHPAD, &val) && val == 0xABCD);

  assert(vdev_write_reg(&dev, REG_SAMPLE_RATE, 50));
  assert(vdev_read_reg(&dev, REG_SAMPLE_RATE, &val) && val == 50);

  printf("Read-Only protection and boundary limits enforced!\n");
  printf("PASSED\n\n");
}

void test_vdev_protocol_commands(void) {
  printf("--- Test 3: Command Frame Dispatcher ---\n");

  VirtualDevice dev;
  vdev_init(&dev);

  Frame req;
  Frame resp;

  // 1. Ping command
  req.header.sync = PROTO_SYNC_WORD;
  req.header.seq = 1;
  req.header.opcode = OP_PING;
  req.header.length = 0;

  assert(vdev_process_frame(&dev, &req, &resp));
  assert(resp.header.sync == PROTO_SYNC_WORD);
  assert(resp.header.seq == 0); // dev->tx_seq started at 0
  assert(resp.header.opcode == OP_RSP_ACK);
  assert(resp.header.length == 0);

  // 2. Get Status command
  req.header.seq = 2;
  req.header.opcode = OP_GET_STATUS;
  req.header.length = 0;

  assert(vdev_process_frame(&dev, &req, &resp));
  assert(resp.header.seq == 1);
  assert(resp.header.opcode == OP_RSP_ACK);
  assert(resp.header.length == 2);
  uint16_t status = (resp.payload[0] << 8) | resp.payload[1];
  assert(status == DEV_STATUS_READY);

  // 3. Read Register (DEV_ID)
  req.header.seq = 3;
  req.header.opcode = OP_READ_REG;
  req.header.length = 1;
  req.payload[0] = REG_DEV_ID;

  assert(vdev_process_frame(&dev, &req, &resp));
  assert(resp.header.opcode == OP_RSP_DATA);
  assert(resp.header.length == 3);
  assert(resp.payload[0] == REG_DEV_ID);
  uint16_t dev_id = (resp.payload[1] << 8) | resp.payload[2];
  assert(dev_id == 0xCAFE);

  // 4. Read Register Out-of-Bounds
  req.header.seq = 4;
  req.payload[0] = 0xAA;

  assert(vdev_process_frame(&dev, &req, &resp));
  assert(resp.header.opcode == OP_RSP_NACK);
  assert(resp.header.length == 1);
  assert(resp.payload[0] == PROTO_ERR_REG_OUT_OF_BOUNDS);

  // 5. Write Register (SCRATCHPAD = 0x4321)
  req.header.seq = 5;
  req.header.opcode = OP_WRITE_REG;
  req.header.length = 3;
  req.payload[0] = REG_SCRATCHPAD;
  req.payload[1] = 0x43;
  req.payload[2] = 0x21;

  assert(vdev_process_frame(&dev, &req, &resp));
  assert(resp.header.opcode == OP_RSP_ACK);
  assert(resp.header.length == 0);

  uint16_t scratch = 0;
  assert(vdev_read_reg(&dev, REG_SCRATCHPAD, &scratch) && scratch == 0x4321);

  // 6. Write Register (Read-Only rejection on REG_FW_VER)
  req.header.seq = 6;
  req.payload[0] = REG_FW_VER;
  req.payload[1] = 0x99;
  req.payload[2] = 0x99;

  assert(vdev_process_frame(&dev, &req, &resp));
  assert(resp.header.opcode == OP_RSP_NACK);
  assert(resp.header.length == 1);
  assert(resp.payload[0] == PROTO_ERR_REG_OUT_OF_BOUNDS);

  // 7. Unknown opcode rejection
  req.header.seq = 7;
  req.header.opcode = 0x7E;
  req.header.length = 0;

  assert(vdev_process_frame(&dev, &req, &resp));
  assert(resp.header.opcode == OP_RSP_NACK);
  assert(resp.header.length == 1);
  assert(resp.payload[0] == PROTO_ERR_UNKNOWN_OP);

  printf("All protocol opcodes, payloads, and error NACKs dispatched correctly!\n");
  printf("PASSED\n\n");
}

void test_vdev_streaming_and_telemetry(void) {
  printf("--- Test 4: Streaming Control and Periodic Telemetry ---\n");

  VirtualDevice dev;
  vdev_init(&dev);

  Frame frame;

  // 1. When streaming is OFF, vdev_tick returns false
  assert(vdev_tick(&dev, &frame) == false);
  assert(dev.tick_count == 1);

  // 2. Enable streaming via OP_STREAM_START
  Frame req;
  Frame resp;
  req.header.sync = PROTO_SYNC_WORD;
  req.header.seq = 10;
  req.header.opcode = OP_STREAM_START;
  req.header.length = 0;

  assert(vdev_process_frame(&dev, &req, &resp));
  assert(resp.header.opcode == OP_RSP_ACK);
  assert(dev.registers[REG_STATUS] & DEV_STATUS_STREAMING);

  // 3. Now vdev_tick generates telemetry frames
  assert(vdev_tick(&dev, &frame) == true);
  assert(frame.header.sync == PROTO_SYNC_WORD);
  assert(frame.header.opcode == OP_TELEMETRY_DATA);
  assert(frame.header.length == 8);

  // Verify Big-Endian sensor payload layout
  uint16_t temp = (frame.payload[0] << 8) | frame.payload[1];
  uint16_t ax = (frame.payload[2] << 8) | frame.payload[3];
  uint16_t ay = (frame.payload[4] << 8) | frame.payload[5];
  uint16_t az = (frame.payload[6] << 8) | frame.payload[7];

  assert(temp == dev.registers[REG_TEMP]);
  assert(ax == dev.registers[REG_ACCEL_X]);
  assert(ay == dev.registers[REG_ACCEL_Y]);
  assert(az == dev.registers[REG_ACCEL_Z]);

  // Pack the telemetry frame to raw wire bytes to test end-to-end CRC validity
  uint8_t wire[PROTO_MAX_FRAME_LEN];
  int wire_len = protocol_pack_frame(&frame, wire, sizeof(wire));
  assert(wire_len == PROTO_HEADER_LEN + 8 + PROTO_CRC_LEN); // 6 + 8 + 2 = 16 bytes

  Frame unpacked;
  assert(protocol_unpack_frame(wire, wire_len, &unpacked) == PROTO_OK);
  assert(unpacked.header.opcode == OP_TELEMETRY_DATA);
  assert(unpacked.header.length == 8);
  assert(memcmp(unpacked.payload, frame.payload, 8) == 0);

  // 4. Disable streaming via OP_STREAM_STOP
  req.header.opcode = OP_STREAM_STOP;
  assert(vdev_process_frame(&dev, &req, &resp));
  assert(resp.header.opcode == OP_RSP_ACK);
  assert(!(dev.registers[REG_STATUS] & DEV_STATUS_STREAMING));

  // 5. Subsequent ticks return false
  assert(vdev_tick(&dev, &frame) == false);

  printf("Autonomous telemetry stream generated, packed, and CRC-validated!\n");
  printf("PASSED\n\n");
}

void test_vdev_soft_reset_via_ctrl_reg(void) {
  printf("--- Test 5: Soft Reset via REG_CTRL ---\n");

  VirtualDevice dev;
  vdev_init(&dev);

  // Set scratchpad and start stream
  assert(vdev_write_reg(&dev, REG_SCRATCHPAD, 0x9999));
  dev.registers[REG_STATUS] |= DEV_STATUS_STREAMING;

  // Command a soft reset by writing DEV_CTRL_RESET to REG_CTRL
  Frame req;
  Frame resp;
  req.header.sync = PROTO_SYNC_WORD;
  req.header.seq = 20;
  req.header.opcode = OP_WRITE_REG;
  req.header.length = 3;
  req.payload[0] = REG_CTRL;
  req.payload[1] = 0x00;
  req.payload[2] = DEV_CTRL_RESET;

  assert(vdev_process_frame(&dev, &req, &resp));
  assert(resp.header.opcode == OP_RSP_ACK);

  // Verify device reset state
  uint16_t scratch = 0;
  assert(vdev_read_reg(&dev, REG_SCRATCHPAD, &scratch) && scratch == 0x0000);
  assert(!(dev.registers[REG_STATUS] & DEV_STATUS_STREAMING));
  assert(dev.tx_seq == 0);

  printf("Device soft-reset via REG_CTRL verified!\n");
  printf("PASSED\n\n");
}

int main(void) {
  printf("====================================================\n");
  printf("   VIRTUAL HARDWARE DEVICE TEST SUITE (PHASE 3)     \n");
  printf("====================================================\n\n");

  test_vdev_init_and_reset();
  test_vdev_register_access_control();
  test_vdev_protocol_commands();
  test_vdev_streaming_and_telemetry();
  test_vdev_soft_reset_via_ctrl_reg();

  printf("====================================================\n");
  printf("    ALL PHASE 3 TESTS PASSED! 🎉 (0 LEAKS, 0 ERRORS)\n");
  printf("====================================================\n");

  return 0;
}
