#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# One-shot unattended diagnostics: SFX_VR_* lines in this file apply to the
# next launch only. Steam launches keep their fixed arguments, so this is how
# an unattended run is armed; the file is removed before the game starts.
diagnostics="${XDG_DATA_HOME:-$HOME/.local/share}/StarFoxEnhanced/vr-diagnostics.env"
if [[ -f "${diagnostics}" ]]; then
    while IFS='=' read -r key value || [[ -n "${key}" ]]; do
        if [[ "${key}" =~ ^SFX_VR_[A-Z0-9_]+$ ]]; then export "${key}=${value}"; fi
    done < "${diagnostics}"
    rm -f -- "${diagnostics}"
fi
exec "${script_dir}/starfox_steamframe" "$@"
