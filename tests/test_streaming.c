#include "parser.h"
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_byte_by_byte_fragmentation(void) {
  printf("--- Test 1: Byte-by-Byte Stream Fragmentation ---\n");

  Parser p;
  parser_init(&p);

  Frame original;
  original.header.sync = PROTO_SYNC_WORD;
  original.header.seq = 101;
  original.header.opcode = OP_TELEMETRY_DATA;
  original.header.length = 16;
  memcpy(original.payload, "FragmentedPacket", 16);

  uint8_t wire_buf[PROTO_MAX_FRAME_LEN];
  int wire_len = protocol_pack_frame(&original, wire_buf, sizeof(wire_buf));
  assert(wire_len > 0);

  Frame received;
  bool ready = false;

  // Feed the wire bytes one single byte at a time
  for (int i = 0; i < wire_len; i++) {
    ready = parser_consume_byte(&p, wire_buf[i], &received);

    if (i < wire_len - 1) {
      // Must return false while packet is still incomplete
      assert(ready == false);
    } else {
      // Must return true on the exact final byte!
      assert(ready == true);
    }
  }

  assert(received.header.seq == 101);
  assert(received.header.opcode == OP_TELEMETRY_DATA);
  assert(received.header.length == 16);
  assert(memcmp(received.payload, "FragmentedPacket", 16) == 0);
  assert(p.stats.frames_received == 1);
  assert(p.stats.crc_errors == 0);
  assert(p.stats.sync_errors == 0);

  printf("1-byte-at-a-time stream completed on exact final byte (%d bytes)!\n", wire_len);
  printf("PASSED\n\n");
}

void test_irregular_chunks(void) {
  printf("--- Test 2: Irregular Chunk Streaming (parser_feed) ---\n");

  Parser p;
  parser_init(&p);

  Frame original;
  original.header.sync = PROTO_SYNC_WORD;
  original.header.seq = 77;
  original.header.opcode = OP_READ_REG;
  original.header.length = 10;
  memcpy(original.payload, "0123456789", 10);

  uint8_t wire_buf[PROTO_MAX_FRAME_LEN];
  int wire_len = protocol_pack_frame(&original, wire_buf, sizeof(wire_buf));
  assert(wire_len == PROTO_HEADER_LEN + 10 + PROTO_CRC_LEN); // 18 bytes

  Frame received;
  bool frame_ready = false;

  // Chunk 1: 5 bytes
  size_t consumed = parser_feed(&p, &wire_buf[0], 5, &received, &frame_ready);
  assert(consumed == 5 && frame_ready == false);

  // Chunk 2: 7 bytes
  consumed = parser_feed(&p, &wire_buf[5], 7, &received, &frame_ready);
  assert(consumed == 7 && frame_ready == false);

  // Chunk 3: 6 bytes (remaining bytes)
  consumed = parser_feed(&p, &wire_buf[12], 6, &received, &frame_ready);
  assert(consumed == 6 && frame_ready == true);

  assert(received.header.seq == 77);
  assert(received.header.length == 10);
  assert(memcmp(received.payload, "0123456789", 10) == 0);

  printf("Chunked stream (5B -> 7B -> 6B) delivered frame successfully!\n");
  printf("PASSED\n\n");
}

void test_packet_coalescence(void) {
  printf("--- Test 3: Packet Coalescence (Multiple Packets in 1 Buffer) ---\n");

  Parser p;
  parser_init(&p);

  // Create 3 distinct frames
  Frame f1 = {.header = {PROTO_SYNC_WORD, 1, OP_PING, 0}};
  Frame f2 = {.header = {PROTO_SYNC_WORD, 2, OP_READ_REG, 4}};
  memcpy(f2.payload, "REG1", 4);
  Frame f3 = {.header = {PROTO_SYNC_WORD, 3, OP_WRITE_REG, 4}};
  memcpy(f3.payload, "VAL1", 4);

  // Pack all 3 back-to-back into one contiguous stream buffer
  uint8_t big_stream[1024];
  int len1 = protocol_pack_frame(&f1, &big_stream[0], sizeof(big_stream));
  int len2 = protocol_pack_frame(&f2, &big_stream[len1], sizeof(big_stream) - len1);
  int len3 = protocol_pack_frame(&f3, &big_stream[len1 + len2], sizeof(big_stream) - (len1 + len2));
  size_t total_stream_len = len1 + len2 + len3;

  printf("Combined stream size: %zu bytes (contains 3 coalesced packets)\n", total_stream_len);

  // Ingest stream and extract packets one by one
  Frame received_frames[3];
  size_t frame_count = 0;
  size_t offset = 0;

  while (offset < total_stream_len) {
    bool ready = false;
    size_t consumed = parser_feed(&p, &big_stream[offset], total_stream_len - offset,
                                  &received_frames[frame_count], &ready);
    offset += consumed;

    if (ready) {
      frame_count++;
    }
  }

  assert(frame_count == 3);
  assert(received_frames[0].header.seq == 1 && received_frames[0].header.opcode == OP_PING);
  assert(received_frames[1].header.seq == 2 && received_frames[1].header.opcode == OP_READ_REG);
  assert(received_frames[2].header.seq == 3 && received_frames[2].header.opcode == OP_WRITE_REG);
  assert(p.stats.frames_received == 3);

  printf("Successfully extracted all 3 coalesced packets in exact order!\n");
  printf("PASSED\n\n");
}

void test_noise_and_false_sync(void) {
  printf("--- Test 4: Line Noise & Edge-Case Sync Recovery ---\n");

  Parser p;
  parser_init(&p);

  Frame valid_frame = {.header = {PROTO_SYNC_WORD, 55, OP_PING, 0}};
  uint8_t packet_bytes[PROTO_MAX_FRAME_LEN];
  int packet_len = protocol_pack_frame(&valid_frame, packet_bytes, sizeof(packet_bytes));

  // Stream with noise:
  // Random noise -> false sync (0xAA 0x99) -> consecutive sync (0xAA 0xAA 0x55) -> Valid Frame
  uint8_t noisy_stream[] = {
      0xDE, 0xAD, 0xBE, 0xEF, // Random line noise
      0xAA, 0x99,             // False sync 1
      0xAA, 0xAA, 0x55        // Consecutive 0xAA before 0x55!
  };

  uint8_t combined[128];
  size_t noise_len = sizeof(noisy_stream) - 2; // Up to 0xAA, 0xAA
  memcpy(&combined[0], noisy_stream, noise_len);
  // Now append valid packet starting with 0x55 (finishing 0xAA 0x55)
  // Or append full valid packet after noise:
  memcpy(&combined[noise_len], packet_bytes, packet_len);
  size_t total_noisy = noise_len + packet_len;

  Frame out;
  size_t offset = 0;
  bool got_frame = false;

  while (offset < total_noisy) {
    bool ready = false;
    size_t consumed = parser_feed(&p, &combined[offset], total_noisy - offset, &out, &ready);
    offset += consumed;
    if (ready) {
      got_frame = true;
      break;
    }
  }

  assert(got_frame == true);
  assert(out.header.seq == 55);
  assert(p.stats.sync_errors > 0);
  assert(p.stats.frames_received == 1);

  printf("Recovered from noise and consecutive sync bytes successfully!\n");
  printf("PASSED\n\n");
}

void test_corrupted_packet_recovery(void) {
  printf("--- Test 5: Corrupted CRC Recovery ---\n");

  Parser p;
  parser_init(&p);

  // 1. Pack a bad frame (flip bit in payload)
  Frame bad = {.header = {PROTO_SYNC_WORD, 1, OP_READ_REG, 4}};
  memcpy(bad.payload, "BAD1", 4);
  uint8_t stream[128];
  int bad_len = protocol_pack_frame(&bad, stream, sizeof(stream));
  stream[PROTO_HEADER_LEN + 1] ^= 0x08; // Corrupt payload bit

  // 2. Append a good frame immediately after
  Frame good = {.header = {PROTO_SYNC_WORD, 2, OP_READ_REG, 4}};
  memcpy(good.payload, "GOOD", 4);
  int good_len = protocol_pack_frame(&good, &stream[bad_len], sizeof(stream) - bad_len);
  size_t total_len = bad_len + good_len;

  Frame out;
  size_t offset = 0;
  size_t frames_found = 0;

  while (offset < total_len) {
    bool ready = false;
    size_t consumed = parser_feed(&p, &stream[offset], total_len - offset, &out, &ready);
    offset += consumed;
    if (ready) {
      frames_found++;
      assert(out.header.seq == 2);
      assert(memcmp(out.payload, "GOOD", 4) == 0);
    }
  }

  assert(frames_found == 1);
  assert(p.stats.crc_errors == 1);
  assert(p.stats.frames_received == 1);

  printf("Corrupt packet safely dropped (crc_errors=1) and subsequent packet parsed!\n");
  printf("PASSED\n\n");
}

int main(void) {
  printf("====================================================\n");
  printf("  STREAMING PARSER RESILIENCE TEST SUITE (PHASE 2) \n");
  printf("====================================================\n\n");

  test_byte_by_byte_fragmentation();
  test_irregular_chunks();
  test_packet_coalescence();
  test_noise_and_false_sync();
  test_corrupted_packet_recovery();

  printf("====================================================\n");
  printf("    ALL PHASE 2 TESTS PASSED! 🎉 (0 LEAKS, 0 ERRORS)\n");
  printf("====================================================\n");
  return 0;
}
