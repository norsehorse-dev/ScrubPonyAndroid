# F-Droid submission notes

Reference for the fdroiddata recipe (metadata/com.norsehorse.scrubpony.yml).

- License: Apache-2.0.
- Not reproducible. The app stores no keys or user data, so a signing-key
  difference only costs a reinstall with nothing lost. No Binaries or
  AllowedAPKSigningKeys in the recipe. Can be added later without an app change.
- Native C core is vendored in app/src/main/cpp/core (copied from the desktop
  ScrubPony), not a submodule. Nothing to check out, no srclibs.
- No proprietary dependencies, so no flavor split. gradle: [yes].
- The core is compiled by the Android Gradle plugin through CMake
  (externalNativeBuild) during the normal build, so no prebuild or build
  block is needed. The recipe only needs ndk: 26.1.10909125 so an NDK is
  provisioned.
- dependenciesInfo is disabled in app/build.gradle.kts so no Google
  dependency-metadata blob is embedded.
- No toolchain or foojay blocks anywhere.
