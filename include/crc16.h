#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

#define CRC16_CCITT_INIT 0xFFFF
#define CRC16_CCITT_POLY 0x1021 // x^16 + x^12 + x^5 + 1

/**
 * Update a running CRC-16-CCITT with a single byte.
 */
uint16_t crc16_update(uint16_t crc, uint8_t byte);

/**
 * Compute the CRC-16-CCITT across a buffer of data.
 * Returns the final 16-bit checksum.
 */
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

/**
 * Slow, bit-by-bit reference implementation for mathematical verification.
 */
uint16_t crc16_ccitt_bitwise(const uint8_t *data, size_t len);

#endif // CRC16_H
