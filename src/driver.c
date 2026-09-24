#include "driver.h"
#include "parser.h"
#include "protocol.h"
#include "vdev.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Private Helper: Loopback Transaction Engine
// ---------------------------------------------------------------------------
// In a production operating system or embedded firmware, this function represents
// writing bytes to a hardware UART/SPI/PCIe FIFO and reading back the response bytes.
//
// To enforce strict hardware boundaries and ensure Phase 1 (Protocol/CRC) and
// Phase 2 (Streaming Parser) are fully validated end-to-end:
// 1. Driver serializes the Request Frame into raw Big-Endian wire bytes.
// 2. Simulated Bus delivers wire bytes to the VirtualDevice.
// 3. VirtualDevice processes the request and serializes its Response Frame to wire bytes.
// 4. Driver feeds Response wire bytes into its internal rx_parser, verifying CRC-16.
static DriverStatus driver_transact(Driver *drv, const Frame *req, Frame *resp) {
  if (!drv || !drv->vdev || !req || !resp) {
    return DRIVER_ERR_NULL_ARG;
  }

  drv->stats.commands_sent++;

  // Step 1: Serialize request frame into raw wire bytes with CRC-16
  uint8_t tx_wire[PROTO_MAX_FRAME_LEN];
  int tx_len = protocol_pack_frame(req, tx_wire, sizeof(tx_wire));
  if (tx_len <= 0) {
    return DRIVER_ERR_IO;
  }

  // Step 2: Simulated Wire -> VirtualDevice
  // The device hardware decodes the wire bytes (verifying sync and CRC)
  Frame dev_req;
  if (protocol_unpack_frame(tx_wire, (size_t)tx_len, &dev_req) != PROTO_OK) {
    return DRIVER_ERR_IO;
  }

  // Execute internal peripheral logic
  Frame dev_resp;
  if (!vdev_process_frame(drv->vdev, &dev_req, &dev_resp)) {
    return DRIVER_ERR_IO;
  }

  // Step 3: VirtualDevice -> Simulated Wire
  // The device serializes its response frame into raw wire bytes with CRC-16
  uint8_t rx_wire[PROTO_MAX_FRAME_LEN];
  int rx_len = protocol_pack_frame(&dev_resp, rx_wire, sizeof(rx_wire));
  if (rx_len <= 0) {
    return DRIVER_ERR_IO;
  }

  // Step 4: Simulated Wire -> Driver RX Parser
  // The driver parses the incoming stream byte-by-byte using its streaming FSM
  bool frame_ready = false;
  for (int i = 0; i < rx_len; i++) {
    if (parser_consume_byte(&drv->rx_parser, rx_wire[i], resp)) {
      frame_ready = true;
      break;
    }
  }

  if (!frame_ready) {
    return DRIVER_ERR_CRC;
  }

  drv->stats.responses_received++;

  // Check if device rejected the command with a NACK
  if (resp->header.opcode == OP_RSP_NACK) {
    drv->stats.nacks_received++;
    return DRIVER_ERR_NACK;
  }

  return DRIVER_OK;
}

// ---------------------------------------------------------------------------
// Public Driver APIs
// ---------------------------------------------------------------------------

// Initialize driver instance, bind to virtual peripheral, and reset FSM/stats
DriverStatus driver_init(Driver *drv, VirtualDevice *vdev) {
  if (!drv || !vdev) {
    return DRIVER_ERR_NULL_ARG;
  }

  drv->vdev = vdev;
  parser_init(&drv->rx_parser);
  drv->next_seq = 0;
  drv->telemetry_cb = NULL;
  drv->cb_user_data = NULL;
  memset(&drv->stats, 0, sizeof(drv->stats));

  return DRIVER_OK;
}

// Ping hardware peripheral to verify bus connectivity and alive status
DriverStatus driver_ping(Driver *drv) {
  if (!drv) {
    return DRIVER_ERR_NULL_ARG;
  }

  Frame req;
  req.header.sync = PROTO_SYNC_WORD;
  req.header.seq = drv->next_seq++;
  req.header.opcode = OP_PING;
  req.header.length = 0;

  Frame resp;
  DriverStatus status = driver_transact(drv, &req, &resp);
  if (status != DRIVER_OK) {
    return status;
  }

  // A successful ping must receive OP_RSP_ACK with empty payload (length 0)
  if (resp.header.opcode != OP_RSP_ACK || resp.header.length != 0) {
    return DRIVER_ERR_UNEXPECTED_RESP;
  }

  return DRIVER_OK;
}

// Read a 16-bit register from the hardware peripheral
DriverStatus driver_read_reg(Driver *drv, uint8_t addr, uint16_t *out_val) {
  if (!drv || !out_val) {
    return DRIVER_ERR_NULL_ARG;
  }

  Frame req;
  req.header.sync = PROTO_SYNC_WORD;
  req.header.seq = drv->next_seq++;
  req.header.opcode = OP_READ_REG;
  req.header.length = 1;
  req.payload[0] = addr;

  Frame resp;
  DriverStatus status = driver_transact(drv, &req, &resp);
  if (status != DRIVER_OK) {
    return status;
  }

  // Expected response: OP_RSP_DATA with 3-byte payload [addr, high_byte, low_byte]
  if (resp.header.opcode != OP_RSP_DATA || resp.header.length != 3) {
    return DRIVER_ERR_UNEXPECTED_RESP;
  }

  // Validate that the returned register address matches what we requested
  if (resp.payload[0] != addr) {
    return DRIVER_ERR_UNEXPECTED_RESP;
  }

  // Decode Big-Endian 16-bit value from payload bytes 1 and 2
  *out_val = (uint16_t)(((uint16_t)resp.payload[1] << 8) | (uint16_t)resp.payload[2]);

  return DRIVER_OK;
}

// Write a 16-bit value to a read-write register on the peripheral
DriverStatus driver_write_reg(Driver *drv, uint8_t addr, uint16_t val) {
  if (!drv) {
    return DRIVER_ERR_NULL_ARG;
  }

  Frame req;
  req.header.sync = PROTO_SYNC_WORD;
  req.header.seq = drv->next_seq++;
  req.header.opcode = OP_WRITE_REG;
  req.header.length = 3;
  req.payload[0] = addr;
  req.payload[1] = (uint8_t)((val >> 8) & 0xFF); // Big-Endian High Byte
  req.payload[2] = (uint8_t)(val & 0xFF);        // Big-Endian Low Byte

  Frame resp;
  DriverStatus status = driver_transact(drv, &req, &resp);
  if (status != DRIVER_OK) {
    return status;
  }

  // A successful register write receives an empty OP_RSP_ACK
  if (resp.header.opcode != OP_RSP_ACK || resp.header.length != 0) {
    return DRIVER_ERR_UNEXPECTED_RESP;
  }

  return DRIVER_OK;
}

// Enable periodic telemetry streaming and register the asynchronous callback
DriverStatus driver_start_telemetry(Driver *drv, TelemetryCallback cb, void *user_data) {
  if (!drv || !cb) {
    return DRIVER_ERR_NULL_ARG;
  }

  drv->telemetry_cb = cb;
  drv->cb_user_data = user_data;

  Frame req;
  req.header.sync = PROTO_SYNC_WORD;
  req.header.seq = drv->next_seq++;
  req.header.opcode = OP_STREAM_START;
  req.header.length = 0;

  Frame resp;
  DriverStatus status = driver_transact(drv, &req, &resp);
  if (status != DRIVER_OK) {
    return status;
  }

  if (resp.header.opcode != OP_RSP_ACK || resp.header.length != 0) {
    return DRIVER_ERR_UNEXPECTED_RESP;
  }

  return DRIVER_OK;
}

// Stop periodic telemetry streaming and unregister the callback
DriverStatus driver_stop_telemetry(Driver *drv) {
  if (!drv) {
    return DRIVER_ERR_NULL_ARG;
  }

  Frame req;
  req.header.sync = PROTO_SYNC_WORD;
  req.header.seq = drv->next_seq++;
  req.header.opcode = OP_STREAM_STOP;
  req.header.length = 0;

  Frame resp;
  DriverStatus status = driver_transact(drv, &req, &resp);
  if (status != DRIVER_OK) {
    return status;
  }

  if (resp.header.opcode != OP_RSP_ACK || resp.header.length != 0) {
    return DRIVER_ERR_UNEXPECTED_RESP;
  }

  drv->telemetry_cb = NULL;
  drv->cb_user_data = NULL;

  return DRIVER_OK;
}

// Hardware time-step simulation & wire polling
// Simulates passing of physical time:
// 1. Advances virtual peripheral clock via vdev_tick().
// 2. If telemetry is emitted, serializes it onto the wire.
// 3. Feeds wire bytes into rx_parser and dispatches to registered callback.
DriverStatus driver_step(Driver *drv) {
  if (!drv || !drv->vdev) {
    return DRIVER_ERR_NULL_ARG;
  }

  Frame telem_frame;
  bool has_telemetry = vdev_tick(drv->vdev, &telem_frame);

  // If streaming is disabled, no frame is placed on the wire
  if (!has_telemetry) {
    return DRIVER_OK;
  }

  // Hardware serializes unsolicited telemetry packet onto the wire
  uint8_t wire_buf[PROTO_MAX_FRAME_LEN];
  int wire_len = protocol_pack_frame(&telem_frame, wire_buf, sizeof(wire_buf));
  if (wire_len <= 0) {
    return DRIVER_ERR_IO;
  }

  // Driver consumes wire bytes using its streaming FSM parser
  Frame parsed_frame;
  bool frame_ready = false;
  for (int i = 0; i < wire_len; i++) {
    if (parser_consume_byte(&drv->rx_parser, wire_buf[i], &parsed_frame)) {
      frame_ready = true;
      break;
    }
  }

  if (!frame_ready) {
    return DRIVER_ERR_CRC;
  }

  // Validate opcode and telemetry payload size (4 x 16-bit values = 8 bytes)
  if (parsed_frame.header.opcode != OP_TELEMETRY_DATA || parsed_frame.header.length != 8) {
    return DRIVER_ERR_UNEXPECTED_RESP;
  }

  // Decode raw sensor readings from Big-Endian payload
  uint16_t raw_temp = (uint16_t)(((uint16_t)parsed_frame.payload[0] << 8) | (uint16_t)parsed_frame.payload[1]);
  int16_t accel_x   = (int16_t)(((uint16_t)parsed_frame.payload[2] << 8) | (uint16_t)parsed_frame.payload[3]);
  int16_t accel_y   = (int16_t)(((uint16_t)parsed_frame.payload[4] << 8) | (uint16_t)parsed_frame.payload[5]);
  int16_t accel_z   = (int16_t)(((uint16_t)parsed_frame.payload[6] << 8) | (uint16_t)parsed_frame.payload[7]);

  drv->stats.telemetry_frames_received++;

  // Convert raw sensor integers into human-readable engineering units
  if (drv->telemetry_cb) {
    DriverTelemetry telem;
    telem.raw_temp = raw_temp;
    telem.temp_celsius = (float)raw_temp / 10.0f; // e.g. 250 -> 25.0 °C
    telem.accel_x = accel_x;
    telem.accel_y = accel_y;
    telem.accel_z = accel_z;

    // Trigger asynchronous application callback
    drv->telemetry_cb(&telem, drv->cb_user_data);
  }

  return DRIVER_OK;
}
