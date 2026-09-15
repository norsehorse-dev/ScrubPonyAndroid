#!/usr/bin/env bash
# Runs inside the docker/release.Dockerfile image. Not meant to be run on a
# host directly; docker/README.md has the docker run invocations.
#
#   release <version> <ref>          signed release build of <ref>
#   verify  <ref> [candidate.apk]    two clean builds of <ref>, compared with
#                                    each other and optionally with a
#                                    published/signed APK (signature files
#                                    excluded), no keystore needed
set -euo pipefail

REPO_URL="${REPO_URL:-https://github.com/norsehorse-dev/ScrubPonyAndroid.git}"
MODE="${1:?usage: release <version> <ref> | verify <ref> [candidate.apk]}"
shift

# Harden Gradle for amd64 emulation on Apple Silicon: an unbounded R8 run
# gets OOM-killed under qemu/Rosetta ("Gradle build daemon disappeared
# unexpectedly"). These only bound parallelism and heap, never output bytes,
# so they are safe to force. An explicit -e on the docker run still wins.
export GRADLE_OPTS="${GRADLE_OPTS:--Dorg.gradle.jvmargs=-Xmx6g -Dorg.gradle.workers.max=2 -Dorg.gradle.parallel=false}"

clone_at() {
  local ref="$1" dest="$2"
  echo "--- Cloning ScrubPonyAndroid at $ref into $dest ---"
  git clone --quiet "$REPO_URL" "$dest"
  git -C "$dest" checkout --quiet "$ref"
  echo "HEAD is $(git -C "$dest" rev-parse HEAD)"
}

check_apk() {
  # Cheap post-build sanity checks that catch the two known reproducibility
  # leaks before anything gets tagged or published.
  local apk="$1"
  echo "--- Checking $apk ---"
  if unzip -l "$apk" | grep -q -i 'assets/dexopt/baseline'; then
    echo "FAIL: baseline.prof/.profm still present (ART profile task not disabled, or a non-clean build)" >&2
    exit 1
  fi
  echo "ok: no ART baseline profile in the APK"
  if unzip -l "$apk" | grep -q 'META-INF/version-control-info'; then
    echo "FAIL: META-INF/version-control-info.textproto present (vcsInfo.include is not false)" >&2
    exit 1
  fi
  echo "ok: no version-control-info in the APK"
  local tmp
  tmp="$(mktemp -d)"
  unzip -q -o "$apk" 'lib/*' -d "$tmp"
  local leaked=0 so
  for so in "$tmp"/lib/*/libscrubpony_jni.so; do
    if strings "$so" | grep -E -q '^(/Users/|/home/|/root/|/work/|/tmp/)'; then
      echo "FAIL: host path embedded in $so:" >&2
      strings "$so" | grep -E '^(/Users/|/home/|/root/|/work/|/tmp/)' >&2
      leaked=1
    fi
  done
  rm -rf "$tmp"
  [[ $leaked -eq 0 ]] || exit 1
  echo "ok: no host paths in libscrubpony_jni.so"
}

case "$MODE" in
  release)
    VERSION="${1:?usage: release <version> <ref>, e.g. release 1.3.1 v1.3.1}"
    REF="${2:?usage: release <version> <ref>, e.g. release 1.3.1 v1.3.1}"
    [[ -f /keystore/release.keystore ]] || {
      echo "Expected the release keystore bind-mounted read-only at /keystore/release.keystore" >&2
      exit 1
    }
    [[ -f /keystore-props/keystore.properties ]] || {
      echo "Expected keystore.properties bind-mounted read-only at /keystore-props/keystore.properties" >&2
      exit 1
    }
    clone_at "$REF" /work/repo
    cd /work/repo
    CURRENT="$(sed -n 's/^ *versionName = "\(.*\)"/\1/p' app/build.gradle.kts)"
    [[ "$CURRENT" == "$VERSION" ]] || {
      echo "app/build.gradle.kts at $REF has versionName \"$CURRENT\", not \"$VERSION\"" >&2
      exit 1
    }
    # keystore.properties on the host points storeFile at a host path that
    # does not exist in here. Rewrite only that line; the passwords and
    # alias pass through untouched.
    sed 's#^storeFile=.*#storeFile=/keystore/release.keystore#' \
      /keystore-props/keystore.properties > keystore.properties
    echo "--- ./gradlew clean assembleRelease ---"
    ./gradlew --no-daemon --no-parallel clean assembleRelease
    APK=app/build/outputs/apk/release/app-release.apk
    [[ -f "$APK" ]] || { echo "Expected $APK not found (did signing get skipped?)" >&2; exit 1; }
    check_apk "$APK"
    OUT="/out/release-$VERSION"
    mkdir -p "$OUT"
    cp "$APK" "$OUT/ScrubPony-$VERSION.apk"
    ( cd "$OUT" && sha256sum "ScrubPony-$VERSION.apk" > "ScrubPony-$VERSION.apk.sha256" && cat "ScrubPony-$VERSION.apk.sha256" )
    echo "Content hash (signature files excluded): $(bash tools/verify_repro.sh content-hash "$APK")"
    echo "Done. Results at release-$VERSION/ on the host."
    ;;
  verify)
    REF="${1:?usage: verify <ref> [candidate.apk]}"
    CANDIDATE="${2:-}"
    clone_at "$REF" /work/repo
    cd /work/repo
    # The unsigned reference build lands next to the release output so it
    # can be checked again on the host (see docker/README.md).
    export VERIFY_OUT="/out/release-verify-$(echo "$REF" | tr '/' '_')"
    if [[ -n "$CANDIDATE" ]]; then
      bash tools/verify_repro.sh rebuild "$REF" "$CANDIDATE"
    else
      bash tools/verify_repro.sh rebuild "$REF"
    fi
    ;;
  *)
    echo "usage: release <version> <ref> | verify <ref> [candidate.apk]" >&2
    exit 1
    ;;
esac
