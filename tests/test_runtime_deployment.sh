#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
setup_ini="$project_dir/setup.ini"
after_cmd=$(sed -n 's/^after_cmd = //p' "$setup_ini")

case "$after_cmd" in
    *M5CardputerZero-AppStore-wayland*|*M5CardputerZero-AppStore-fbdev*)
        echo "default setup.ini deployment must remain framebuffer-only" >&2
        exit 1
        ;;
esac

printf '%s\n' "$after_cmd" |
    grep -F 'install -m 755 /home/pi/dist/M5CardputerZero-AppStore /usr/share/APPLaunch/bin/M5CardputerZero-AppStore' \
    >/dev/null

echo "default framebuffer deployment test passed"
