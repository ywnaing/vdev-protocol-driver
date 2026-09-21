#include "protocol.h"
#include "crc16.h"
#include <string.h>

// -------------------------------------------------------------
// Safe Endian-Independent Packing Helpers (Big-Endian)
// -------------------------------------------------------------
static inline void write_u16_be(uint8_t *buf, uint16_t val) {
  buf[0] = (uint8_t)((val >> 8) & 0xFF);
  buf[1] = (uint8_t)(val & 0xFF);
}

static inline uint16_t read_u16_be(const uint8_t *buf) {
  return (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
}

// -------------------------------------------------------------
// Frame Packing (Serialization)
// -------------------------------------------------------------
int protocol_pack_frame(const Frame *frame, uint8_t *out_buf, size_t out_cap) {
  if (!frame || !out_buf) {
    return -PROTO_ERR_BUFFER_SMALL;
  }

  uint16_t payload_len = frame->header.length;
  if (payload_len > PROTO_MAX_PAYLOAD_LEN) {
    return -PROTO_ERR_PAYLOAD_TOO_LARGE;
  }

  size_t total_len = PROTO_HEADER_LEN + payload_len + PROTO_CRC_LEN;
  if (out_cap < total_len) {
    return -PROTO_ERR_BUFFER_SMALL;
  }

  // 1. Pack Header (Big-Endian)
  write_u16_be(&out_buf[0], PROTO_SYNC_WORD);
  out_buf[2] = frame->header.seq;
  out_buf[3] = frame->header.opcode;
  write_u16_be(&out_buf[4], payload_len);

  // 2. Pack Payload
  if (payload_len > 0) {
    memcpy(&out_buf[PROTO_HEADER_LEN], frame->payload, payload_len);
  }

  // 3. Compute and Pack CRC-16 across Header + Payload
  size_t data_len_to_crc = PROTO_HEADER_LEN + payload_len;
  uint16_t computed_crc = crc16_ccitt(out_buf, data_len_to_crc);
  write_u16_be(&out_buf[data_len_to_crc], computed_crc);

  return (int)total_len;
}

// -------------------------------------------------------------
// Header Unpacking (Deserialization)
// -------------------------------------------------------------
ProtoStatus protocol_unpack_header(const uint8_t *in_buf, size_t in_len, FrameHeader *out_header) {
  if (!in_buf || !out_header || in_len < PROTO_HEADER_LEN) {
    return PROTO_ERR_BUFFER_SMALL;
  }

  uint16_t sync = read_u16_be(&in_buf[0]);
  if (sync != PROTO_SYNC_WORD) {
    return PROTO_ERR_BAD_SYNC;
  }

  uint16_t len = read_u16_be(&in_buf[4]);
  if (len > PROTO_MAX_PAYLOAD_LEN) {
    return PROTO_ERR_PAYLOAD_TOO_LARGE;
  }

  out_header->sync = sync;
  out_header->seq = in_buf[2];
  out_header->opcode = in_buf[3];
  out_header->length = len;

  return PROTO_OK;
}

// -------------------------------------------------------------
// Full Frame Unpacking & CRC Verification
// -------------------------------------------------------------
ProtoStatus protocol_unpack_frame(const uint8_t *in_buf, size_t in_len, Frame *out_frame) {
  if (!in_buf || !out_frame) {
    return PROTO_ERR_BUFFER_SMALL;
  }

  // 1. Unpack and validate header
  FrameHeader header;
  ProtoStatus status = protocol_unpack_header(in_buf, in_len, &header);
  if (status != PROTO_OK) {
    return status;
  }

  size_t total_expected = PROTO_HEADER_LEN + header.length + PROTO_CRC_LEN;
  if (in_len < total_expected) {
    return PROTO_ERR_BUFFER_SMALL; // Frame not completely received yet
  }

  // 2. Verify CRC-16
  size_t data_len = PROTO_HEADER_LEN + header.length;
  uint16_t wire_crc = read_u16_be(&in_buf[data_len]);
  uint16_t computed_crc = crc16_ccitt(in_buf, data_len);

  if (wire_crc != computed_crc) {
    return PROTO_ERR_BAD_CRC;
  }

  // 3. Populate Frame Struct
  out_frame->header = header;
  if (header.length > 0) {
    memcpy(out_frame->payload, &in_buf[PROTO_HEADER_LEN], header.length);
  }
  out_frame->crc = wire_crc;

  return PROTO_OK;
}
