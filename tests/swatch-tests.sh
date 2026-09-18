#!/bin/bash
set -euo pipefail

binary=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if "$binary" "$work/invalid" --app test --size '-1x10' >/dev/null 2>&1; then
	echo "negative dimensions were accepted" >&2
	exit 1
fi

mkdir -p "$work/out/nested"
"$binary" "$work/out/nested" --app '../../escaped' --size 64x64 --width 4 >/dev/null
[[ -f $work/out/nested/..-..-escaped.png ]]
[[ ! -f $work/out/escaped.png ]]
