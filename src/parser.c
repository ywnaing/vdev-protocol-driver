#include "parser.h"
#include "protocol.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void parser_init(Parser *p) {

  p->bytes_collected = 0;
  ParserStats stats = {0};
  p->stats = stats;
  p->current_state = PARSE_STATE_WAIT_SYNC_1;
}

void parser_reset(Parser *p) {
  p->current_state = PARSE_STATE_WAIT_SYNC_1;
  p->bytes_collected = 0;
}

bool parser_consume_byte(Parser *p, uint8_t byte, Frame *out_frame) {
  switch (p->current_state) {
  case PARSE_STATE_WAIT_SYNC_1:
    if (byte == PROTO_SYNC_BYTE_1) {
      p->raw_buf[0] = byte;
      p->bytes_collected = 1;
      p->current_state = PARSE_STATE_WAIT_SYNC_2;
    }
    break;
  case PARSE_STATE_WAIT_SYNC_2:
    if (byte == PROTO_SYNC_BYTE_2) {
      p->raw_buf[1] = byte;
      p->bytes_collected = 2;
      p->current_state = PARSE_STATE_READ_HEADER;
    } else if (byte == 0xAA) {
    } else {
      p->stats.sync_errors += 1;
      parser_reset(p);
    }
    break;
  case PARSE_STATE_READ_HEADER:
    p->raw_buf[p->bytes_collected++] = byte;

    if (p->bytes_collected == PROTO_HEADER_LEN) {
      uint16_t len = (p->raw_buf[4] << 8) | p->raw_buf[5];
      p->payload_len = len;

      if (len == 0) {
        p->current_state = PARSE_STATE_READ_CRC;
        break;
      }

      if (len > PROTO_MAX_PAYLOAD_LEN) {
        p->stats.sync_errors += 1;
        parser_reset(p);
        break;
      }

      if (len > 0) {
        p->current_state = PARSE_STATE_READ_PAYLOAD;
        break;
      }
    }

    break;
  case PARSE_STATE_READ_PAYLOAD:
    if (p->bytes_collected != PROTO_HEADER_LEN + p->payload_len) {
      p->raw_buf[p->bytes_collected++] = byte;
    }

    if (p->bytes_collected == PROTO_HEADER_LEN + p->payload_len) {
      p->current_state = PARSE_STATE_READ_CRC;
    }
    break;
  case PARSE_STATE_READ_CRC:
    p->raw_buf[p->bytes_collected++] = byte;
    size_t total_expected = PROTO_HEADER_LEN + p->payload_len + PROTO_CRC_LEN;
    if (p->bytes_collected == total_expected) {
      // Now all bytes (including the 2 CRC bytes) are in p->raw_buf!
      if (protocol_unpack_frame(
              p->raw_buf, PROTO_HEADER_LEN + p->payload_len + PROTO_CRC_LEN,
              out_frame) == PROTO_OK) {
        p->stats.frames_received += 1;
        parser_reset(p);
        return true;
      } else {
        p->stats.crc_errors += 1;
        parser_reset(p);
        return false;
      }
    }
    break;
  default:
    break;
  }

  return false;
}

size_t parser_feed(Parser *p, const uint8_t *data, size_t len, Frame *out_frame,
                   bool *frame_ready) {
  for (size_t i = 0; i < len; i++) {
    if (parser_consume_byte(p, data[i], out_frame)) {
      *frame_ready = true;
      return i + 1;
    }
  }

  *frame_ready = false;
  return len;
}
