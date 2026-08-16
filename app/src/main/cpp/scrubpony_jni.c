/* JNI bridge — thin glue only.
 *
 * Every byte of scrubbing logic — segment/chunk/box parsing, keep/drop
 * policy, the temp-file-then-rename atomic write — lives unmodified in
 * ./core, copied verbatim from ScrubPonyDesktop/src. This file's only job is
 * translating between Java/Kotlin types and that C API, detecting which of
 * the four supported containers an input is, and packing sp_rewrite_stats
 * into a result the Kotlin side (ScrubStats.kt) can decode.
 *
 * Both paths handed in are real filesystem paths in this app's private
 * storage (see ScrubEngine.kt) — never content:// URIs, and never the
 * caller's original file. sp_out_open() is always called with force=true
 * because the output path is one this app just generated and controls.
 */
#include <jni.h>
#include <string.h>

#include "core/exif.h"
#include "core/io.h"
#include "core/jpeg.h"
#include "core/png.h"
#include "core/pngrewrite.h"
#include "core/webp.h"
#include "core/webprewrite.h"
#include "core/heif.h"
#include "core/heifrewrite.h"
#include "core/policy.h"
#include "core/rewrite.h"
#include "core/scrubpony.h"

/* Format codes packed into result[8], matching ImageFormat's ordinal on the
 * Kotlin side: JPEG=0, PNG=1, WEBP=2, HEIC=3. */
#define FMT_JPEG 0
#define FMT_PNG  1
#define FMT_WEBP 2
#define FMT_HEIF 3

/* Mirrors detect_format() in ScrubPonyDesktop/src/main.c: JPEG's SOI, then
 * PNG's signature, then WebP's RIFF header, then HEIC's ftyp. No probe leaves
 * the stream position defined on failure (see jpeg.h/png.h/webp.h/heif.h), so
 * a rewind precedes each subsequent attempt. Returns SP_ERR_NOT_HEIF when
 * none matched — the caller treats that as "no supported format", not as a
 * HEIC-specific verdict. */
static sp_status detect_format(sp_file *f, int *fmt)
{
    sp_status st;

    st = sp_jpeg_probe(f);
    if (st == SP_OK) { *fmt = FMT_JPEG; return SP_OK; }
    if (st != SP_ERR_NOT_JPEG) return st;

    st = sp_seek(f, 0);
    if (st != SP_OK) return st;

    st = sp_png_probe(f);
    if (st == SP_OK) { *fmt = FMT_PNG; return SP_OK; }
    if (st != SP_ERR_NOT_PNG) return st;

    st = sp_seek(f, 0);
    if (st != SP_OK) return st;

    st = sp_webp_probe(f);
    if (st == SP_OK) { *fmt = FMT_WEBP; return SP_OK; }
    if (st != SP_ERR_NOT_WEBP) return st;

    st = sp_seek(f, 0);
    if (st != SP_OK) return st;

    st = sp_heif_probe(f);
    if (st == SP_OK) { *fmt = FMT_HEIF; return SP_OK; }
    return st;
}

JNIEXPORT jlongArray JNICALL
Java_com_norsehorse_scrubpony_NativeScrubber_scrubFile(
    JNIEnv *env, jobject thiz,
    jstring j_input_path, jstring j_output_path,
    jboolean strict, jboolean no_orientation)
{
    (void)thiz;

    const char *input_path = (*env)->GetStringUTFChars(env, j_input_path, NULL);
    const char *output_path = (*env)->GetStringUTFChars(env, j_output_path, NULL);

    jlong result[9];
    sp_file f;
    sp_out out;
    sp_policy pol;
    sp_rewrite_stats stats;
    sp_status st;
    int opened_input = 0;
    int fmt = FMT_JPEG;

    memset(result, 0, sizeof result);
    memset(&pol, 0, sizeof pol);
    pol.strict = strict ? true : false;
    pol.no_orientation = no_orientation ? true : false;

    if (input_path == NULL || output_path == NULL) {
        result[0] = (jlong)SP_ERR_USAGE;
        goto respond;
    }

    st = sp_open_read(input_path, &f);
    if (st != SP_OK) {
        result[0] = (jlong)st;
        goto respond;
    }
    opened_input = 1;

    st = detect_format(&f, &fmt);
    if (st != SP_OK) {
        result[0] = (jlong)st;
        goto respond;
    }

    st = sp_out_open(output_path, &f.attrs, /* force = */ true, &out);
    if (st != SP_OK) {
        result[0] = (jlong)st;
        goto respond;
    }

    switch (fmt) {
    case FMT_PNG:  st = sp_png_rewrite(&f, &out, &pol, NULL, &stats); break;
    case FMT_WEBP: st = sp_webp_rewrite(&f, &out, &pol, NULL, &stats); break;
    case FMT_HEIF: st = sp_heif_rewrite(&f, &out, &pol, NULL, &stats); break;
    default:       st = sp_rewrite(&f, &out, &pol, NULL, &stats); break;
    }
    if (st != SP_OK) {
        /* Nothing is published: original untouched, temp file removed. This
         * covers SP_ERR_UNSUPPORTED too — a valid HEIC whose layout the
         * rewriter will not touch — which the Kotlin side reports as its own
         * outcome rather than a corrupt-file failure. */
        sp_out_abort(&out);
        result[0] = (jlong)st;
        result[8] = (jlong)fmt;
        goto respond;
    }

    st = sp_out_commit(&out);
    if (st != SP_OK) {
        sp_out_abort(&out);
        result[0] = (jlong)st;
        result[8] = (jlong)fmt;
        goto respond;
    }

    result[0] = (jlong)SP_OK;
    result[1] = (jlong)stats.dropped;
    result[2] = (jlong)stats.kept;
    result[3] = (jlong)stats.in_size;
    result[4] = (jlong)stats.out_size;
    result[5] = (stats.have_exif && stats.exif.has_gps) ? 1 : 0;
    result[6] = (stats.have_exif && sp_exif_orientation_matters(stats.exif.orientation)) ? 1 : 0;
    result[7] = stats.orientation_kept ? 1 : 0;
    result[8] = (jlong)fmt;

respond:
    if (opened_input)
        sp_close(&f);
    if (input_path != NULL)
        (*env)->ReleaseStringUTFChars(env, j_input_path, input_path);
    if (output_path != NULL)
        (*env)->ReleaseStringUTFChars(env, j_output_path, output_path);

    {
        jlongArray jresult = (*env)->NewLongArray(env, 9);
        if (jresult != NULL)
            (*env)->SetLongArrayRegion(env, jresult, 0, 9, result);
        return jresult;
    }
}

JNIEXPORT jstring JNICALL
Java_com_norsehorse_scrubpony_NativeScrubber_statusMessage(
    JNIEnv *env, jobject thiz, jint status)
{
    (void)thiz;
    return (*env)->NewStringUTF(env, sp_strstatus((sp_status)status));
}
