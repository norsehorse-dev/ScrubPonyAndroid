cmake_minimum_required(VERSION 3.22.1)
project(scrubpony_jni C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# core/ is copied verbatim from ScrubPonyDesktop/src — see that project's
# README and docs/PLAN.md for why each of these files is shaped the way it
# is. Do not hand-edit copies here; re-copy from the desktop repo instead so
# the two never drift apart.
add_library(scrubpony_jni SHARED
    scrubpony_jni.c
    core/crc32.c
    core/exif.c
    core/io.c
    core/jpeg.c
    core/png.c
    core/pngpolicy.c
    core/pngrewrite.c
    core/webp.c
    core/webppolicy.c
    core/webprewrite.c
    core/heif.c
    core/heifpolicy.c
    core/heifrewrite.c
    core/policy.c
    core/report.c
    core/rewrite.c
)

target_include_directories(scrubpony_jni PRIVATE .)

# Matches ScrubPonyDesktop/Makefile's CPPFLAGS exactly, so the core behaves
# identically here and on macOS/Linux even though bionic is usually more
# permissive about these than glibc is.
target_compile_definitions(scrubpony_jni PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)

target_compile_options(scrubpony_jni PRIVATE -Wall -Wextra -Wpedantic)

# Reproducible builds. F-Droid rebuilds this library from source on its own
# buildserver and compares the result byte for byte with the published
# release, so nothing about the build host may leak into the .so:
#  - -ffile-prefix-map rewrites the absolute source and build directories
#    that clang would otherwise record (__FILE__, DWARF, .comment-adjacent
#    strings) to fixed neutral paths, so the checkout location does not
#    matter. The build dir also carries AGP's per-configuration hash under
#    app/.cxx, which is why it gets its own mapping.
#  - --build-id=sha1 makes the ELF build-id a content hash rather than
#    anything time- or path-derived. It is the NDK default, pinned here so a
#    toolchain change cannot silently switch it.
# Timestamps: the core uses no __DATE__/__TIME__/__TIMESTAMP__, and AGP
# strips the release .so before packaging, so no debug info ships at all.
target_compile_options(scrubpony_jni PRIVATE
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=/scrubpony/cpp"
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=/scrubpony/build"
)
target_link_options(scrubpony_jni PRIVATE "-Wl,--build-id=sha1")

find_library(log-lib log)
target_link_libraries(scrubpony_jni ${log-lib})
