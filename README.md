# ScrubPony for Android

Strips identifying metadata out of JPEG, PNG, WebP and HEIC photos on your
phone, the same way the [desktop ScrubPony](https://github.com/norsehorse-dev/ScrubPony)
does: losslessly, no re-encoding, the pixels left byte for byte the same.

Everything runs on the device. Nothing is uploaded, no network is used, and
the app declares no permissions. Your originals are never changed; every
scrub writes a fresh clean copy.

## What it does

Two ways in, one scrubbing pipeline:

- Share sheet. Share one or more photos from your gallery, a messaging app,
  anything, and pick "Scrub metadata." ScrubPony detects the format
  automatically, drops GPS, timestamps, device info and embedded thumbnails,
  and hands back clean copies.
- In-app picker. Open the app and choose photos two ways, neither needing a
  storage permission: the system Photo Picker for your gallery, or "Pick from
  Files" (the Storage Access Framework document picker) to reach anything in
  Files, Downloads, an SD card or a synced cloud folder. The Files path
  matters for HEIC especially, because the Photo Picker only surfaces visual
  media the gallery has indexed, so HEICs sitting in Files often do not show
  up there.

Once a batch is done there are three ways out: "Save to Pictures" drops the
copies into the `Pictures/ScrubPony` MediaStore collection, "Save to Files"
opens the folder picker so you can choose any location, and "Share clean
copies" opens the system share sheet.

## The app

A dark, themed interface built with Jetpack Compose and Material 3. A bottom
bar holds two tabs, Scrub and Settings. A first-run tour explains what the
app does; it can be replayed later from Settings. The Settings screen carries
the scrub options (keep orientation, strict), a language picker, links to the
source and the rest of the tools, and the open-source licenses.

The interface is localised in English, German, Spanish, French, Japanese and
Brazilian Portuguese, switchable in-app.

## Formats and limits

JPEG, PNG, WebP and HEIC, the full set the desktop core supports. HEIC is
what a lot of phones save by default. A HEIC with an exotic internal layout
(external data references, image sequences) is reported as unsupported and
left untouched, matching the desktop core. AVIF, which shares the HEIC
container, is not handled yet.

The `strict` and `keepOrientation` options reset to their defaults each
launch; the chosen language and the first-run state are persisted.

## How it's built

The metadata parsing and policy logic is not reimplemented. It is the same C
core as the desktop ScrubPony, copied verbatim into `app/src/main/cpp/core/`
and compiled through the Android NDK. That is the code with the desktop
project's sanitizer-clean test suite (1,400+ assertions across all four
formats) and fuzz coverage behind it. None of that is redone here.

```
app/src/main/cpp/
  scrubpony_jni.c   JNI glue only: detects JPEG / PNG / WebP / HEIC the same
                    way the desktop main.c does, calls the matching
                    sp_*_rewrite, and packs the stats plus a format code into
                    a long[9]
  CMakeLists.txt    builds scrubpony_jni.c + core/*.c into libscrubpony_jni.so
  core/             unmodified copy of the desktop src (minus main.c and
                    walk.c: no CLI parsing or directory recursion needed)

app/src/main/kotlin/com/norsehorse/scrubpony/
  NativeScrubber.kt      external fun declarations + System.loadLibrary
  ScrubStats.kt          decodes the packed long[9]; zero Android imports, so
                         it is plain-JVM unit-testable
  ScrubEngine.kt         content:// to cache file to native call to tally
  SaveExporter.kt        MediaStore, Storage Access Framework and FileProvider
  ScrubViewModel.kt      batch state machine (Idle / Processing / Done)
  Theme.kt               design tokens matched to scrubpony.app
  MainActivity.kt        bottom nav, share intents, pickers, onboarding gate
  i18n/LanguageManager.kt          in-app language switching
  ui/ScrubScreen.kt                the scrub tab
  ui/onboarding/OnboardingScreen.kt first-run tour
  ui/settings/SettingsScreen.kt    settings, options, links, about
  ui/settings/Links.kt             every external link, one place
  ui/settings/LanguagePickerScreen.kt
  ui/settings/LicensesScreen.kt
```

Android hands the app `content://` URIs, not filesystem paths, and the C core
needs real paths (that is where the atomic rename lives). `ScrubEngine.kt`
copies each input into the app's private cache dir first, runs the core
against that real file, and the scrubbed output lands back in the cache dir,
the same directory the MediaStore export and the share provider read from.

No permissions are declared or requested. Reading a shared or picked photo
needs none (the sender grants a scoped read on the URI), and writing into
`Pictures/ScrubPony` needs none either: `minSdk 29` means every device uses
scoped storage, where an app can always write into MediaStore collections it
created.

## Building

Open the project in Android Studio, or from the command line:

```
./gradlew test
./gradlew installDebug
./gradlew assembleRelease
```

`./gradlew test` runs the plain-JVM decode test with no device. The first
build installs the NDK (`26.1.10909125`) and CMake (`3.22.1`) pinned in
`app/build.gradle.kts`. Toolchain: AGP 8.5.2, Kotlin 1.9.24, Compose BOM
2024.06.00, `minSdk 29`, `targetSdk 34`.

## Reproducible builds

From 1.3.1 on, releases are reproducible: F-Droid rebuilds each tag from
source and ships the developer-signed APK when its build matches the one
attached to the GitHub release byte for byte (signature aside). That means
installs from GitHub and from F-Droid share one signature and update over
each other.

What makes the build deterministic lives in three places. `app/build.gradle.kts`
disables the ART baseline profile (AGP does not serialize it reproducibly),
keeps the Google dependency-metadata blob and the version-control-info file
out of the APK, and runs R8 with a single keep rule for the JNI boundary.
`app/src/main/cpp/CMakeLists.txt` maps the source and build directories to
fixed neutral paths and pins a content-hash build-id, so nothing about the
build host reaches `libscrubpony_jni.so`. And every release is built on Linux
inside the container in `docker/`, which mirrors F-Droid's buildserver
(Debian trixie, openjdk-21, the pinned NDK and CMake), because the NDK's
macOS and Linux compilers are not guaranteed to produce identical code.

`tools/verify_repro.sh` is the gate: it builds a ref twice from clean clones,
checks the two match, and checks a published APK against them.
`docker/README.md` has the exact commands.

## License

Apache-2.0. See [LICENSE](LICENSE).
