#!/usr/bin/env bash
#
# revendor_interface.sh — Re-vendor the public AFI920 interface subset.
#
# Copies the public SOME/IP / ISO 23150 interface headers from a local clone of
# the upstream interface repo into
# third_party/afi920_interface/, excluding internal-only parts (raw-data, EOL),
# and updates the recorded upstream commit hash in the vendored README.
#
# Usage:
#   scripts/revendor_interface.sh [INTERFACE_REPO_PATH] [REF]
#
#   INTERFACE_REPO_PATH  Path to a local clone of the interface repo.
#                        If omitted, you are prompted for it.
#   REF                  Git ref (commit/tag/branch) to vendor from.
#                        Defaults to the repo's current HEAD.
#
# Examples:
#   scripts/revendor_interface.sh /path/to/afi920_interface
#   scripts/revendor_interface.sh /path/to/afi920_interface v3.0.1
#
set -euo pipefail

# --- Files under src/c that must NOT be vendored (internal-only). ------------
EXCLUDES=(
  "AFI920/bts_iso23150_eol.h"
  "AFI920/bts_iso23150_raw_data.c"
  "AFI920/bts_iso23150_raw_data.h"
)

# --- Internal-only symbols to strip line-by-line from otherwise-public --------
#     headers. The upstream public headers still declare raw-data / reserved
#     event & interface IDs; the public vendored subset omits them. Each entry
#     is an extended-regex; any line matching is deleted after copy. These are
#     single-line enum/#define entries, so line-based stripping is safe.
STRIP_PATTERNS=(
  'BTS_SOMEIP_EVENT_(PMOI|GENERAL|RAW_DATA)'
  'BTS_IID_RAW_DATA'
  'EVENT_ID_ENUM_(PMOI|GENERAL|RAW_DATA)'
)

# --- Locate the SDK repo root (this script lives in <root>/scripts/). --------
SDK_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$SDK_ROOT/third_party/afi920_interface"
DEST_SRC="$DEST/src/c"
README="$DEST/README.md"

# --- Resolve the interface repo path (arg 1, else prompt). -------------------
SRC="${1:-}"
if [[ -z "$SRC" ]]; then
  read -r -p "Path to local afi920_interface repo: " SRC
fi
SRC="${SRC%/}"

if [[ ! -d "$SRC/.git" && ! -f "$SRC/.git" ]]; then
  echo "ERROR: '$SRC' is not a git repository." >&2
  exit 1
fi
if [[ ! -d "$SRC/src/c" ]]; then
  echo "ERROR: '$SRC/src/c' not found — is this the interface repo?" >&2
  exit 1
fi

# --- Resolve the ref to vendor (arg 2, else current HEAD). -------------------
REF="${2:-HEAD}"
if ! FULL_HASH="$(git -C "$SRC" rev-parse --verify "$REF^{commit}" 2>/dev/null)"; then
  echo "ERROR: ref '$REF' does not resolve in '$SRC'." >&2
  exit 1
fi
SHORT_HASH="$(git -C "$SRC" rev-parse --short "$FULL_HASH")"
SUBJECT="$(git -C "$SRC" log -1 --format='%s' "$FULL_HASH")"

echo "Source repo : $SRC"
echo "Vendoring   : $SHORT_HASH  ($SUBJECT)"
echo "Destination : $DEST_SRC"
echo

# --- Extract src/c at the chosen ref into a temp dir (clean, reproducible). --
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
git -C "$SRC" archive "$FULL_HASH" src/c | tar -x -C "$TMP"

# --- Replace the vendored tree, honoring the exclude list. -------------------
rm -rf "$DEST_SRC"
mkdir -p "$DEST_SRC"

copied=0
skipped=0
while IFS= read -r rel; do
  rel="${rel#./}"
  skip=0
  for ex in "${EXCLUDES[@]}"; do
    if [[ "$rel" == "$ex" ]]; then skip=1; break; fi
  done
  if [[ $skip -eq 1 ]]; then
    skipped=$((skipped + 1))
    continue
  fi
  mkdir -p "$DEST_SRC/$(dirname "$rel")"
  cp "$TMP/src/c/$rel" "$DEST_SRC/$rel"
  copied=$((copied + 1))
done < <(cd "$TMP/src/c" && find . -type f)

echo "Copied $copied file(s); excluded $skipped internal file(s)."

# --- Strip internal-only symbols from the copied public headers. -------------
STRIP_RE="$(IFS='|'; echo "${STRIP_PATTERNS[*]}")"
stripped=0
while IFS= read -r f; do
  n="$(grep -Ec "$STRIP_RE" "$f" || true)"
  if [[ "$n" -gt 0 ]]; then
    sed -i -E "/$STRIP_RE/d" "$f"
    stripped=$((stripped + n))
    echo "  stripped $n internal symbol line(s) from ${f#"$DEST_SRC/"}"
  fi
done < <(find "$DEST_SRC" -type f)
echo "Stripped $stripped internal-symbol line(s) total."

# --- Drop cosmetic churn: restore files whose *content* is unchanged. ---------
#     git archive emits LF; on autocrlf checkouts that flags content-identical
#     files as modified (EOL-only). Restore any vendored file that has no real
#     content diff vs HEAD so the re-vendor diff shows only genuine changes.
restored=0
while IFS= read -r f; do
  rel_git="${f#"$SDK_ROOT/"}"
  if git -C "$SDK_ROOT" cat-file -e "HEAD:$rel_git" 2>/dev/null; then
    if git -C "$SDK_ROOT" diff --quiet -- "$f"; then
      git -C "$SDK_ROOT" checkout -- "$f" 2>/dev/null || true
      restored=$((restored + 1))
    fi
  fi
done < <(find "$DEST_SRC" -type f)
echo "Kept $restored unchanged file(s) at their committed bytes (no EOL churn)."

# --- Update the recorded upstream commit hash in the vendored README. --------
if [[ -f "$README" ]]; then
  # Replace "commit <hash>" with the newly vendored short hash.
  sed -i -E "s/commit [0-9a-f]{7,40}/commit $SHORT_HASH/" "$README"
  echo "Updated $README -> commit $SHORT_HASH"
fi

# --- Summary of what changed in the SDK working tree. ------------------------
echo
echo "=== git status (vendored tree) ==="
git -C "$SDK_ROOT" status --short -- "third_party/afi920_interface" || true
echo
echo "Done. Review the diff, then rebuild/test before committing:"
echo "  git -C \"$SDK_ROOT\" diff -- third_party/afi920_interface"
