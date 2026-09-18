#!/bin/bash
# Prove that loading, using and unloading the plugin cannot take Hyprland down.
#
# The plugin runs inside the compositor, so a mistake on unload ends the user's
# whole session. This runs the plugin in a throwaway nested Hyprland — a window
# inside the current session — and puts it through what users and development
# both do: load a copy, change settings the way the bar menu does, unload it,
# reload the config, and repeat with a fresh copy from a new path. The session
# you run it from is never touched.
#
#   scripts/unload-check.sh [path/to/hyprknit.so]
#
# Needs a running Wayland session. Exits non-zero if the nested compositor dies.

set -uo pipefail

plugin=$(realpath "${1:-build/hyprknit.so}")
[[ -f $plugin ]] || { echo "no plugin at $plugin; build it first" >&2; exit 2; }
[[ -n ${WAYLAND_DISPLAY:-} ]] || { echo "needs a running Wayland session" >&2; exit 2; }

work=$(mktemp -d)
trap 'kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; rm -rf "$work"' EXIT
printf -- '-- throwaway nested session for hyprknit unload checks\nhl.config({ general = { gaps_out = 20 } })\n' >"$work/hyprland.lua"

# Each reload in a development session loads the plugin from a new path, and a
# library that cannot be unmapped lingers between them. Three copies reproduce
# that history.
copies=()
for n in 1 2 3; do
	cp "$plugin" "$work/hyprknit-$n.so"
	copies+=("$work/hyprknit-$n.so")
done

before=$(hyprctl instances -j | jq -r '.[].instance')
Hyprland --config "$work/hyprland.lua" >"$work/hyprland.log" 2>&1 &
pid=$!

sig=""
for _ in $(seq 1 100); do
	sig=$(hyprctl instances -j | jq -r '.[].instance' | grep -vxF "$before" | head -1)
	[[ -n $sig ]] && hyprctl -i "$sig" version >/dev/null 2>&1 && break
	sleep 0.1
done
[[ -n $sig ]] || { echo "nested Hyprland did not start" >&2; exit 2; }

h() { HYPRLAND_INSTANCE_SIGNATURE=$sig hyprctl "$@"; }
alive() { kill -0 "$pid" 2>/dev/null && h version >/dev/null 2>&1; }

failed=0
step=0
for copy in "${copies[@]}"; do
	step=$((step + 1))
	h plugin load "$copy" >/dev/null
	h eval 'hl.config({ plugin = { hyprknit = { pattern = "zigzag" } } })' >/dev/null
	h hyprknit-reload >/dev/null 2>&1
	h plugin unload "$copy" >/dev/null 2>&1
	sleep 1
	h reload >/dev/null 2>&1
	sleep 1
	if alive; then
		echo "copy $step: load, use, unload, reload config: survived"
	else
		echo "copy $step: load, use, unload, reload config: HYPRLAND CRASHED"
		failed=1
		break
	fi
done

if ((!failed)); then
	h dispatch exit >/dev/null 2>&1
	echo "ok: unloading hyprknit is safe"
fi
exit "$failed"
