# F-Droid submission notes

Reference for the fdroiddata recipe (metadata/com.norsehorse.scrubpony.yml).

- License: Apache-2.0. Category: Graphics.
- Reproducible from 1.3.1 (versionCode 5) on. The recipe carries Binaries
  (https://github.com/norsehorse-dev/ScrubPonyAndroid/releases/download/v%v/ScrubPony-%v.apk)
  and AllowedAPKSigningKeys (SHA-256 of the release signing certificate), so
  F-Droid publishes the developer-signed APK once its rebuild matches.
  1.0.0 to 1.3.0 were Mac-built with R8 off and are not reproducible; the
  recipe starts at 1.3.1 and never lists them.
- Every release is built on Linux in docker/ (Debian trixie, openjdk-21,
  NDK 26.1.10909125, CMake 3.22.1), which mirrors buildserver-trixie. See
  docker/README.md and the "Reproducible builds" section of README.md for
  what is pinned and why.
- Native C core is vendored in app/src/main/cpp/core (copied from the desktop
  ScrubPony), not a submodule. Nothing to check out, no srclibs.
- No proprietary dependencies, so no flavor split. gradle: [yes].
- The core is compiled by the Android Gradle plugin through CMake
  (externalNativeBuild) during the normal build, so no prebuild or build
  block is needed. The recipe only needs ndk: 26.1.10909125 so an NDK is
  provisioned. CMakeLists.txt applies -ffile-prefix-map for the source and
  build dirs and pins --build-id=sha1.
- R8 (minify + resource shrinking) is on for release. The only keep rule is
  the NativeScrubber native methods, since the JNI symbols are statically
  named and the C side never calls back into Kotlin.
- The ART baseline profile task is disabled in app/build.gradle.kts, and
  dependenciesInfo and vcsInfo are off, so no non-reproducible blobs are
  embedded.
- No toolchain or foojay blocks anywhere.
- Recipe commit: must be the full hash of the tag commit, never the tag.
  AutoUpdateMode: Version, UpdateCheckMode: Tags, one build entry per
  recipe update, CurrentVersion/CurrentVersionCode matching.
