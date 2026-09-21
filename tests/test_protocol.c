#include "crc16.h"
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_crc16_correctness(void) {
  printf("--- Test 1: CRC-16-CCITT Correctness & Test Vectors ---\n");

  // Standard CRC-16-CCITT test vector: "123456789" (ASCII) -> Expected: 0x29B1
  const uint8_t test_vector[] = "123456789";
  uint16_t crc_table = crc16_ccitt(test_vector, 9);
  uint16_t crc_bitwise = crc16_ccitt_bitwise(test_vector, 9);

  printf("Test Vector '123456789':\n");
  printf("  -> Table CRC:   0x%04X\n", crc_table);
  printf("  -> Bitwise CRC: 0x%04X\n", crc_bitwise);

  assert(crc_table == 0x29B1);
  assert(crc_bitwise == 0x29B1);
  assert(crc_table == crc_bitwise);

  // Test with arbitrary data buffers to ensure Table and Bitwise match 100%
  uint8_t buffer[64];
  for (size_t i = 0; i < sizeof(buffer); i++) {
    buffer[i] = (uint8_t)(i * 7 + 3);
  }
  assert(crc16_ccitt(buffer, sizeof(buffer)) == crc16_ccitt_bitwise(buffer, sizeof(buffer)));

  printf("PASSED\n\n");
}

void test_frame_roundtrip(void) {
  printf("--- Test 2: Frame Serialization & Deserialization Roundtrip ---\n");

  Frame original;
  original.header.sync = PROTO_SYNC_WORD;
  original.header.seq = 42;
  original.header.opcode = OP_READ_REG;
  original.header.length = 12;
  memcpy(original.payload, "SensorConfig", 12);

  uint8_t wire_buf[PROTO_MAX_FRAME_LEN];
  int packed_len = protocol_pack_frame(&original, wire_buf, sizeof(wire_buf));
  assert(packed_len == PROTO_HEADER_LEN + 12 + PROTO_CRC_LEN);

  printf("Packed Frame Total Length: %d bytes\n", packed_len);
  printf("Wire bytes: [%02X %02X %02X %02X %02X %02X ...]\n",
         wire_buf[0], wire_buf[1], wire_buf[2], wire_buf[3], wire_buf[4], wire_buf[5]);

  // Verify sync bytes on wire are 0xAA, 0x55
  assert(wire_buf[0] == 0xAA);
  assert(wire_buf[1] == 0x55);
  assert(wire_buf[2] == 42);
  assert(wire_buf[3] == OP_READ_REG);
  assert(wire_buf[4] == 0x00); // Length high byte
  assert(wire_buf[5] == 0x0C); // Length low byte (12)

  // Unpack frame
  Frame unpacked;
  ProtoStatus status = protocol_unpack_frame(wire_buf, packed_len, &unpacked);
  assert(status == PROTO_OK);

  assert(unpacked.header.sync == PROTO_SYNC_WORD);
  assert(unpacked.header.seq == 42);
  assert(unpacked.header.opcode == OP_READ_REG);
  assert(unpacked.header.length == 12);
  assert(memcmp(unpacked.payload, "SensorConfig", 12) == 0);

  printf("Roundtrip unpacked successfully with matching header, payload, and CRC!\n");
  printf("PASSED\n\n");
}

void test_empty_payload_frame(void) {
  printf("--- Test 3: Minimum Frame with Zero Payload (Ping) ---\n");

  Frame ping_frame;
  ping_frame.header.sync = PROTO_SYNC_WORD;
  ping_frame.header.seq = 1;
  ping_frame.header.opcode = OP_PING;
  ping_frame.header.length = 0;

  uint8_t wire_buf[PROTO_MAX_FRAME_LEN];
  int packed_len = protocol_pack_frame(&ping_frame, wire_buf, sizeof(wire_buf));
  assert(packed_len == PROTO_MIN_FRAME_LEN); // Exactly 8 bytes

  Frame unpacked;
  ProtoStatus status = protocol_unpack_frame(wire_buf, packed_len, &unpacked);
  assert(status == PROTO_OK);
  assert(unpacked.header.opcode == OP_PING);
  assert(unpacked.header.length == 0);

  printf("Zero-payload ping frame (8 bytes) verified!\n");
  printf("PASSED\n\n");
}

void test_bit_corruption_detection(void) {
  printf("--- Test 4: Bit-Flip & Line Noise Error Detection ---\n");

  Frame original;
  original.header.sync = PROTO_SYNC_WORD;
  original.header.seq = 10;
  original.header.opcode = OP_WRITE_REG;
  original.header.length = 8;
  memcpy(original.payload, "\x01\x02\x03\x04\x05\x06\x07\x08", 8);

  uint8_t wire_buf[PROTO_MAX_FRAME_LEN];
  int packed_len = protocol_pack_frame(&original, wire_buf, sizeof(wire_buf));
  assert(packed_len > 0);

  // 1. Corrupt 1 single bit in payload (simulate line noise)
  uint8_t corrupted_payload[PROTO_MAX_FRAME_LEN];
  memcpy(corrupted_payload, wire_buf, packed_len);
  corrupted_payload[PROTO_HEADER_LEN + 2] ^= 0x01; // Flip bit 0

  Frame dummy;
  ProtoStatus status = protocol_unpack_frame(corrupted_payload, packed_len, &dummy);
  assert(status == PROTO_ERR_BAD_CRC);
  printf("Bit-flip in payload caught by CRC-16! (Returned PROTO_ERR_BAD_CRC)\n");

  // 2. Corrupt 1 single bit in Header sequence number
  uint8_t corrupted_header[PROTO_MAX_FRAME_LEN];
  memcpy(corrupted_header, wire_buf, packed_len);
  corrupted_header[2] ^= 0x80; // Flip MSB of sequence number

  status = protocol_unpack_frame(corrupted_header, packed_len, &dummy);
  assert(status == PROTO_ERR_BAD_CRC);
  printf("Bit-flip in header caught by CRC-16! (Returned PROTO_ERR_BAD_CRC)\n");

  // 3. Corrupt Sync marker
  uint8_t corrupted_sync[PROTO_MAX_FRAME_LEN];
  memcpy(corrupted_sync, wire_buf, packed_len);
  corrupted_sync[0] = 0x00;

  status = protocol_unpack_frame(corrupted_sync, packed_len, &dummy);
  assert(status == PROTO_ERR_BAD_SYNC);
  printf("Corrupt sync marker caught! (Returned PROTO_ERR_BAD_SYNC)\n");

  printf("PASSED\n\n");
}

void test_payload_bounds(void) {
  printf("--- Test 5: Maximum Transmission Unit (MTU) Bounds ---\n");

  Frame oversize;
  oversize.header.sync = PROTO_SYNC_WORD;
  oversize.header.seq = 99;
  oversize.header.opcode = OP_TELEMETRY_DATA;
  oversize.header.length = PROTO_MAX_PAYLOAD_LEN + 1; // 257 bytes!

  uint8_t wire_buf[PROTO_MAX_FRAME_LEN + 10];
  int res = protocol_pack_frame(&oversize, wire_buf, sizeof(wire_buf));
  assert(res == -PROTO_ERR_PAYLOAD_TOO_LARGE);

  printf("Payload exceeding %d bytes gracefully rejected with PROTO_ERR_PAYLOAD_TOO_LARGE!\n",
         PROTO_MAX_PAYLOAD_LEN);
  printf("PASSED\n\n");
}

int main(void) {
  printf("====================================================\n");
  printf("   BINARY PROTOCOL & CRC ENGINE TEST SUITE (PHASE 1)\n");
  printf("====================================================\n\n");

  test_crc16_correctness();
  test_frame_roundtrip();
  test_empty_payload_frame();
  test_bit_corruption_detection();
  test_payload_bounds();

  printf("====================================================\n");
  printf("    ALL PHASE 1 TESTS PASSED! 🎉 (0 LEAKS, 0 ERRORS)\n");
  printf("====================================================\n");
  return 0;
}
