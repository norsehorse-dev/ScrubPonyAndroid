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

find_library(log-lib log)
target_link_libraries(scrubpony_jni ${log-lib})
