# Release build container

Builds the signed release APK on Linux, in an image that mirrors F-Droid's
buildserver (Debian trixie, openjdk-21, NDK 26.1.10909125, CMake 3.22.1, SDK
at /opt/android-sdk), instead of on macOS. ScrubPony is a reproducible build
on F-Droid from 1.3.1 on: F-Droid rebuilds the tag from source and compares
it byte for byte with the APK attached to the GitHub release, so the release
has to come out of the same host class the buildserver uses. The NDK's
darwin and linux clang prebuilts are not guaranteed to emit identical code
for the C core, so a Mac-built release is not a valid reference.

Requires Docker Desktop (or another local Docker) on the machine holding the
real release keystore. There is no CI integration on purpose: the keystore
stays local and bind-mounted read-only, never baked into the image or passed
as a build arg.

Build the image once, and again whenever `ndkVersion`, the CMake version or
`compileSdk` in `app/build.gradle.kts` changes:

```
cd ~/Apps/ScrubPonyAndroid
docker build -t scrubpony-release -f docker/release.Dockerfile docker
```

Signed release build. `<ref>` is a pushed commit SHA, branch or tag; the
container clones fresh from GitHub and never touches the local working tree,
so the ref has to be on GitHub already. The first `-v` reads the keystore
path straight out of `keystore.properties`:

```
docker run --rm \
  -v "$(sed -n 's/^storeFile=//p' keystore.properties)":/keystore/release.keystore:ro \
  -v ~/Apps/ScrubPonyAndroid/keystore.properties:/keystore-props/keystore.properties:ro \
  -v ~/Apps/ScrubPonyAndroid:/out \
  scrubpony-release release <version> <ref>
```

Results land in `release-<version>/` under the repo root (gitignored):
`ScrubPony-<version>.apk` and its `.sha256`. The entrypoint refuses to hand
back an APK that still carries an ART baseline profile, a
version-control-info file, or a host path inside `libscrubpony_jni.so`.

Reproducibility check, no keystore needed. Clones the ref twice, builds each
clean in its own Gradle home, compares the two, then compares the signed
release against them with the signature files excluded (the same comparison
F-Droid runs):

```
docker run --rm \
  -v ~/Apps/ScrubPonyAndroid:/out \
  scrubpony-release verify <ref> /out/release-<version>/ScrubPony-<version>.apk
```

The unsigned reference build from that run is left at
`release-verify-<ref>/app-release-unsigned.apk` (gitignored), so the same
comparison can be repeated on the host with apksigcopier, which is the tool
F-Droid's own verification uses:

```
python3 -m pip install --user apksigcopier
apksigcopier compare release-<version>/ScrubPony-<version>.apk --unsigned release-verify-<ref>/app-release-unsigned.apk
```

Apple Silicon note: the image is pinned to linux/amd64 and runs emulated.
R8 under emulation needs memory; give Docker Desktop 12 GB or so. The
entrypoint already caps Gradle at `-Xmx6g`, two workers, no parallel, and
runs without the daemon, so a build that dies with "Gradle build daemon
disappeared unexpectedly" is the kernel OOM killer, not a build error. Raise
the Docker memory limit and rerun.
