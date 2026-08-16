# ScrubPony for Android

Strips identifying metadata out of JPEGs on your phone, the same way the
[desktop `scrubpony`](https://github.com/norsehorse-dev/ScrubPony) does:
losslessly, no re-encoding, no dependencies beyond what's already in the
repo.

> **Status: early scaffold, not yet built or run on a device.** This was
> generated in an environment with no access to the Android SDK/NDK or
> Google's Maven repo, so it has not been through a real Gradle build. See
> [Verification status](#verification-status) below for exactly what has and
> has not been checked, and [First build](#first-build) for what to expect
> the first time you open it.

## What it does

Two entry points into the same scrubbing pipeline:

- **Share sheet.** Share one or more photos from Google Photos, your
  gallery, a messaging app — anything — and pick "Scrub metadata." ScrubPony
  strips GPS, timestamps, device info and embedded thumbnails, then lets you
  save the clean copies or share them straight back out.
- **In-app picker.** Open the app directly and pick photos from your library
  via Android's built-in Photo Picker (no storage permission needed).

Either way, the original files are never touched — ScrubPony only ever
produces new, clean copies, saved to `Pictures/ScrubPony` and/or shared
onward.

## How it's built

The metadata parsing and policy logic is **not reimplemented**. It's the
exact same C core from ScrubPonyDesktop — `exif.c`, `io.c`, `jpeg.c`,
`policy.c`, `report.c`, `rewrite.c` — copied verbatim into
`app/src/main/cpp/core/` and compiled through the Android NDK. That's the
code with 741 assertions, 7.5 million fuzzer executions, and the atomic
write-temp-rename guarantee behind it; none of that is redone or re-earned
here; this is why one of the first setup questions asked, when this project
was scaffolded, was C-core-via-JNI vs. a Kotlin rewrite. C-core-via-JNI is
what got built.

```
app/src/main/cpp/
  scrubpony_jni.c    JNI glue only — opens the two paths, calls sp_open_read
                      / sp_jpeg_probe / sp_out_open / sp_rewrite / sp_out_commit
                      exactly like main.c's handle_scrub() does, packs the
                      resulting sp_rewrite_stats into a long[8]
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
  ScrubStats.kt        decodes the packed long[8] — deliberately has zero
                        Android imports, so it's plain-JVM unit-testable
  ScrubEngine.kt        content:// -> cache file -> native call -> tally,
                        mirrors summarise() in the desktop main.c
  SaveExporter.kt      MediaStore export + FileProvider share intent
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

- **The C core compiles and links clean** against the real JNI headers
  (OpenJDK's `jni.h`), with the identical `_POSIX_C_SOURCE` /
  `_DEFAULT_SOURCE` defines the desktop Makefile uses — `-Wall -Wextra
  -Wpedantic`, zero warnings.
- **The JNI bridge actually runs**, end to end, against three of the
  desktop project's own synthetic fixtures, loaded as a plain shared library
  on Linux (not through Android, but through the real, unmodified JNI call
  path): `phone_gps.jpg`, `kitchen_sink.jpg`, `clean_no_metadata.jpg`. The
  numbers matched the desktop README's own worked example exactly —
  `kitchen_sink.jpg` dropped 5 segments and preserved orientation, same as
  the `-n` dry-run output documented there.
- **The Kotlin decode logic has a real unit test**
  (`NativeScrubberDecodeTest.kt`) that runs under plain `./gradlew test`.
- **Everything that needs the Android SDK, NDK, or AGP/Compose/androidx
  dependency resolution is unverified.** That's the Gradle sync itself, the
  actual NDK cross-compile for `arm64-v8a` etc., every Kotlin file that
  imports `android.*` or `androidx.*`, the manifest, the resources, and the
  UI. This is scaffolded to the best of my knowledge of current AGP/Compose
  APIs, not compiled.

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
- **The native build should be the safe part** — see Verification status
  above.

```
./gradlew test           runs NativeScrubberDecodeTest, no device needed
./gradlew assembleDebug  builds the full APK, including the NDK cross-compile
```

## What it doesn't do (yet)

Same scope line as the desktop tool, plus Android-specific gaps:

- JPEG only — same as desktop. No HEIC, which is what a lot of Android
  camera apps actually save by default; worth deciding early whether that's
  a v2 priority, since without it "share a photo from your gallery" will
  silently no-op (reported as "not JPEG") for a chunk of real phones' camera
  rolls.
- No settings persistence — `strict` / `keepOrientation` reset to defaults
  every launch.
- No app icon beyond a placeholder vector mark.
- No instrumented (on-device) tests yet, only the plain-JVM decode test.

## Licence

MIT, same as the desktop project.
