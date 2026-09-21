#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// -------------------------------------------------------------
// Protocol Constants & Limits
// -------------------------------------------------------------
#define PROTO_SYNC_BYTE_1   0xAA
#define PROTO_SYNC_BYTE_2   0x55
#define PROTO_SYNC_WORD     0xAA55

#define PROTO_HEADER_LEN    6   // Sync (2) + Seq (1) + Opcode (1) + Len (2)
#define PROTO_CRC_LEN       2   // CRC-16 (2)
#define PROTO_MIN_FRAME_LEN (PROTO_HEADER_LEN + PROTO_CRC_LEN) // 8 bytes

#define PROTO_MAX_PAYLOAD_LEN 256
#define PROTO_MAX_FRAME_LEN   (PROTO_MIN_FRAME_LEN + PROTO_MAX_PAYLOAD_LEN) // 264 bytes

// -------------------------------------------------------------
// Opcodes / Message Types
// -------------------------------------------------------------
typedef enum {
  // 1. Core System Commands (0x01 - 0x1F)
  OP_PING           = 0x01,
  OP_RESET          = 0x02,
  OP_GET_STATUS     = 0x03,
  OP_SET_CONFIG     = 0x04,

  // 2. Register Read/Write Operations (0x20 - 0x3F)
  OP_READ_REG       = 0x20,
  OP_WRITE_REG      = 0x21,

  // 3. Telemetry & Streaming (0x40 - 0x5F)
  OP_STREAM_START   = 0x40,
  OP_STREAM_STOP    = 0x41,
  OP_TELEMETRY_DATA = 0x42,

  // 4. Device Responses (0x80 - 0xFF)
  OP_RSP_ACK        = 0x80,
  OP_RSP_NACK       = 0x81,
  OP_RSP_DATA       = 0x82,
  OP_RSP_STATUS     = 0x83
} Opcode;

// -------------------------------------------------------------
// Protocol Error / Status Codes (Used in NACK / Status Responses)
// -------------------------------------------------------------
typedef enum {
  PROTO_OK                = 0x00,
  PROTO_ERR_BAD_SYNC      = 0x01,
  PROTO_ERR_BAD_CRC       = 0x02,
  PROTO_ERR_UNKNOWN_OP    = 0x03,
  PROTO_ERR_PAYLOAD_TOO_LARGE = 0x04,
  PROTO_ERR_REG_OUT_OF_BOUNDS = 0x05,
  PROTO_ERR_DEVICE_BUSY   = 0x06,
  PROTO_ERR_BUFFER_SMALL  = 0x07
} ProtoStatus;

// -------------------------------------------------------------
// Logical In-Memory Representations (Decoupled from wire byte order)
// -------------------------------------------------------------
typedef struct {
  uint16_t sync;
  uint8_t seq;
  uint8_t opcode;
  uint16_t length;
} FrameHeader;

typedef struct {
  FrameHeader header;
  uint8_t payload[PROTO_MAX_PAYLOAD_LEN];
  uint16_t crc;
} Frame;

// -------------------------------------------------------------
// Big-Endian Wire Encoding & Decoding API
// -------------------------------------------------------------

/**
 * Serializes a logical Frame into a wire-ready Big-Endian byte buffer.
 * Automatically computes and appends the CRC-16 checksum at the end.
 *
 * @param frame       Pointer to source frame.
 * @param out_buf     Destination buffer for raw bytes.
 * @param out_cap     Capacity of destination buffer.
 * @return Total bytes written, or negative ProtoStatus code on failure.
 */
int protocol_pack_frame(const Frame *frame, uint8_t *out_buf, size_t out_cap);

/**
 * Deserializes raw wire bytes into a FrameHeader struct.
 * Validates sync word and maximum payload bounds.
 *
 * @param in_buf      Buffer containing at least PROTO_HEADER_LEN bytes.
 * @param in_len      Length of available bytes in buffer.
 * @param out_header  Pointer to output header struct.
 * @return PROTO_OK on success, or ProtoStatus error code.
 */
ProtoStatus protocol_unpack_header(const uint8_t *in_buf, size_t in_len, FrameHeader *out_header);

/**
 * Deserializes a full frame from raw wire bytes, verifying the CRC-16.
 *
 * @param in_buf      Buffer containing the full frame.
 * @param in_len      Total length of the frame buffer.
 * @param out_frame   Pointer to output Frame struct.
 * @return PROTO_OK on success, or ProtoStatus error code (e.g. PROTO_ERR_BAD_CRC).
 */
ProtoStatus protocol_unpack_frame(const uint8_t *in_buf, size_t in_len, Frame *out_frame);

#endif // PROTOCOL_H
