# Cardputer Zero AppStore

Device-side APPLaunch application for browsing Cardputer Zero registries and installing apps from online registry metadata.

AppStore installs only Debian `.deb` packages. The backend downloads the `.deb`
to the local cache first, verifies the registry `download.md5`, then installs
the local file with the Raspberry Pi OS/Debian package tools. Installed apps can
be launched through their APPLaunch `.desktop` file and removed with the package
manager.

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

Registry entries must provide a Debian package download:

```yaml
download:
  type: deb
  package: lofibox
  url: https://github.com/CardputerZero/packages/raw/main/pool/main/lofibox/lofibox_0.2.0-1~lofibox23_arm64.deb
  md5: 6a9798c1208e6d8bf5a9f28914160483
```

## Share Code Flow

On the AppStore home screen, press `S` to open the share code input page. Type
the code from CardputerZero Hub, then press `Enter` to jump to that app's detail
page. From there, use the normal install/reinstall flow.
