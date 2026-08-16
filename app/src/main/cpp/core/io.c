#include "io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *sp_strstatus(sp_status s)
{
    switch (s) {
    case SP_OK:             return "ok";
    case SP_ERR_IO:         return "i/o error";
    case SP_ERR_NOT_JPEG:   return "not a JPEG";
    case SP_ERR_NOT_REGULAR:return "not a regular file";
    case SP_ERR_TRUNCATED:  return "truncated";
    case SP_ERR_MALFORMED:  return "malformed";
    case SP_ERR_EXISTS:     return "output already exists (use -f)";
    case SP_ERR_OUTPUT_GREW:return "output would be larger than input";
    case SP_ERR_USAGE:      return "usage error";
    }
    return "unknown error";
}

sp_status sp_open_read(const char *path, sp_file *out)
{
    int fd;
    int saved;
    struct stat st;
    FILE *fp;
    char *copy;

    if (out == NULL)
        return SP_ERR_USAGE;

    out->fp = NULL;
    out->path = NULL;
    out->size = 0;
    memset(&out->attrs, 0, sizeof out->attrs);

    if (path == NULL || path[0] == '\0')
        return SP_ERR_USAGE;

    /* O_NOFOLLOW is the whole point: a symlink here is either an accident or
     * an attempt to reach a file outside the tree the user pointed us at. */
    fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        /* ELOOP means it was a symlink, which is a refusal, not an I/O fault. */
        return (errno == ELOOP || errno == EMLINK) ? SP_ERR_NOT_REGULAR
                                                   : SP_ERR_IO;
    }

    if (fstat(fd, &st) != 0) {
        saved = errno;
        (void)close(fd);
        errno = saved;
        return SP_ERR_IO;
    }

    if (!S_ISREG(st.st_mode)) {
        (void)close(fd);
        return SP_ERR_NOT_REGULAR;
    }

    if (st.st_size < 0) { /* cannot happen for a regular file; be sure anyway */
        (void)close(fd);
        return SP_ERR_IO;
    }

    fp = fdopen(fd, "rb");
    if (fp == NULL) {
        saved = errno;
        (void)close(fd);
        errno = saved;
        return SP_ERR_IO;
    }

    copy = strdup(path);
    if (copy == NULL) {
        (void)fclose(fp); /* closes fd too */
        return SP_ERR_IO;
    }

    out->fp   = fp;
    out->path = copy;
    out->size = (uint64_t)st.st_size;
    out->attrs.mode = (uint32_t)st.st_mode;
    out->attrs.uid  = (uint32_t)st.st_uid;
    out->attrs.gid  = (uint32_t)st.st_gid;
    return SP_OK;
}

sp_status sp_open_memory(void *buf, size_t len, sp_file *out)
{
    FILE *fp;
    char *name;

    if (out == NULL)
        return SP_ERR_USAGE;
    memset(out, 0, sizeof *out);
    if (buf == NULL && len > 0u)
        return SP_ERR_USAGE;

    /* POSIX leaves fmemopen with a zero size unspecified, and libcs disagree:
     * glibc hands back a usable stream, others fail. A zero-byte input is a
     * perfectly ordinary thing to hand a scrubber — sp_open_read accepts an
     * empty file and lets the probe reject it — so open an empty stream a way
     * that behaves the same everywhere rather than inheriting the argument. */
    fp = (len == 0u) ? fopen("/dev/null", "rb") : fmemopen(buf, len, "rb");
    if (fp == NULL)
        return SP_ERR_IO;

    name = strdup("<memory>");
    if (name == NULL) {
        (void)fclose(fp);
        return SP_ERR_IO;
    }

    out->fp = fp;
    out->path = name;
    out->size = (uint64_t)len;
    return SP_OK;
}

void sp_close(sp_file *f)
{
    if (f == NULL)
        return;
    if (f->fp != NULL) {
        (void)fclose(f->fp);
        f->fp = NULL;
    }
    free(f->path);
    f->path = NULL;
    f->size = 0;
    memset(&f->attrs, 0, sizeof f->attrs);
}

sp_status sp_read_exact(sp_file *f, void *buf, size_t n, size_t *got)
{
    size_t r;

    if (got != NULL)
        *got = 0;
    if (f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;
    if (n == 0)
        return SP_OK;
    if (buf == NULL)
        return SP_ERR_USAGE;

    /* Item size 1, always. fread's return is then a byte count, which is the
     * only form worth reasoning about. */
    r = fread(buf, 1, n, f->fp);
    if (got != NULL)
        *got = r;

    if (r == n)
        return SP_OK;
    if (ferror(f->fp) != 0)
        return SP_ERR_IO;
    return SP_ERR_TRUNCATED;
}

sp_status sp_seek(sp_file *f, uint64_t off)
{
    if (f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;
    /* off_t is signed and may be 32 bits on an unlucky platform. Refuse
     * rather than wrap. */
    if (off > (uint64_t)0x7FFFFFFFFFFFFFFFLL)
        return SP_ERR_MALFORMED;
    if (fseeko(f->fp, (off_t)off, SEEK_SET) != 0)
        return SP_ERR_IO;
    return SP_OK;
}

sp_status sp_skip(sp_file *f, uint64_t n)
{
    uint64_t here;

    if (f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;
    if (n == 0)
        return SP_OK;

    here = sp_tell(f);
    if (here == UINT64_MAX)
        return SP_ERR_IO;

    /* The whole reason this is not a bare fseek: n usually came out of the
     * file we are parsing, so it is attacker-controlled. Both the overflow
     * and the past-EOF case have to be refused here, once, rather than at
     * every call site. */
    if (n > UINT64_MAX - here)
        return SP_ERR_MALFORMED;
    if (here + n > f->size)
        return SP_ERR_TRUNCATED;

    return sp_seek(f, here + n);
}

uint64_t sp_tell(sp_file *f)
{
    off_t p;
    if (f == NULL || f->fp == NULL)
        return UINT64_MAX;
    p = ftello(f->fp);
    if (p < 0)
        return UINT64_MAX;
    return (uint64_t)p;
}

/* ---------------------------------------------------------------------- *
 * Output
 * ---------------------------------------------------------------------- */

#define SP_TMP_SUFFIX ".scrubpony-tmp"
#define SP_OUT_SUFFIX ".scrubbed"

/* Splits at the last '/', returning the index where the basename starts. */
static size_t basename_start(const char *path)
{
    const char *slash = strrchr(path, '/');
    return (slash == NULL) ? 0u : (size_t)(slash - path) + 1u;
}

char *sp_derive_output_path(const char *input_path)
{
    size_t len;
    size_t base;
    size_t split;
    const char *dot;
    char *out;

    if (input_path == NULL || input_path[0] == '\0')
        return NULL;

    len = strlen(input_path);
    base = basename_start(input_path);
    if (base >= len)
        return NULL; /* a trailing slash: that is a directory, not a file */

    /* Search for the extension dot only within the basename, and only after
     * its first character, so that ".hidden" keeps its whole name and
     * "../photo" does not acquire an extension from the "..". */
    dot = strrchr(input_path + base + 1u, '.');
    split = (dot != NULL) ? (size_t)(dot - input_path) : len;

    out = malloc(len + sizeof SP_OUT_SUFFIX);
    if (out == NULL)
        return NULL;

    memcpy(out, input_path, split);
    memcpy(out + split, SP_OUT_SUFFIX, sizeof SP_OUT_SUFFIX - 1u);
    memcpy(out + split + sizeof SP_OUT_SUFFIX - 1u, input_path + split,
           len - split);
    out[len + sizeof SP_OUT_SUFFIX - 1u] = '\0';
    return out;
}

/* ".photo.scrubbed.jpg.scrubpony-tmp", in the same directory as the target
 * so that rename() stays within one filesystem and therefore atomic. */
static char *derive_tmp_path(const char *final_path)
{
    size_t len = strlen(final_path);
    size_t base = basename_start(final_path);
    char *tmp = malloc(len + sizeof SP_TMP_SUFFIX + 1u);

    if (tmp == NULL)
        return NULL;

    memcpy(tmp, final_path, base);
    tmp[base] = '.';
    memcpy(tmp + base + 1u, final_path + base, len - base);
    memcpy(tmp + len + 1u, SP_TMP_SUFFIX, sizeof SP_TMP_SUFFIX);
    return tmp;
}

sp_status sp_out_open(const char *final_path, const sp_attrs *attrs,
                      bool force, sp_out *o)
{
    int fd;
    int saved;

    if (o == NULL)
        return SP_ERR_USAGE;
    memset(o, 0, sizeof *o);
    if (final_path == NULL || final_path[0] == '\0')
        return SP_ERR_USAGE;

    /* Refuse to clobber. photo.scrubbed.jpg already existing almost always
     * means this ran before, and silently replacing it is the kind of thing
     * people discover much later. */
    if (!force) {
        struct stat st;
        if (lstat(final_path, &st) == 0)
            return SP_ERR_EXISTS;
    }

    o->path = strdup(final_path);
    if (o->path == NULL)
        return SP_ERR_IO;
    o->tmp_path = derive_tmp_path(final_path);
    if (o->tmp_path == NULL) {
        free(o->path);
        o->path = NULL;
        return SP_ERR_IO;
    }

    /* O_EXCL so a stale temp from an interrupted run is noticed rather than
     * silently reused, and 0600 so the file is never briefly world-readable
     * while it is being filled. Real permissions are applied at commit. */
    fd = open(o->tmp_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0 && errno == EEXIST) {
        (void)unlink(o->tmp_path);
        fd = open(o->tmp_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    }
    if (fd < 0) {
        saved = errno;
        sp_out_abort(o);
        errno = saved;
        return SP_ERR_IO;
    }

    o->fp = fdopen(fd, "wb");
    if (o->fp == NULL) {
        saved = errno;
        (void)close(fd);
        sp_out_abort(o);
        errno = saved;
        return SP_ERR_IO;
    }

    o->open = true;
    o->written = 0;

    if (attrs != NULL) {
        int fd2 = fileno(o->fp);

        /* Ownership before permissions: chown clears setuid and setgid bits
         * on most systems, so doing it the other way round would silently
         * undo the chmod. Both are best-effort — only a privileged process
         * can give a file away, and a file that ends up conservatively
         * permissioned beats a file not scrubbed. The results are examined
         * rather than cast to void because glibc marks fchown
         * warn_unused_result and a cast does not satisfy it. */
        if (fchown(fd2, (uid_t)attrs->uid, (gid_t)attrs->gid) != 0)
            errno = 0;
        if (fchmod(fd2, (mode_t)(attrs->mode & 07777u)) != 0)
            errno = 0;
    }
    return SP_OK;
}

sp_status sp_out_write(sp_out *o, const void *buf, size_t n)
{
    if (o == NULL || !o->open || o->fp == NULL)
        return SP_ERR_USAGE;
    if (n == 0u)
        return SP_OK;
    if (buf == NULL)
        return SP_ERR_USAGE;
    if (fwrite(buf, 1, n, o->fp) != n)
        return SP_ERR_IO;
    o->written += (uint64_t)n;
    return SP_OK;
}

sp_status sp_out_matches_destination(sp_out *o, bool *same)
{
    struct stat st;
    FILE *a;
    FILE *b;
    bool equal = true;

    if (same != NULL)
        *same = false;
    if (o == NULL || !o->open || o->fp == NULL || same == NULL)
        return SP_ERR_USAGE;

    if (fflush(o->fp) != 0)
        return SP_ERR_IO;

    if (lstat(o->path, &st) != 0 || !S_ISREG(st.st_mode))
        return SP_OK; /* nothing there to match */
    if (st.st_size < 0 || (uint64_t)st.st_size != o->written)
        return SP_OK; /* different lengths settle it without reading */

    a = fopen(o->tmp_path, "rb");
    if (a == NULL)
        return SP_ERR_IO;
    b = fopen(o->path, "rb");
    if (b == NULL) {
        (void)fclose(a);
        return SP_ERR_IO;
    }

    for (;;) {
        char ba[8192];
        char bb[8192];
        size_t na = fread(ba, 1, sizeof ba, a);
        size_t nb = fread(bb, 1, sizeof bb, b);

        if (na != nb || (na > 0u && memcmp(ba, bb, na) != 0)) {
            equal = false;
            break;
        }
        if (na == 0u)
            break;
    }

    if (ferror(a) != 0 || ferror(b) != 0)
        equal = false;

    (void)fclose(a);
    (void)fclose(b);
    *same = equal;
    return SP_OK;
}

sp_status sp_out_commit(sp_out *o)
{
    int fd;

    if (o == NULL || !o->open || o->fp == NULL)
        return SP_ERR_USAGE;

    if (fflush(o->fp) != 0) {
        sp_out_abort(o);
        return SP_ERR_IO;
    }

    fd = fileno(o->fp);
    /* Get the bytes onto the platter before the rename that publishes them.
     * Without this, a crash can leave a correctly-named file full of
     * nothing, which is worse than no file at all. */
    if (fd >= 0 && fsync(fd) != 0) {
        sp_out_abort(o);
        return SP_ERR_IO;
    }

    if (fclose(o->fp) != 0) {
        o->fp = NULL;
        sp_out_abort(o);
        return SP_ERR_IO;
    }
    o->fp = NULL;

    if (rename(o->tmp_path, o->path) != 0) {
        sp_out_abort(o);
        return SP_ERR_IO;
    }

    /* Sync the directory too, so the rename itself survives a crash. */
    {
        size_t base = basename_start(o->path);
        char *dir = malloc(base + 2u);
        if (dir != NULL) {
            int dfd;
            if (base == 0u) {
                dir[0] = '.';
                dir[1] = '\0';
            } else {
                memcpy(dir, o->path, base);
                dir[base] = '\0';
            }
            dfd = open(dir, O_RDONLY);
            if (dfd >= 0) {
                (void)fsync(dfd);
                (void)close(dfd);
            }
            free(dir);
        }
    }

    o->open = false;
    free(o->tmp_path);
    o->tmp_path = NULL;
    free(o->path);
    o->path = NULL;
    return SP_OK;
}

void sp_out_abort(sp_out *o)
{
    if (o == NULL)
        return;
    if (o->fp != NULL) {
        (void)fclose(o->fp);
        o->fp = NULL;
    }
    if (o->tmp_path != NULL) {
        /* Leaving .photo.jpg.scrubpony-tmp files scattered through someone's
         * Pictures folder is its own small betrayal. */
        (void)unlink(o->tmp_path);
        free(o->tmp_path);
        o->tmp_path = NULL;
    }
    free(o->path);
    o->path = NULL;
    o->open = false;
    o->written = 0;
}

sp_status sp_copy_range(sp_file *in, sp_out *o, uint64_t off, uint64_t len)
{
    uint8_t buf[16384];
    sp_status st;

    if (in == NULL || in->fp == NULL || o == NULL || !o->open)
        return SP_ERR_USAGE;
    if (len == 0u)
        return SP_OK;
    if (off > in->size || len > in->size - off)
        return SP_ERR_TRUNCATED;

    st = sp_seek(in, off);
    if (st != SP_OK)
        return st;

    while (len > 0u) {
        size_t want = (len < (uint64_t)sizeof buf) ? (size_t)len : sizeof buf;
        size_t got = 0;

        st = sp_read_exact(in, buf, want, &got);
        if (st != SP_OK)
            return st;
        st = sp_out_write(o, buf, got);
        if (st != SP_OK)
            return st;
        len -= (uint64_t)got;
    }

    return SP_OK;
}

sp_status sp_mkdir_p(const char *dir, uint32_t mode)
{
    char *copy;
    char *p;
    sp_status st = SP_OK;

    if (dir == NULL || dir[0] == '\0')
        return SP_ERR_USAGE;

    copy = strdup(dir);
    if (copy == NULL)
        return SP_ERR_IO;

    /* Walk forward, terminating the string at each separator in turn. Start
     * at index 1 so that a leading '/' is not treated as an empty component
     * we then try to create. */
    for (p = copy + 1; *p != '\0'; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(copy, (mode_t)mode) != 0 && errno != EEXIST) {
            st = SP_ERR_IO;
            break;
        }
        *p = '/';
    }

    if (st == SP_OK && mkdir(copy, (mode_t)mode) != 0 && errno != EEXIST)
        st = SP_ERR_IO;

    /* EEXIST is only acceptable if what exists is actually a directory. */
    if (st == SP_OK && !sp_is_directory(copy))
        st = SP_ERR_IO;

    free(copy);
    return st;
}

sp_status sp_mkdir_parents(const char *file_path, uint32_t mode)
{
    size_t base;
    char *dir;
    sp_status st;

    if (file_path == NULL)
        return SP_ERR_USAGE;

    base = basename_start(file_path);
    if (base == 0u)
        return SP_OK; /* no directory part; nothing to create */

    dir = malloc(base + 1u);
    if (dir == NULL)
        return SP_ERR_IO;
    memcpy(dir, file_path, base);
    dir[base] = '\0';

    /* Strip the trailing separator unless the whole thing is "/". */
    if (base > 1u && dir[base - 1u] == '/')
        dir[base - 1u] = '\0';

    st = sp_mkdir_p(dir, mode);
    free(dir);
    return st;
}

bool sp_path_within(const char *inner, const char *outer)
{
    char *ri;
    char *ro;
    bool within = false;

    if (inner == NULL || outer == NULL)
        return false;

    /* realpath rather than string comparison: "clean/../pics" and a symlink
     * both look like separate trees until they are resolved. */
    ri = realpath(inner, NULL);
    ro = realpath(outer, NULL);

    if (ri != NULL && ro != NULL) {
        size_t lo = strlen(ro);
        if (strcmp(ri, ro) == 0) {
            within = true;
        } else if (strncmp(ri, ro, lo) == 0) {
            /* Guard against "/a/bc" matching the prefix "/a/b": the next
             * character has to be the separator, unless outer already ended
             * with one (which only "/" does after realpath). */
            within = (ro[lo - 1u] == '/') || (ri[lo] == '/');
        }
    }

    free(ri);
    free(ro);
    return within;
}

static bool stat_mode(const char *path, mode_t *mode)
{
    struct stat st;
    if (path == NULL || mode == NULL)
        return false;
    if (lstat(path, &st) != 0)
        return false;
    *mode = st.st_mode;
    return true;
}

bool sp_is_regular_file(const char *path)
{
    mode_t m;
    return stat_mode(path, &m) && S_ISREG(m);
}

bool sp_is_directory(const char *path)
{
    mode_t m;
    return stat_mode(path, &m) && S_ISDIR(m);
}

bool sp_is_symlink(const char *path)
{
    mode_t m;
    return stat_mode(path, &m) && S_ISLNK(m);
}
