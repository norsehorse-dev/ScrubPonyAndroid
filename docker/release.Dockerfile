# Canonical Linux release-build environment for ScrubPony (Android).
#
# Why this exists: from 1.3.1 on, ScrubPony ships as a reproducible build on
# F-Droid. F-Droid rebuilds the tag from source on its own buildserver
# (Debian trixie, JDK 21, the NDK the recipe pins) and compares the result
# byte for byte with the APK attached to the GitHub release. The NDK ships
# separate darwin and linux clang prebuilts under the same version number,
# and they do not always produce identical machine code, so a Mac-built
# release can never be trusted to match. Every release is therefore built
# inside this image, which mirrors the buildserver as closely as a container
# can: same Debian release, same JDK package, same NDK, SDK at the same
# /opt/android-sdk path.
#
# The keystore is never baked into the image or passed as a build arg. It is
# bind-mounted read-only at run time (see docker/README.md), and only the
# storeFile line of keystore.properties is rewritten to the mount point.
#
# --platform=linux/amd64 is pinned on purpose: the NDK only ships an x86_64
# Linux toolchain, and F-Droid builds on x86_64. On an Apple Silicon Mac
# Docker Desktop emulates the whole image (Rosetta or qemu) instead of
# mixing an arm64 rootfs with an x86_64 clang, which is what fails otherwise.
FROM --platform=linux/amd64 debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV NDK_VERSION=26.1.10909125
ENV CMAKE_VERSION=3.22.1
ENV ANDROID_NDK_HOME=${ANDROID_SDK_ROOT}/ndk/${NDK_VERSION}
ENV PATH=${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools:${PATH}

# openjdk-21 is what buildserver-trixie ships and uses by default, so the
# same Debian package here keeps the JDK out of the list of variables.
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl unzip git python3 binutils \
      openjdk-21-jdk-headless \
    && rm -rf /var/lib/apt/lists/*

# Build number in this URL is current as of 2026-09 per
# https://developer.android.com/studio#command-tools. If it 404s, that page
# has the current commandlinetools-linux-*.zip name; only the number
# changes, sdkmanager handles every further install.
RUN mkdir -p "${ANDROID_SDK_ROOT}/cmdline-tools" \
    && curl -sL -o /tmp/cmdline-tools.zip \
      "https://dl.google.com/android/repository/commandlinetools-linux-15859902_latest.zip" \
    && unzip -q /tmp/cmdline-tools.zip -d "${ANDROID_SDK_ROOT}/cmdline-tools" \
    && mv "${ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools" "${ANDROID_SDK_ROOT}/cmdline-tools/latest" \
    && rm /tmp/cmdline-tools.zip

# Exactly what app/build.gradle.kts asks for: compileSdk 34, the pinned NDK
# and the pinned CMake. AGP would fetch missing pieces itself, but pinning
# them here keeps the image self-describing.
RUN yes | sdkmanager --licenses > /dev/null \
    && sdkmanager --install \
      "platform-tools" \
      "platforms;android-34" \
      "build-tools;34.0.0" \
      "ndk;${NDK_VERSION}" \
      "cmake;${CMAKE_VERSION}"

COPY release-entrypoint.sh /usr/local/bin/release-entrypoint.sh
RUN chmod +x /usr/local/bin/release-entrypoint.sh

WORKDIR /work
ENTRYPOINT ["/usr/local/bin/release-entrypoint.sh"]
