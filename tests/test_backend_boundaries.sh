#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

if grep -R -n -E 'appstore::native_|#include[[:space:]]+"appstore_native\.hpp"' "$root/main/ui"; then
    echo "UI must not call or include the native backend directly" >&2
    exit 1
fi

if grep -R -n -- '"--package-helper"' "$root/main/ui"; then
    echo "UI must submit typed privileged package requests, not construct helper commands" >&2
    exit 1
fi

if grep -n 'backend_capture' "$root/main/ui/appstore.cpp"; then
    echo "AppStore UI must route backend work through task/package services" >&2
    exit 1
fi

echo "backend boundary tests passed"
