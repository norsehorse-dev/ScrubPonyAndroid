/* Safe file access.
 *
 * Two rules this header exists to enforce:
 *   1. Every read is checked against the count requested, never against zero.
 *   2. Nothing is ever opened through a symlink.
 */
#ifndef SP_IO_H
#define SP_IO_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "scrubpony.h"

/* What a scrubbed copy should inherit from its original. Ownership matters
 * for `sudo scrubpony -i -r /shared/photos`: without it, every file in the
 * tree quietly becomes root's. */
typedef struct {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
} sp_attrs;

typedef struct {
    FILE     *fp;
    char     *path;  /* owned; freed by sp_close */
    uint64_t  size;  /* from fstat at open time  */
    sp_attrs  attrs; /* to be carried onto the output */
} sp_file;

/* Opens a regular file for reading, refusing symlinks (O_NOFOLLOW) and
 * anything that is not a regular file. On failure *out is left zeroed and
 * nothing needs closing. */
sp_status sp_open_read(const char *path, sp_file *out);

/* Wraps a buffer as a read-only sp_file, so the parser can be driven without
 * touching the disk. The caller owns buf and must keep it alive until
 * sp_close.
 *
 * This exists for the fuzzer: at a few hundred thousand executions a second,
 * a temporary file per input would make the run I/O-bound, and an exactly
 * sized heap buffer lets AddressSanitizer catch a one-byte overread that a
 * page-aligned file mapping would silently allow. */
sp_status sp_open_memory(void *buf, size_t len, sp_file *out);

/* Idempotent. Safe on a zeroed sp_file. */
void sp_close(sp_file *f);

/* Reads exactly n bytes into buf.
 *   SP_OK            n bytes read
 *   SP_ERR_TRUNCATED fewer than n bytes were available; *got says how many
 *   SP_ERR_IO        a real read error
 * *got is always set when non-NULL, including on failure. A short read at end
 * of file is a value to handle, not an error to discover later. */
sp_status sp_read_exact(sp_file *f, void *buf, size_t n, size_t *got);

/* Absolute seek. Fails with SP_ERR_IO past end of file only when a
 * subsequent read is attempted; the seek itself is permissive, as fseek is. */
sp_status sp_seek(sp_file *f, uint64_t off);

/* Relative skip forward. Refuses to move past f->size, which is what makes
 * it safe to hand a length field straight from the file. */
sp_status sp_skip(sp_file *f, uint64_t n);

/* Current offset, or UINT64_MAX if it cannot be determined. */
uint64_t sp_tell(sp_file *f);

/* Path predicates. All use lstat, so a symlink to a regular file answers
 * false from sp_is_regular_file and true from sp_is_symlink. */
bool sp_is_regular_file(const char *path);
bool sp_is_directory(const char *path);
bool sp_is_symlink(const char *path);

/* ---------------------------------------------------------------------- *
 * Output
 *
 * Every write goes to a temporary file beside the destination and is moved
 * into place with rename() only once it is complete and on disk. rename is
 * atomic within a filesystem, so a crash, a full disk, or a kill signal
 * leaves the destination either untouched or fully written, never half of
 * each.
 *
 * That matters less for a fresh photo.scrubbed.jpg than it will for the
 * in-place mode in phase 6, but doing it the same way in both means phase 6
 * inherits a path that has already been exercised rather than getting its
 * own freshly-written one on the day it starts overwriting originals.
 * ---------------------------------------------------------------------- */

typedef struct {
    FILE     *fp;
    char     *path;     /* where it will end up                          */
    char     *tmp_path; /* where it is being written                     */
    uint64_t  written;
    bool      open;
} sp_out;

/* Creates the temporary file. attrs are the permissions and ownership the
 * final file should carry, normally the input's; NULL means leave whatever
 * the process umask produces. Fails with SP_ERR_EXISTS if final_path is
 * already occupied and force is false.
 *
 * Ownership is applied best-effort: only a privileged process may give a
 * file away, and failing to do so is not a reason to refuse to scrub. */
sp_status sp_out_open(const char *final_path, const sp_attrs *attrs,
                      bool force, sp_out *o);

sp_status sp_out_write(sp_out *o, const void *buf, size_t n);

/* Flushes, syncs, applies permissions, and renames into place. After this
 * returns SP_OK the destination exists and the temporary does not. */
sp_status sp_out_commit(sp_out *o);

/* Compares what has been written so far against whatever is currently at the
 * destination. Used by in-place mode to decide whether the rename is worth
 * doing at all: replacing a file with a byte-identical copy churns its mtime
 * and inode for no benefit.
 *
 * Cheap in the case that matters — differing sizes answer immediately, and
 * the byte comparison only runs when the sizes already match, which after a
 * real scrub they do not. */
sp_status sp_out_matches_destination(sp_out *o, bool *same);

/* Closes and removes the temporary, leaving the destination untouched.
 * Idempotent, and safe to call on a zeroed or already-committed sp_out —
 * which is what makes it usable as a blanket cleanup on every error path. */
void sp_out_abort(sp_out *o);

/* Streams len bytes from offset off in the input to the output. Leaves the
 * input positioned immediately after the copied range. */
sp_status sp_copy_range(sp_file *in, sp_out *o, uint64_t off, uint64_t len);

/* "a/photo.jpg" -> "a/photo.scrubbed.jpg"; "a/photo" -> "a/photo.scrubbed".
 * Returns a malloc'd string the caller frees, or NULL on allocation failure
 * or a nonsensical path. A leading dot in the basename is part of the name,
 * not an extension: ".hidden" becomes ".hidden.scrubbed". */
char *sp_derive_output_path(const char *input_path);

/* Creates every missing component of a directory path. An existing directory
 * is success, not a conflict. */
sp_status sp_mkdir_p(const char *dir, uint32_t mode);

/* Creates the directories leading up to a file path, but not the file. */
sp_status sp_mkdir_parents(const char *file_path, uint32_t mode);

/* True when inner is outer, or lies beneath it. Both are resolved with
 * realpath first, so symlinks and ".." cannot be used to sneak an output
 * directory inside the tree being read. Both must exist. */
bool sp_path_within(const char *inner, const char *outer);

#endif /* SP_IO_H */
