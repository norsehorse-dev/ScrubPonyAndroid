/* CRC-32, the exact variant PNG requires (ISO 3309 / ITU-T V.42, polynomial
 * 0xEDB88320 reflected — the same one zlib, gzip and Ethernet use).
 *
 * A from-scratch implementation rather than a dependency: the whole project
 * is "no dependencies beyond libc," and linking zlib for one polynomial
 * would be a strange trade. The algorithm is standard and well understood;
 * sp_crc32("123456789", 9) == 0xCBF43926 is the textbook check value, and
 * the tests assert exactly that.
 */
#ifndef SP_CRC32_H
#define SP_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* One-shot CRC-32 over buf[0..len). */
uint32_t sp_crc32(const uint8_t *buf, size_t len);

/* Incremental form, for streaming a chunk's type+data without buffering it
 * whole. crc is the running value; start it at 0 via SP_CRC32_INIT and feed
 * successive spans (type first, then data) — sp_crc32(buf, len) is exactly
 * sp_crc32_update(SP_CRC32_INIT, buf, len). */
#define SP_CRC32_INIT 0u
uint32_t sp_crc32_update(uint32_t crc, const uint8_t *buf, size_t len);

#endif /* SP_CRC32_H */
