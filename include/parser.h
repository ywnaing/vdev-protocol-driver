#ifndef PARSER_H
#define PARSER_H
#include "protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  PARSE_STATE_WAIT_SYNC_1,  // Waiting for 0xAA
  PARSE_STATE_WAIT_SYNC_2,  // Waiting for 0x55
  PARSE_STATE_READ_HEADER,  // Reading Seq, Opcode, Length (4 bytes)
  PARSE_STATE_READ_PAYLOAD, // Reading variable payload (N bytes)
  PARSE_STATE_READ_CRC      // Reading CRC-16 (2 bytes)
} ParserState;

typedef struct {
  uint32_t frames_received;
  uint32_t crc_errors;
  uint32_t sync_errors;
} ParserStats;

typedef struct {
  ParserState current_state;
  uint8_t raw_buf[PROTO_MAX_FRAME_LEN];
  uint16_t bytes_collected;
  uint16_t payload_len;
  ParserStats stats;
} Parser;

void parser_init(Parser *p);
void parser_reset(Parser *p);
bool parser_consume_byte(Parser *p, uint8_t byte, Frame *out_frame);
size_t parser_feed(Parser *p, const uint8_t *data, size_t len, Frame *out_frame,
                   bool *frame_ready);

#endif // PARSER_H
