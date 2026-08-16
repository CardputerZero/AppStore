#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "package repair helper tests skipped: root is required"
    exit 0
fi

binary=${1:?usage: test_package_repair_helper.sh /path/to/AppStore-binary}
binary=$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary")
test_root=$(mktemp -d "${TMPDIR:-/tmp}/appstore-repair-test.XXXXXX")
trap 'rm -rf -- "$test_root"' EXIT HUP INT TERM

mkdir -p "$test_root/bin" "$test_root/state" "$test_root/cache" \
    "$test_root/app-root/applications" "$test_root/dpkg-info" "$test_root/backups"

cat > "$test_root/bin/dpkg-query" <<'EOF'
#!/bin/sh
set -eu
if [ "$1" = -L ]; then
    printf '/usr/bin/demo-package\n'
    exit 0
fi
case "$(cat "$FAKE_PACKAGE_STATE")" in
    installed) printf 'ii \t1.2.0\n' ;;
    previous) printf 'ii \t1.1.0\n' ;;
    half) printf 'iF \t1.2.0\n' ;;
    absent)
        printf 'dpkg-query: no packages found matching demo-package\n' >&2
        exit 1
        ;;
    *) exit 2 ;;
esac
EOF

cat > "$test_root/bin/dpkg" <<'EOF'
#!/bin/sh
set -eu
printf '%s\n' "$*" >> "$FAKE_DPKG_LOG"
case "$1" in
    --configure)
        case "$FAKE_REPAIR_MODE" in
            configure) printf 'installed\n' > "$FAKE_PACKAGE_STATE"; exit 0 ;;
            timeout) sleep 10; printf 'installed\n' > "$FAKE_PACKAGE_STATE"; exit 0 ;;
            *) exit 1 ;;
        esac
        ;;
    --force-remove-reinstreq)
        case "$FAKE_REPAIR_MODE" in
            rollback|timeout) printf 'absent\n' > "$FAKE_PACKAGE_STATE"; exit 0 ;;
            *) exit 1 ;;
        esac
        ;;
    --force-all)
        [ ! -e "$M5APPSTORE_DPKG_INFO_DIR/demo-package.postinst" ] || exit 9
        printf 'absent\n' > "$FAKE_PACKAGE_STATE"
        exit 0
        ;;
    --install)
        [ "$FAKE_REPAIR_MODE" = restore ] || exit 1
        printf 'previous\n' > "$FAKE_PACKAGE_STATE"
        exit 0
        ;;
    --triggers-only) exit 0 ;;
esac
exit 2
EOF

cat > "$test_root/bin/dpkg-deb" <<'EOF'
#!/bin/sh
case "$3" in
    Package) printf 'demo-package\n' ;;
    Version) printf '1.1.0\n' ;;
    *) exit 1 ;;
esac
EOF
chmod +x "$test_root/bin/dpkg-query" "$test_root/bin/dpkg" "$test_root/bin/dpkg-deb"

export PATH="$test_root/bin:$PATH"
export M5APPSTORE_STATE_DIR="$test_root/state"
export M5APPSTORE_CACHE_DIR="$test_root/cache"
export M5APPSTORE_APP_ROOT="$test_root/app-root"
export M5APPSTORE_DPKG_INFO_DIR="$test_root/dpkg-info"
export M5APPSTORE_REPAIR_BACKUP_DIR="$test_root/backups"
export M5APPSTORE_REPAIR_CONFIGURE_TIMEOUT=1
export M5APPSTORE_REPAIR_REMOVE_TIMEOUT=1
export M5APPSTORE_REPAIR_FORCE_TIMEOUT=1
export M5APPSTORE_REPAIR_TRIGGER_TIMEOUT=1
export FAKE_PACKAGE_STATE="$test_root/package-state"
export FAKE_DPKG_LOG="$test_root/dpkg.log"

make_pending()
{
    case_tx=$1
    rm -f "$M5APPSTORE_STATE_DIR/pending-package.json" "$M5APPSTORE_STATE_DIR/installed.json"
    rm -rf "$M5APPSTORE_DPKG_INFO_DIR" "$M5APPSTORE_REPAIR_BACKUP_DIR"
    mkdir -p "$M5APPSTORE_DPKG_INFO_DIR" "$M5APPSTORE_REPAIR_BACKUP_DIR"
    : > "$FAKE_DPKG_LOG"
    printf 'half\n' > "$FAKE_PACKAGE_STATE"
    cat > "$M5APPSTORE_STATE_DIR/pending-package.json" <<EOF
{
  "schema_version": 2,
  "transaction_id": "$case_tx",
  "action": "install",
  "app_id": "app-id",
  "package": "demo-package",
  "previously_installed": false,
  "previous_version": "",
  "expected_package_version": "1.2.0",
  "helper_completed": false,
  "repair_requested": true,
  "app_snapshot": {
    "uuid": "app-id",
    "share_code": "demo",
    "title": "Demo",
    "version": "1.2.0",
    "download": {"package": "demo-package"}
  }
}
EOF
}

run_helper()
{
    "$binary" --package-helper repair \
        --package-value demo-package \
        --package-transaction "$1" \
        --package-pending-path "$M5APPSTORE_STATE_DIR/pending-package.json"
}

make_pending configure-success
export FAKE_REPAIR_MODE=configure
run_helper configure-success | grep -q '^PACKAGE_HELPER[[:space:]]repair[[:space:]]demo-package'
"$binary" --finalize-package install app-id configure-success |
    grep -q '^PACKAGE_RESULT[[:space:]]install[[:space:]]app-id[[:space:]]demo-package[[:space:]]1.2.0'
test ! -e "$M5APPSTORE_STATE_DIR/pending-package.json"

make_pending restore-previous
export FAKE_REPAIR_MODE=restore
rollback_deb="$M5APPSTORE_CACHE_DIR/demo-package_1.1.0_arm64.deb"
printf 'previous package\n' > "$rollback_deb"
python3 - "$M5APPSTORE_STATE_DIR/pending-package.json" "$rollback_deb" <<'PY'
import json, sys
path, rollback_deb = sys.argv[1:]
with open(path, encoding="utf-8") as source:
    pending = json.load(source)
pending["action"] = "upgrade"
pending["previously_installed"] = True
pending["previous_version"] = "1.1.0"
pending["rollback_deb_path"] = rollback_deb
with open(path, "w", encoding="utf-8") as output:
    json.dump(pending, output)
PY
run_helper restore-previous | grep -q '^PACKAGE_HELPER[[:space:]]repair[[:space:]]demo-package'
"$binary" --finalize-package upgrade app-id restore-previous |
    grep -q '^PACKAGE_RESULT[[:space:]]repair-restored[[:space:]]app-id[[:space:]]demo-package[[:space:]]1.1.0'
test ! -e "$M5APPSTORE_STATE_DIR/pending-package.json"

make_pending resume-after-restore
export FAKE_REPAIR_MODE=force
printf 'previous\n' > "$FAKE_PACKAGE_STATE"
python3 - "$M5APPSTORE_STATE_DIR/pending-package.json" <<'PY'
import json, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as source:
    pending = json.load(source)
pending["action"] = "upgrade"
pending["previously_installed"] = True
pending["previous_version"] = "1.1.0"
with open(path, "w", encoding="utf-8") as output:
    json.dump(pending, output)
PY
run_helper resume-after-restore | grep -q '^PACKAGE_HELPER[[:space:]]repair[[:space:]]demo-package'
test "$(cat "$FAKE_DPKG_LOG")" = "--triggers-only --pending"
"$binary" --finalize-package upgrade app-id resume-after-restore |
    grep -q '^PACKAGE_RESULT[[:space:]]repair-restored[[:space:]]app-id[[:space:]]demo-package[[:space:]]1.1.0'
test ! -e "$M5APPSTORE_STATE_DIR/pending-package.json"

make_pending rollback-success
export FAKE_REPAIR_MODE=rollback
run_helper rollback-success | grep -q '^PACKAGE_HELPER[[:space:]]repair[[:space:]]demo-package'
"$binary" --finalize-package install app-id rollback-success |
    grep -q '^PACKAGE_RESULT[[:space:]]repair-rollback[[:space:]]app-id[[:space:]]demo-package'
test ! -e "$M5APPSTORE_STATE_DIR/pending-package.json"

make_pending timeout-rollback
export FAKE_REPAIR_MODE=timeout
started=$(date +%s)
run_helper timeout-rollback | grep -q '^PACKAGE_HELPER[[:space:]]repair[[:space:]]demo-package'
elapsed=$(($(date +%s) - started))
[ "$elapsed" -lt 8 ]
"$binary" --finalize-package install app-id timeout-rollback |
    grep -q '^PACKAGE_RESULT[[:space:]]repair-rollback[[:space:]]app-id[[:space:]]demo-package'

make_pending force-remove
export FAKE_REPAIR_MODE=force
printf '#!/bin/sh\nexit 1\n' > "$M5APPSTORE_DPKG_INFO_DIR/demo-package.postinst"
chmod 755 "$M5APPSTORE_DPKG_INFO_DIR/demo-package.postinst"
run_helper force-remove | grep -q '^PACKAGE_HELPER[[:space:]]repair[[:space:]]demo-package'
test ! -e "$M5APPSTORE_DPKG_INFO_DIR/demo-package.postinst"
test -z "$(find "$M5APPSTORE_REPAIR_BACKUP_DIR" -mindepth 1 -print -quit)"
"$binary" --finalize-package install app-id force-remove |
    grep -q '^PACKAGE_RESULT[[:space:]]repair-rollback[[:space:]]app-id[[:space:]]demo-package'

make_pending resume-after-reboot
export FAKE_REPAIR_MODE=force
resume_dir="$M5APPSTORE_REPAIR_BACKUP_DIR/$(printf '%s' resume-after-reboot | sha1sum | cut -d' ' -f1)"
mkdir -p "$resume_dir"
printf '#!/bin/sh\nexit 1\n' > "$resume_dir/demo-package.postinst"
python3 - "$M5APPSTORE_STATE_DIR/pending-package.json" "$resume_dir" <<'PY'
import json, sys
path, backup = sys.argv[1:]
with open(path, encoding="utf-8") as source:
    pending = json.load(source)
pending["repair_scripts_quarantined"] = True
pending["repair_script_backup"] = backup
with open(path, "w", encoding="utf-8") as output:
    json.dump(pending, output)
PY
run_helper resume-after-reboot | grep -q '^PACKAGE_HELPER[[:space:]]repair[[:space:]]demo-package'
"$binary" --finalize-package install app-id resume-after-reboot |
    grep -q '^PACKAGE_RESULT[[:space:]]repair-rollback[[:space:]]app-id[[:space:]]demo-package'
test ! -e "$resume_dir"

echo "package repair helper tests passed"
