#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
launcher="$project_dir/bin/M5CardputerZero-AppStore"
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/appstore-launcher.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

make_runtime() {
    path=$1
    printf '%s\n' '#!/bin/sh' \
        'printf "%s|%s|%s|%s\\n" "$0" "${SDL_VIDEODRIVER:-}" "${SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR:-}" "${SDL_VIDEO_WAYLAND_PREFER_LIBDECOR:-}"' \
        > "$path"
    chmod +x "$path"
}

wayland="$test_dir/M5CardputerZero-AppStore-wayland"
fbdev="$test_dir/M5CardputerZero-AppStore-fbdev"
legacy="$test_dir/M5CardputerZero-AppStore.bin"
test_launcher="$test_dir/M5CardputerZero-AppStore"
cp "$launcher" "$test_launcher"
chmod +x "$test_launcher"
make_runtime "$wayland"
make_runtime "$fbdev"

expected="$wayland|wayland|0|0"
actual=$(env -i PATH="$PATH" WAYLAND_DISPLAY=wayland-0 "$test_launcher")
[ "$actual" = "$expected" ]

expected="$fbdev|||"
actual=$(env -i PATH="$PATH" "$test_launcher")
[ "$actual" = "$expected" ]

expected="$wayland|||"
actual=$(env -i PATH="$PATH" DISPLAY=:0 "$test_launcher")
[ "$actual" = "$expected" ]

expected="$fbdev|||"
actual=$(env -i PATH="$PATH" WAYLAND_DISPLAY=wayland-0 M5APPSTORE_DISPLAY_BACKEND=fbdev "$test_launcher")
[ "$actual" = "$expected" ]

expected="$wayland|x11|0|0"
actual=$(env -i PATH="$PATH" WAYLAND_DISPLAY=wayland-0 SDL_VIDEODRIVER=x11 "$test_launcher")
[ "$actual" = "$expected" ]

rm "$fbdev"
make_runtime "$legacy"
expected="$legacy|||"
actual=$(env -i PATH="$PATH" "$test_launcher")
[ "$actual" = "$expected" ]

if env -i PATH="$PATH" M5APPSTORE_DISPLAY_BACKEND=unknown "$test_launcher" >"$test_dir/error" 2>&1; then
    echo "launcher accepted an invalid display backend" >&2
    exit 1
fi
grep -F 'unsupported M5APPSTORE_DISPLAY_BACKEND=unknown' "$test_dir/error" >/dev/null

echo "runtime launcher tests passed"
