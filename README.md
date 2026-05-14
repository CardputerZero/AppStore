# Cardputer Zero AppStore

Device-side APPLaunch application for browsing Cardputer Zero registries and installing apps from online registry metadata.

AppStore installs only Debian `.deb` packages. The backend downloads the `.deb`
to the local cache first, verifies the registry `download.md5`, then installs
the local file with the Raspberry Pi OS/Debian package tools. Installed apps can
be launched through their APPLaunch `.desktop` file and removed with the package
manager.

## Runtime

The LVGL UI calls `appstore.py` for registry, install, upgrade, and uninstall operations. By default the script lives next to the installed binary, but the UI also honors:

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
https://cardputerzero.github.io/generated/registry.json
```

AppStore performs a fresh registry sync when it starts. Registry HTTP requests
send `Cache-Control: no-cache` and a timestamp query parameter so GitHub Pages
or intermediate CDN cache does not hide newly published entries. If a registry
cannot be loaded, the UI shows the load failure; if a previous catalog exists,
AppStore keeps showing the cached app list and marks the registry as cached.

## Backend Commands

```bash
python3 appstore.py --summary
python3 appstore.py --registries
python3 appstore.py --add-registry https://example.com/generated/registry.json
python3 appstore.py --sync
python3 appstore.py --plan <app-id>
python3 appstore.py --install <app-id>
python3 appstore.py --upgrade <app-id>
python3 appstore.py --uninstall <app-id>
```

`<app-id>` accepts either the app UUID or its `share_code` from registry metadata.

Multiple registries are supported. Add another registry from the Registries
screen or with `--add-registry`; enabled registries are synced and merged into
one app list. Apps with the same UUID/share code are de-duplicated, with later
registry metadata taking precedence. On the home screen, `Ctrl+Tab` cycles the
display order through Featured, Name, Category, Installed, and Registry.

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
