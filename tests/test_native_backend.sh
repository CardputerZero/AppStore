#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
binary="$project_dir/dist/M5CardputerZero-AppStore"
test_root=$(mktemp -d "${TMPDIR:-/tmp}/appstore-native-test.XXXXXX")
trap 'rm -rf -- "$test_root"' EXIT HUP INT TERM

mkdir -p "$test_root/bin" "$test_root/cache" "$test_root/state" "$test_root/root/applications"
deb="$test_root/demo_1.2.0_arm64.deb"
printf 'fake deb payload\n' > "$deb"
md5=$(md5sum "$deb" | cut -d' ' -f1)

cat > "$test_root/registry.json" <<EOF
{"apps":[{"uuid":"app-id","share_code":"demo","title":"Demo","version":"1.2.0","review_status":"approved","download":{"type":"deb","package":"demo-package","url":"https://example.invalid/demo.deb","md5":"$md5"},"app":{"applaunch":{"desktop_entry":"applications/demo.desktop","exec":"/usr/bin/demo-package"}}}]}
EOF

cat > "$test_root/bin/curl" <<'EOF'
#!/bin/sh
set -eu
output=
url=
while [ "$#" -gt 0 ]; do
    case "$1" in
        -o) output=$2; shift 2 ;;
        -A|--connect-timeout|--max-time|-C) shift 2 ;;
        -*) shift ;;
        *) url=$1; shift ;;
    esac
done
if [ "${FAKE_CURL_FAIL:-0}" = 1 ]; then exit 22; fi
case "$url" in
    *ipinfo.io*) printf 'US\n' ;;
    *demo.deb*) cp "$FAKE_DEB" "$output" ;;
    *) cp "$FAKE_REGISTRY" "$output" ;;
esac
EOF
cat > "$test_root/bin/dpkg-query" <<'EOF'
#!/bin/sh
set -eu
if [ "$1" = -L ]; then printf '/usr/bin/demo-package\n'; exit 0; fi
if [ -f "$FAKE_PACKAGE_STATE" ]; then printf 'ii \t1.2.0\n'; exit 0; fi
printf 'dpkg-query: no packages found matching demo-package\n' >&2
exit 1
EOF
cat > "$test_root/bin/dpkg-deb" <<'EOF'
#!/bin/sh
set -eu
case "$3" in
    Package) printf 'demo-package\n' ;;
    Version) printf '1.2.0\n' ;;
    Pre-Depends|Depends) : ;;
    *) exit 1 ;;
esac
EOF
chmod +x "$test_root/bin/curl" "$test_root/bin/dpkg-query" "$test_root/bin/dpkg-deb"

export PATH="$test_root/bin:$PATH"
export FAKE_DEB="$deb" FAKE_REGISTRY="$test_root/registry.json"
export FAKE_PACKAGE_STATE="$test_root/package-installed"
export M5APPSTORE_STATE_DIR="$test_root/state"
export M5APPSTORE_CACHE_DIR="$test_root/cache"
export M5APPSTORE_APP_ROOT="$test_root/root"
export M5APPSTORE_HTTP_TRANSPORT=curl

sync_output=$($binary --sync)
printf '%s\n' "$sync_output" | grep -q '^SYNC[[:space:]]1[[:space:]]0[[:space:]]0[[:space:]]1'
summary_output=$($binary --summary)
printf '%s\n' "$summary_output" | grep -q '^APP[[:space:]]app-id[[:space:]]Demo'

FAKE_CURL_FAIL=1 $binary --sync | grep -q '^SYNC[[:space:]]0[[:space:]]1[[:space:]]0[[:space:]]1'

job=$($binary --prepare-package install app-id)
printf '%s\n' "$job" | grep -q '^PACKAGE_JOB[[:space:]]install'
tx=$(printf '%s\n' "$job" | awk -F '\t' '/^PACKAGE_JOB/{print $6}')
test -n "$tx"
if $binary --finalize-package install app-id "$tx" > "$test_root/early-finalize"; then
    echo 'finalize unexpectedly succeeded before helper completion' >&2
    exit 1
fi
grep -q 'helper completion was not recorded' "$test_root/early-finalize"

sed -i 's/"helper_completed": false/"helper_completed": true/' "$test_root/state/pending-package.json"
touch "$FAKE_PACKAGE_STATE"
$binary --finalize-package install app-id "$tx" | grep -q '^PACKAGE_RESULT[[:space:]]install[[:space:]]app-id[[:space:]]demo-package[[:space:]]1.2.0'
$binary --finalize-package install app-id "$tx" | grep -q '^PACKAGE_RESULT[[:space:]]install[[:space:]]app-id[[:space:]]demo-package[[:space:]]1.2.0'
jq -e '."app-id".files == ["/usr/bin/demo-package"]' "$test_root/state/installed.json" >/dev/null

echo 'native backend integration tests passed'
