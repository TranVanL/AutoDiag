#!/usr/bin/env bash
# VDiag AIDL freeze / backward-compat diff helper.
#
# Usage:
#   bash scripts/freeze_aidl.sh --diff <current_dir> <frozen_version>
#       Compare current AIDL sources against a frozen version snapshot.
#       Exits 0 if only backward-compatible changes are detected.
#       Exits 1 if a breaking change is detected.
#
#   bash scripts/freeze_aidl.sh --snapshot <frozen_version>
#       Copy current AIDL sources into a new frozen version directory.
#
# The "current" source of truth is android/aidl_hal/com/vdiag/hal/.
# Frozen snapshots live under android/aidl_hal/v<version>/.
# (Production AAOS uses aidl_api/<interface-name>/<version>/ via Soong,
# but this repo keeps a lightweight v<version>/ mirror for CI.)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
AIDL_ROOT="${REPO_ROOT}/android/aidl_hal"
CURRENT_DIR="${AIDL_ROOT}/com/vdiag/hal"

usage() {
    cat <<EOF
Usage:
  $0 --diff <current_dir> <frozen_version>
  $0 --snapshot <frozen_version>

Examples:
  $0 --diff current 1
  $0 --snapshot 2
EOF
    exit 1
}

log_info()  { echo "[freeze_aidl] INFO: $*"; }
log_warn()  { echo "[freeze_aidl] WARN: $*"; }
log_error() { echo "[freeze_aidl] ERROR: $*" >&2; }

# Normalize an AIDL file for comparison:
#   - strip block comments /* ... */
#   - strip line comments // ...
#   - collapse consecutive blank lines
#   - trim leading/trailing whitespace
normalize_aidl() {
    local file="$1"
    # 1. Remove /* ... */ comments (non-greedy approximation across lines).
    # 2. Remove // comments.
    # 3. Strip leading/trailing whitespace.
    # 4. Collapse blank runs.
    perl -0777 -pe 's#/\*.*?\*/##gs' "$file" \
        | sed 's#//.*##' \
        | sed 's/^[[:space:]]*//; s/[[:space:]]*$//' \
        | sed '/^$/N;/^\n$/D' \
        | grep -v '^$'
}

# Compare two AIDL files semantically for breaking changes.
# Breaking changes detected here:
#   - method removed
#   - method reordered
#   - method signature changed (return type / parameter list / direction)
#   - parcelable field removed
#   - parcelable field reordered
#   - @VintfStability annotation removed
#
# Allowed changes:
#   - new method appended at end of interface
#   - new parcelable field appended at end
#   - comment / whitespace / import order changes
is_breaking_change() {
    local old_file="$1"
    local new_file="$2"
    local filename
    filename="$(basename "$old_file")"

    local old_norm new_norm
    old_norm="$(normalize_aidl "$old_file")"
    new_norm="$(normalize_aidl "$new_file")"

    if [[ "$old_norm" == "$new_norm" ]]; then
        return 1  # no change at all
    fi

    # Interface: extract method signatures in declaration order.
    if [[ "$filename" == *.aidl ]] && grep -q '^interface ' "$old_file"; then
        local old_methods new_methods
        old_methods="$(echo "$old_norm" | grep -E '^[[:space:]]*[a-zA-Z0-9_<>\[\]]+[[:space:]]+[a-zA-Z_][a-zA-Z0-9_]*\s*\(' || true)"
        new_methods="$(echo "$new_norm" | grep -E '^[[:space:]]*[a-zA-Z0-9_<>\[\]]+[[:space:]]+[a-zA-Z_][a-zA-Z0-9_]*\s*\(' || true)"

        # Check that every old method still exists unchanged and in the same prefix order.
        local idx=0
        while IFS= read -r method; do
            [ -z "$method" ] && continue
            # Build a regex that matches this exact method line somewhere in new_methods.
            local escaped
            escaped="$(printf '%s' "$method" | sed 's/[][^$.*+?{}\\|()]/\\&/g')"
            if ! echo "$new_methods" | awk -v m="$method" 'BEGIN{found=0} index($0,m)==1{found=1; exit} END{exit !found}'; then
                log_error "Breaking change in $filename: method removed or signature changed: $method"
                return 0
            fi
            idx=$((idx + 1))
        done <<< "$old_methods"

        # Method reordering: the first N methods of new_methods must match old_methods exactly.
        local old_count new_count
        old_count="$(echo "$old_methods" | grep -c '^' || true)"
        new_count="$(echo "$new_methods" | grep -c '^' || true)"
        if [ "$old_count" -gt 0 ] && [ "$new_count" -gt 0 ]; then
            local old_prefix new_prefix
            old_prefix="$(echo "$old_methods" | head -n "$old_count")"
            new_prefix="$(echo "$new_methods" | head -n "$old_count")"
            if [ "$old_prefix" != "$new_prefix" ]; then
                log_error "Breaking change in $filename: existing methods were reordered."
                return 0
            fi
        fi

        # @VintfStability must not be removed.
        if grep -q '@VintfStability' "$old_file" && ! grep -q '@VintfStability' "$new_file"; then
            log_error "Breaking change in $filename: @VintfStability annotation removed."
            return 0
        fi

        return 1  # only backward-compatible additions
    fi

    # Parcelable: extract field declarations in declaration order.
    if [[ "$filename" == *.aidl ]] && grep -q '^parcelable ' "$old_file"; then
        local old_fields new_fields
        old_fields="$(echo "$old_norm" | grep -E '^[[:space:]]*[a-zA-Z0-9_<>\[\]]+[[:space:]]+[a-zA-Z_][a-zA-Z0-9_]*\s*;' || true)"
        new_fields="$(echo "$new_norm" | grep -E '^[[:space:]]*[a-zA-Z0-9_<>\[\]]+[[:space:]]+[a-zA-Z_][a-zA-Z0-9_]*\s*;' || true)"

        while IFS= read -r field; do
            [ -z "$field" ] && continue
            if ! echo "$new_fields" | awk -v f="$field" 'BEGIN{found=0} index($0,f)==1{found=1; exit} END{exit !found}'; then
                log_error "Breaking change in $filename: field removed or type changed: $field"
                return 0
            fi
        done <<< "$old_fields"

        local old_count new_count
        old_count="$(echo "$old_fields" | grep -c '^' || true)"
        new_count="$(echo "$new_fields" | grep -c '^' || true)"
        if [ "$old_count" -gt 0 ] && [ "$new_count" -gt 0 ]; then
            local old_prefix new_prefix
            old_prefix="$(echo "$old_fields" | head -n "$old_count")"
            new_prefix="$(echo "$new_fields" | head -n "$old_count")"
            if [ "$old_prefix" != "$new_prefix" ]; then
                log_error "Breaking change in $filename: existing fields were reordered."
                return 0
            fi
        fi

        if grep -q '@VintfStability' "$old_file" && ! grep -q '@VintfStability' "$new_file"; then
            log_error "Breaking change in $filename: @VintfStability annotation removed."
            return 0
        fi

        return 1
    fi

    # Fallback: any textual difference in non-interface/non-parcelable files is flagged.
    log_error "Breaking change: unstructured difference in $filename."
    return 0
}

do_diff() {
    local current_arg="$1"
    local version="$2"
    local frozen_dir

    if [ "$current_arg" != "current" ]; then
        log_error "First argument to --diff must be 'current' (got '$current_arg')."
        usage
    fi

    frozen_dir="${AIDL_ROOT}/v${version}/com/vdiag/hal"
    if [ ! -d "$frozen_dir" ]; then
        log_error "Frozen version directory not found: $frozen_dir"
        exit 1
    fi

    log_info "Comparing current AIDL against frozen v${version} ..."

    local breaking=0
    local changed=0
    local file

    for file in "$CURRENT_DIR"/*.aidl; do
        local basename
        basename="$(basename "$file")"
        local frozen_file="${frozen_dir}/${basename}"

        if [ ! -f "$frozen_file" ]; then
            log_warn "New file in current (not in frozen v${version}): $basename"
            changed=1
            continue
        fi

        if is_breaking_change "$frozen_file" "$file"; then
            breaking=1
        elif ! cmp -s <(normalize_aidl "$frozen_file") <(normalize_aidl "$file"); then
            log_info "Backward-compatible change detected: $basename"
            changed=1
        fi
    done

    # Files removed from current are breaking.
    for frozen_file in "$frozen_dir"/*.aidl; do
        local basename
        basename="$(basename "$frozen_file")"
        if [ ! -f "$CURRENT_DIR/$basename" ]; then
            log_error "Breaking change: file removed from current: $basename"
            breaking=1
        fi
    done

    if [ "$breaking" -eq 1 ]; then
        log_error "AIDL backward-compat check FAILED. Breaking change detected."
        exit 1
    fi

    if [ "$changed" -eq 0 ]; then
        log_info "AIDL is identical to frozen v${version}."
    else
        log_info "AIDL changes are backward-compatible with frozen v${version}."
    fi
    exit 0
}

do_snapshot() {
    local version="$1"
    local frozen_dir="${AIDL_ROOT}/v${version}/com/vdiag/hal"

    if [ -d "$frozen_dir" ]; then
        log_error "Frozen version already exists: $frozen_dir"
        exit 1
    fi

    mkdir -p "$frozen_dir"
    cp -v "$CURRENT_DIR"/*.aidl "$frozen_dir/"
    log_info "Snapshot v${version} created at $frozen_dir"
}

main() {
    [ "$#" -lt 1 ] && usage

    local cmd="$1"
    shift

    case "$cmd" in
        --diff)
            [ "$#" -ne 2 ] && usage
            do_diff "$1" "$2"
            ;;
        --snapshot)
            [ "$#" -ne 1 ] && usage
            do_snapshot "$1"
            ;;
        -h|--help|help)
            usage
            ;;
        *)
            log_error "Unknown command: $cmd"
            usage
            ;;
    esac
}

main "$@"
