/* ScrubPony — shared types and status codes.
 *
 * Everything in the project reports failure through sp_status. main() is the
 * only place that turns one into a process exit code, so the mapping lives in
 * exactly one function and nowhere else.
 */
#ifndef SCRUBPONY_H
#define SCRUBPONY_H

#include <stddef.h>
#include <stdint.h>

#define SP_NAME    "scrubpony"
#define SP_VERSION "1.3.0"

typedef enum {
    SP_OK = 0,
    SP_ERR_IO,          /* open/read/write failed, errno is meaningful      */
    SP_ERR_NOT_JPEG,    /* no SOI marker; not a JPEG whatever the extension */
    SP_ERR_NOT_PNG,     /* no PNG signature; not a PNG whatever the extension */
    SP_ERR_NOT_WEBP,    /* no RIFF/WEBP header; not a WebP whatever the extension */
    SP_ERR_NOT_HEIF,    /* no HEIC-family ftyp; not a HEIC whatever the extension */
    SP_ERR_NOT_REGULAR, /* directory, symlink, device, fifo, socket         */
    SP_ERR_TRUNCATED,   /* file ended mid-structure                         */
    SP_ERR_MALFORMED,   /* structurally invalid: bad length, desynced       */
    SP_ERR_UNSUPPORTED, /* valid, but uses a feature this tool won't rewrite */
    SP_ERR_EXISTS,      /* output path already occupied and -f was not given */
    SP_ERR_OUTPUT_GREW, /* scrubbing only removes bytes; this means a bug    */
    SP_ERR_USAGE        /* caller's fault, not the file's                   */
} sp_status;

/* Human-readable, lowercase, no trailing punctuation, suitable for
 * "scrubpony: %s: %s\n" with a path. */
const char *sp_strstatus(sp_status s);

/* Process exit codes. See the planning document, section 3. */
#define SP_EXIT_OK    0
#define SP_EXIT_ERROR 1
#define SP_EXIT_DIRTY 2 /* --check only: identifying metadata was present */
#define SP_EXIT_USAGE 64

#endif /* SCRUBPONY_H */
