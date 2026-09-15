#!/usr/bin/env bash
# Reproducible-build gate for ScrubPonyAndroid. Same shape as PassPony's:
# it never extracts an APK to the filesystem to compare it (AGP's shortened
# resource names can collide case-insensitively, and a case-insensitive
# filesystem such as macOS silently drops one of each pair). Every
# comparison reads both ZIPs entry by entry via Python's zipfile module, in
# memory, and hashes them.
#
# Usage:
#   tools/verify_repro.sh rebuild <ref> [candidate.apk]
#   tools/verify_repro.sh compare <a.apk> <b.apk>
#   tools/verify_repro.sh content-hash <apk>
#
# rebuild:  clones <ref> twice into isolated roots (separate
#           GRADLE_USER_HOME, --no-daemon), builds each with $GRADLE_TASK,
#           fails unless the two builds are content-identical, then fails
#           unless the optional candidate APK also matches them. Needs an
#           Android SDK with the NDK and CMake pinned in app/build.gradle.kts
#           (ANDROID_HOME or ANDROID_SDK_ROOT exported) and network for the
#           clones. Meant to run inside the docker/release.Dockerfile image
#           (docker run ... verify <ref> [candidate]); a Mac build is not a
#           valid reference for what F-Droid's Linux buildserver produces.
# compare:  content comparison of two APKs via a per-entry SHA-256 manifest,
#           excluding only the signature files
#           (META-INF/*.SF|*.RSA|*.DSA|*.EC|MANIFEST.MF). Prints IDENTICAL or
#           a per-file diff, exits nonzero on any difference. This is the
#           same thing F-Droid checks: an unsigned rebuild must match the
#           published signed APK everywhere except the signature.
# content-hash: SHA-256 of the sorted per-entry manifest (same exclusions),
#           the hash to publish in release notes. Unlike a whole-file hash it
#           does not change with the signature, so anyone rebuilding from
#           source can reproduce it.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_URL="${REPO_URL:-$(git -C "$REPO_ROOT" remote get-url origin 2>/dev/null || echo https://github.com/norsehorse-dev/ScrubPonyAndroid.git)}"
GRADLE_TASK="${GRADLE_TASK:-:app:assembleRelease}"
APK_GLOB="${APK_GLOB:-app/build/outputs/apk/release/app-release*.apk}"

SIG_FILE_RE='^META-INF/([^/]+\.(SF|RSA|DSA|EC)|MANIFEST\.MF)$'

manifest_py() {
  python3 - "$1" "$SIG_FILE_RE" <<'PY'
import sys, zipfile, hashlib, re
apk, sig_re = sys.argv[1], re.compile(sys.argv[2])
with zipfile.ZipFile(apk) as z:
    rows = []
    for info in z.infolist():
        if info.is_dir() or sig_re.match(info.filename):
            continue
        rows.append((info.filename, hashlib.sha256(z.read(info.filename)).hexdigest()))
rows.sort()
for name, digest in rows:
    print(f"{name}\t{digest}")
PY
}

content_hash() {
  manifest_py "$1" | python3 -c "import sys, hashlib; print(hashlib.sha256(sys.stdin.buffer.read()).hexdigest())"
}

print_dex_info() {
  python3 - "$1" "$2" <<'PY'
import sys, zipfile, hashlib, re
apk, label = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(apk) as z:
    for name in sorted(n for n in z.namelist() if re.match(r'^classes\d*\.dex$', n)):
        data = z.read(name)
        digest = hashlib.sha256(data).hexdigest()
        m = re.search(rb'~~R8\{[^}]*\}', data)
        marker = m.group(0).decode('utf-8', 'replace') if m else '(no R8 marker found)'
        print(f"  {label} {name}: sha256={digest}")
        print(f"    {marker}")
PY
}

cmd_content_hash() {
  local apk="${1:?usage: verify_repro.sh content-hash <apk>}"
  content_hash "$apk"
}

cmd_compare() {
  local a="${1:?usage: verify_repro.sh compare <a.apk> <b.apk>}"
  local b="${2:?usage: verify_repro.sh compare <a.apk> <b.apk>}"
  python3 - "$a" "$b" "$SIG_FILE_RE" <<'PY'
import sys, zipfile, hashlib, re
a_path, b_path, sig_re = sys.argv[1], sys.argv[2], re.compile(sys.argv[3])

def manifest(path):
    with zipfile.ZipFile(path) as z:
        out = {}
        for info in z.infolist():
            if info.is_dir() or sig_re.match(info.filename):
                continue
            out[info.filename] = hashlib.sha256(z.read(info.filename)).hexdigest()
        return out

ma, mb = manifest(a_path), manifest(b_path)
only_a = sorted(set(ma) - set(mb))
only_b = sorted(set(mb) - set(ma))
diff = sorted(n for n in (set(ma) & set(mb)) if ma[n] != mb[n])

if not only_a and not only_b and not diff:
    print("IDENTICAL")
    sys.exit(0)

print("DIFFERS")
for n in only_a:
    print(f"  only in A: {n}")
for n in only_b:
    print(f"  only in B: {n}")
for n in diff:
    print(f"  differs:   {n}")
sys.exit(1)
PY
}

find_apk() {
  # $1 = source root. Resolves the APK glob (signed builds produce
  # app-release.apk, unsigned ones app-release-unsigned.apk).
  local root="$1" f
  for f in "$root"/$APK_GLOB; do
    [[ -f "$f" ]] && { printf '%s' "$f"; return 0; }
  done
  return 1
}

cmd_rebuild() {
  local ref="${1:?usage: verify_repro.sh rebuild <ref> [candidate.apk]}"
  local candidate="${2:-}"
  if [[ -z "${ANDROID_HOME:-}${ANDROID_SDK_ROOT:-}" ]]; then
    echo "ANDROID_HOME (or ANDROID_SDK_ROOT) is not set. local.properties is gitignored, so a fresh clone only finds the SDK through the env var." >&2
    exit 1
  fi

  local work
  work="$(mktemp -d "${TMPDIR:-/tmp}/verify-repro.XXXXXX")"
  echo "Work directory: $work"

  local root
  for root in srcA srcB; do
    echo "--- Cloning $ref into $root ---"
    git clone --quiet "$REPO_URL" "$work/$root"
    git -C "$work/$root" checkout --quiet "$ref"
  done

  for root in srcA srcB; do
    echo "--- Building $root ($GRADLE_TASK) ---"
    ( cd "$work/$root" && env GRADLE_USER_HOME="$work/gradle-$root" ./gradlew --no-daemon --no-parallel "$GRADLE_TASK" )
  done

  local apk_a apk_b
  apk_a="$(find_apk "$work/srcA")" || { echo "Build A produced no APK matching $APK_GLOB" >&2; exit 1; }
  apk_b="$(find_apk "$work/srcB")" || { echo "Build B produced no APK matching $APK_GLOB" >&2; exit 1; }

  echo "--- Dex info ---"
  print_dex_info "$apk_a" "A"
  print_dex_info "$apk_b" "B"

  echo "--- Comparing A vs B ---"
  if cmd_compare "$apk_a" "$apk_b"; then
    echo "Two clean builds of $ref are content-identical."
  else
    echo "Two clean builds of $ref DIFFER, not reproducible." >&2
    exit 1
  fi

  echo "Content hash: $(content_hash "$apk_a")"
  echo "buildA APK: $apk_a"
  if [[ -n "${VERIFY_OUT:-}" ]]; then
    # Keep the unsigned reference build around for a second opinion, e.g.
    # apksigcopier compare <signed.apk> --unsigned <this file>, which is the
    # same check F-Droid's verification runs.
    mkdir -p "$VERIFY_OUT"
    cp "$apk_a" "$VERIFY_OUT/"
    echo "Unsigned reference build copied to $VERIFY_OUT/$(basename "$apk_a")"
  fi

  if [[ -n "$candidate" ]]; then
    echo "--- Comparing candidate ($candidate) vs buildA ---"
    if cmd_compare "$candidate" "$apk_a"; then
      echo "Candidate matches the clean-clone build."
    else
      echo "Candidate DOES NOT match the clean-clone build." >&2
      exit 1
    fi
  fi
}

main() {
  local sub="${1:-}"
  [[ $# -gt 0 ]] && shift
  case "$sub" in
    rebuild) cmd_rebuild "$@" ;;
    compare) cmd_compare "$@" ;;
    content-hash) cmd_content_hash "$@" ;;
    *)
      echo "usage: $(basename "$0") rebuild <ref> [candidate.apk] | compare <a.apk> <b.apk> | content-hash <apk>" >&2
      exit 1
      ;;
  esac
}

main "$@"
