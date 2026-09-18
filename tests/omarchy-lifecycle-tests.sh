#!/bin/bash
set -euo pipefail

project=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

export HOME="$work/home"
export XDG_DATA_HOME="$work/data"
export XDG_CACHE_HOME="$work/cache"
state="$XDG_DATA_HOME/hyprknit"
autostart="$HOME/.config/hypr/autostart.lua"
mkdir -p "$state" "$(dirname "$autostart")" "$XDG_CACHE_HOME/hyprknit/build"
install -m 755 "$project/omarchy/hyprknit-plugin" "$state/hyprknit-plugin"
printf '%s\n' "$project" >"$state/source"

"$project/omarchy/hyprknit-plugin" repair >/dev/null
grep -qF -- '-- hyprknit: load the knitting at login' "$autostart"

printf '%s\n' "$work/removed-plugin" >"$state/source"
"$state/hyprknit-plugin" load >/dev/null
[[ ! -e $state ]]
[[ ! -e $XDG_CACHE_HOME/hyprknit ]]
! grep -qF -- '-- hyprknit' "$autostart"
