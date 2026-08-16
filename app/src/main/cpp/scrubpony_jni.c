/* JNI bridge — thin glue only.
 *
 * Every byte of scrubbing logic — segment parsing, keep/drop policy, the
 * temp-file-then-rename atomic write — lives unmodified in ./core, copied
 * verbatim from ScrubPonyDesktop/src. This file's only job is translating
 * between Java/Kotlin types and that C API, and packing sp_rewrite_stats
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
#include "core/policy.h"
#include "core/rewrite.h"
#include "core/scrubpony.h"

JNIEXPORT jlongArray JNICALL
Java_com_norsehorse_scrubpony_NativeScrubber_scrubFile(
    JNIEnv *env, jobject thiz,
    jstring j_input_path, jstring j_output_path,
    jboolean strict, jboolean no_orientation)
{
    (void)thiz;

    const char *input_path = (*env)->GetStringUTFChars(env, j_input_path, NULL);
    const char *output_path = (*env)->GetStringUTFChars(env, j_output_path, NULL);

    jlong result[8];
    sp_file f;
    sp_out out;
    sp_policy pol;
    sp_rewrite_stats stats;
    sp_status st;
    int opened_input = 0;

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

    st = sp_jpeg_probe(&f);
    if (st != SP_OK) {
        result[0] = (jlong)st;
        goto respond;
    }

    st = sp_out_open(output_path, &f.attrs, /* force = */ true, &out);
    if (st != SP_OK) {
        result[0] = (jlong)st;
        goto respond;
    }

    st = sp_rewrite(&f, &out, &pol, NULL, &stats);
    if (st != SP_OK) {
        /* Nothing is published: original untouched, temp file removed. */
        sp_out_abort(&out);
        result[0] = (jlong)st;
        goto respond;
    }

    st = sp_out_commit(&out);
    if (st != SP_OK) {
        sp_out_abort(&out);
        result[0] = (jlong)st;
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

respond:
    if (opened_input)
        sp_close(&f);
    if (input_path != NULL)
        (*env)->ReleaseStringUTFChars(env, j_input_path, input_path);
    if (output_path != NULL)
        (*env)->ReleaseStringUTFChars(env, j_output_path, output_path);

    {
        jlongArray jresult = (*env)->NewLongArray(env, 8);
        if (jresult != NULL)
            (*env)->SetLongArrayRegion(env, jresult, 0, 8, result);
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
