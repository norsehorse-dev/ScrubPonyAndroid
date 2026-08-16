# ScrubPony for Android

Strips identifying metadata out of JPEGs, PNGs, WebP and HEIC on your phone,
the same way the [desktop `scrubpony`](https://github.com/norsehorse-dev/ScrubPony)
does: losslessly, no re-encoding, no dependencies beyond what's already in the
repo.

> **Status: scaffold plus PNG, WebP and HEIC updates, still not built or run
> on a real device.** All were done in an environment with no access to the
> Android SDK/NDK or Google's Maven repo, so none has been through a real
> Gradle build. See [Verification status](#verification-status) below for
> exactly what has and has not been checked, and [First build](#first-build)
> for what to expect the first time you open it.

## What it does

Two entry points into the same scrubbing pipeline:

- **Share sheet.** Share one or more photos from Google Photos, your
  gallery, a messaging app, anything, and pick "Scrub metadata." ScrubPony
  detects JPEG, PNG, WebP or HEIC automatically, strips GPS, timestamps,
  device info and embedded thumbnails, then lets you save the clean copies or
  share them straight back out.
- **In-app picker.** Open the app directly and pick photos from your library
  via Android's built-in Photo Picker (no storage permission needed).

Once a batch is done, there are three ways to get the clean copies out:
"Save to Pictures" drops them straight into the `Pictures/ScrubPony`
MediaStore collection, "Save to files" opens Android's folder picker so you
can choose any location (a specific folder, an SD card, a synced cloud
folder, whatever the picker offers), and "Share clean copies" opens the
system share sheet to hand them to another app directly.

Either way, the original files are never touched — ScrubPony only ever
produces new, clean copies.

## How it's built

The metadata parsing and policy logic is **not reimplemented**. It's the
exact same C core from ScrubPonyDesktop — `crc32.c`, `exif.c`, `io.c`,
`jpeg.c`, `png.c`/`pngpolicy.c`/`pngrewrite.c`, `webp.c`/`webppolicy.c`/`webprewrite.c`,
`heif.c`/`heifpolicy.c`/`heifrewrite.c`, `policy.c`, `report.c`, `rewrite.c` —
copied verbatim into `app/src/main/cpp/core/` and compiled through the Android
NDK. That's the code with the desktop project's own
sanitizer-clean test suite and fuzz coverage behind it (741 assertions and
7.5 million fuzzer executions on the JPEG side as of 1.0; see the desktop
README's [PNG support](https://github.com/norsehorse-dev/ScrubPony#png-support)
section for what backs the newer PNG path specifically). None of that is
redone or re-earned here; this is why one of the first setup questions asked,
when this project was scaffolded, was C-core-via-JNI vs. a Kotlin rewrite.
C-core-via-JNI is what got built.

```
app/src/main/cpp/
  scrubpony_jni.c    JNI glue only — detects JPEG / PNG / WebP / HEIC the
                      same way main.c's detect_format() does (sp_jpeg_probe,
                      then sp_png_probe, sp_webp_probe, sp_heif_probe, each on
                      a rewind), calls sp_open_read / sp_out_open / the matching
                      sp_*_rewrite / sp_out_commit exactly like main.c's
                      handle_scrub() does, packs the resulting sp_rewrite_stats
                      plus the detected format code (0=JPEG 1=PNG 2=WebP 3=HEIC)
                      into a long[9]
  CMakeLists.txt      builds scrubpony_jni.c + core/*.c into libscrubpony_jni.so
  core/               unmodified copy of ScrubPonyDesktop/src (minus main.c
                      and walk.c — no CLI arg parsing or directory recursion
                      needed here; the Photo Picker / share sheet already
                      hand over a flat list of items)
```

Android hands the app `content://` URIs, not filesystem paths, and the C
core needs real paths (that's where the atomic rename lives). So
`ScrubEngine.kt` copies each input into the app's private cache dir first,
runs the native core against that real file, and the scrubbed output lands
back in the cache dir too — the same directory `FileProvider` and the
`Pictures/ScrubPony` MediaStore export both read from.

```
app/src/main/kotlin/com/norsehorse/scrubpony/
  NativeScrubber.kt   external fun declarations + System.loadLibrary
  ScrubStats.kt        decodes the packed long[9] — deliberately has zero
                        Android imports, so it's plain-JVM unit-testable
  ScrubEngine.kt        content:// -> cache file -> native call -> tally,
                        mirrors summarise() in the desktop main.c; also
                        renames the output to match whatever format the
                        native side actually detected
  SaveExporter.kt      MediaStore export, Storage Access Framework export to
                        a user-picked folder, and the FileProvider share
                        intent; MIME type for all three picked from the (now
                        format-correct) extension
  ScrubViewModel.kt   batch state machine (Idle / Processing / Done)
  MainActivity.kt      SEND / SEND_MULTIPLE intent handling, Photo Picker
  ui/ScrubScreen.kt    the one screen: options, progress, per-file results,
                        summary line phrased like the CLI's
```

No permissions are declared or requested. Reading a shared or picked photo
needs none (the sender/picker grants a scoped read on the URI), and writing
into `Pictures/ScrubPony` via MediaStore needs none either — `minSdk 29`
means every device this runs on uses scoped storage, where an app can always
write into MediaStore collections it created entries in.

## Verification status

What's actually been checked, from inside an environment with no Android
SDK, no NDK, and no access to `dl.google.com` / Google's Maven / the Gradle
Plugin Portal:

- **The C core compiles clean** with the identical `_POSIX_C_SOURCE` /
  `_DEFAULT_SOURCE` defines the desktop Makefile uses — `-Wall -Wextra
  -Wpedantic`, zero warnings. Re-verified after the WebP and HEIC updates: all
  32 `core/*.c` files (now including `webp*.c` and `heif*.c`) compile clean,
  and `scrubpony_jni.c` type-checks against a `jni.h` matching the signatures
  it uses. This re-verification ran on the target Mac itself, so it is the same
  clang the NDK host tooling sits on top of. The desktop core these files are
  copied from carries its own sanitizer-clean unit suite (now 1,400+ assertions
  across all four formats) and over a million fuzz iterations per newer format.
- **The JNI bridge actually runs**, end to end, against real fixtures, loaded
  as a plain shared library on Linux (not through Android, but through the
  real, unmodified JNI call path, `GetStringUTFChars`/`NewLongArray` and all):
  `kitchen_sink.jpg` (dropped 5 segments, GPS present, orientation preserved,
  `isPng=0`) and the desktop test suite's `png_metadata.png` fixture (dropped
  7 chunks, GPS present, orientation preserved, `isPng=1`, out_size 115 —
  matching the desktop CLI's own scrub of the same file exactly). Format
  detection was exercised for real, not assumed: the same `scrubFile` call
  correctly told a JPEG and a PNG apart and returned the right numbers for
  each.
- **The Kotlin decode logic has a real unit test**
  (`NativeScrubberDecodeTest.kt`) that runs under plain `./gradlew test`.
- **Everything that needs the Android SDK, NDK, or AGP/Compose/androidx
  dependency resolution is unverified.** That's the Gradle sync itself, the
  actual NDK cross-compile for `arm64-v8a` etc., every Kotlin file that
  imports `android.*` or `androidx.*`, the manifest, the resources, and the
  UI. This is scaffolded to the best of my knowledge of current AGP/Compose
  APIs, not compiled.
- **The "Save to files" button is new and falls in that same unverified
  bucket.** It uses `ActivityResultContracts.OpenDocumentTree()` and
  `DocumentFile` to let the user pick any folder through the system picker,
  same idea as `saveToGallery` but not limited to the `Pictures/ScrubPony`
  MediaStore collection. Added because the system share sheet's set of
  targets (whether a "Files" app shows up at all) is out of this app's
  control and varies by device, so this gives a reliable path to an
  arbitrary folder that doesn't depend on what's installed. Needs the new
  `androidx.documentfile` dependency declared in `app/build.gradle.kts`.

The gradle wrapper (`gradlew`, `gradle/wrapper/gradle-wrapper.jar`) is real
and untouched by any of that — it was generated by an actual Gradle
installation, not hand-authored, and points at Gradle 8.7.

## First build

Open the project root in Android Studio (Ladybug or newer). It will prompt
to install the NDK (pinned to `26.1.10909125` in `app/build.gradle.kts`) and
CMake `3.22.1` on first sync — accept that.

Realistic expectations for a first sync of a scaffold like this:

- **Dependency versions may need a bump.** AGP 8.5.2, Kotlin 1.9.24, and
  Compose BOM `2024.06.00` were current knowledge as of this scaffold's
  writing, not verified against what's actually latest right now. If
  Android Studio's upgrade assistant suggests newer versions, that's
  expected and fine to accept.
- **Minor Kotlin/Compose API drift is possible** in files that couldn't be
  compiler-checked here — most likely `ScrubScreen.kt` or `MainActivity.kt`,
  since those touch the most androidx surface area. If something doesn't
  resolve, it's very likely a small API rename, not a logic problem.
- **The PNG update's Kotlin changes** (`ScrubEngine.kt`'s format-aware output
  naming, `SaveExporter.kt`'s MIME type lookup, the `long[9]` decode in
  `ScrubStats.kt`) were reasoned through carefully and cross-checked against
  the real JNI array the native side now returns, but — same limitation as
  the rest of this scaffold — no `kotlinc` or AGP was available here to
  actually compile them. `NativeScrubberDecodeTest.kt` covers the decode
  logic's shape once `./gradlew test` can run.
- **The native build should be the safe part** — see Verification status
  above.

```
./gradlew test           runs NativeScrubberDecodeTest, no device needed
./gradlew assembleDebug  builds the full APK, including the NDK cross-compile
```

## What it doesn't do (yet)

Same scope line as the desktop tool, plus Android-specific gaps:

- JPEG, PNG, WebP and HEIC — the full set the desktop core supports as of
  1.3. HEIC is what a lot of phones save by default, so this closes the gap
  that used to report a big chunk of real camera rolls as unsupported. A HEIC
  with an exotic internal layout (external data references, image sequences)
  is reported as unsupported and left untouched, matching the desktop core;
  AVIF (same container as HEIC) is not handled yet.
- No settings persistence — `strict` / `keepOrientation` reset to defaults
  every launch.
- No app icon beyond a placeholder vector mark.
- No instrumented (on-device) tests yet, only the plain-JVM decode test.

## Licence

MIT, same as the desktop project.
