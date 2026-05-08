# Cardputer Zero AppStore

Device-side APPLaunch application for browsing Cardputer Zero registries and installing apps from online registry metadata.

## Runtime

The LVGL UI calls `appstore.py` for registry, install, uninstall, and run operations. By default the script lives next to the installed binary, but the UI also honors:

```bash
M5APPSTORE_SCRIPT=/path/to/appstore.py
```

Useful backend environment variables:

```bash
M5APPSTORE_STATE_DIR=/root/.local/share/cardputerzero-appstore
M5APPSTORE_APP_ROOT=/usr/share/APPLaunch
```

Default registry:

```text
https://cardputerzero.github.io/generated/registry-index.json
```

## Backend Commands

```bash
python3 appstore.py --summary
python3 appstore.py --registries
python3 appstore.py --add-registry https://example.com/generated/registry-index.json
python3 appstore.py --sync
python3 appstore.py --plan <app-id>
python3 appstore.py --install <app-id>
python3 appstore.py --uninstall <app-id>
python3 appstore.py --run <app-id>
```

`<app-id>` accepts either the app UUID or its `share_code` from registry metadata.

## Share Code Flow

On the AppStore home screen, press `S` to open the share code input page. Type
the code from CardputerZero Hub, then press `Enter` to jump to that app's detail
page. From there, use the normal install/reinstall flow.
