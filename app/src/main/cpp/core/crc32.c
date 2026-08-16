#include "crc32.h"

/* Generated once at compile time via the standard bit-at-a-time construction
 * rather than hand-copied from somewhere, so there is exactly one place that
 * could be wrong and the test's known-answer check covers it directly. */
static uint32_t table[256];
static int table_ready = 0;

static void build_table(void)
{
    uint32_t n;

    for (n = 0; n < 256u; n++) {
        uint32_t c = n;
        int k;
        for (k = 0; k < 8; k++) {
            if ((c & 1u) != 0u)
                c = 0xEDB88320u ^ (c >> 1);
            else
                c = c >> 1;
        }
        table[n] = c;
    }
    table_ready = 1;
}

uint32_t sp_crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    uint32_t c;
    size_t i;

    if (!table_ready)
        build_table();

    /* The running value is carried complemented between calls (standard for
     * this algorithm), so un-complement on entry and re-complement on exit.
     * A caller who starts at SP_CRC32_INIT (0) and only ever calls this
     * function never has to know that; the complement cancels itself out
     * across a chain of calls and only matters at the true start and end. */
    c = crc ^ 0xFFFFFFFFu;
    if (buf != NULL) {
        for (i = 0; i < len; i++)
            c = table[(c ^ buf[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

uint32_t sp_crc32(const uint8_t *buf, size_t len)
{
    return sp_crc32_update(SP_CRC32_INIT, buf, len);
}
