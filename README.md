# CardputerZero AppStore

Device-side APPLaunch application for browsing CardputerZero registries and installing apps from online registry metadata.

AppStore installs only Debian `.deb` packages whose registry review status is
`approved`. The backend downloads the `.deb` to the local cache first, verifies
the registry `download.md5`, then installs the local file with the Raspberry Pi
OS/Debian package tools. Installed apps can be launched through their APPLaunch
`.desktop` file and removed with the package manager.

## Runtime

The LVGL UI calls the native backend compiled into the AppStore executable for
registry, install, upgrade, and uninstall operations. No Python runtime or
backend service is required.

Useful backend environment variables:

```bash
M5APPSTORE_STATE_DIR=/root/.local/share/cardputerzero-appstore
M5APPSTORE_APP_ROOT=/usr/share/APPLaunch
```

## Display runtimes

The default CardputerZero deployment is unchanged: it builds and installs the
single `M5CardputerZero-AppStore` framebuffer/evdev binary through the existing
APPLaunch path. Wayland support is an optional dual-runtime deployment; it does
not replace the default framebuffer runtime.

For an explicit dual-runtime deployment, the stable APPLaunch entry becomes a
wrapper that selects the display implementation at runtime:

```text
/usr/share/APPLaunch/bin/M5CardputerZero-AppStore
  wrapper at the existing launcher path
/usr/share/APPLaunch/bin/M5CardputerZero-AppStore-wayland
  SDL/LVGL runtime for labwc/Wayland and X11 sessions
/usr/share/APPLaunch/bin/M5CardputerZero-AppStore-fbdev
  direct framebuffer runtime for the legacy launcher
```

When the optional wrapper is installed, `M5APPSTORE_DISPLAY_BACKEND` selects a
runtime explicitly. If it is unset, the wrapper uses the Wayland/SDL runtime if
`WAYLAND_DISPLAY` or `DISPLAY` is present; otherwise it uses the framebuffer
runtime. This preserves the APPLaunch entry path:

```bash
M5APPSTORE_DISPLAY_BACKEND=wayland M5CardputerZero-AppStore
M5APPSTORE_DISPLAY_BACKEND=fbdev M5CardputerZero-AppStore
```

The wrapper preserves a caller-provided `SDL_VIDEODRIVER`. In a Wayland session
where it is not already set, it selects SDL's Wayland driver and disables
libdecor so labwc owns the window decorations. The SDL runtime identifies its
Wayland window as `cardputerzero-appstore` and creates a fixed 320×170 AppStore
window.

The normal framebuffer-only device build remains the existing command:

```bash
CardputerZero=y scons -j8
```

To make a dual-runtime release, build the two named runtime binaries separately
(use separate build directories or clean between configurations), then assemble
the wrapper and both binaries into one release directory:

```bash
# CardputerZero cross build: primary framebuffer runtime
CardputerZero=y APPSTORE_DISPLAY_BACKEND=fbdev scons -j8

# Native aarch64 Linux build on the Wayland-capable runtime image: optional runtime
APPSTORE_DISPLAY_BACKEND=wayland scons -j8
```

Install the dual-runtime release only when all three files are present:

```bash
install -m 755 M5CardputerZero-AppStore /usr/share/APPLaunch/bin/M5CardputerZero-AppStore
install -m 755 M5CardputerZero-AppStore-fbdev /usr/share/APPLaunch/bin/M5CardputerZero-AppStore-fbdev
install -m 755 M5CardputerZero-AppStore-wayland /usr/share/APPLaunch/bin/M5CardputerZero-AppStore-wayland
```

The framebuffer build deliberately remains the primary cross-compilation
profile. The Wayland build is native because it links against the runtime
image's SDL2/Wayland libraries; requesting it with `CardputerZero=y` fails
early instead of producing an incompatible binary. The stock `setup.ini`
continues to deploy the framebuffer-only artifact; dual-runtime release
assembly and installation are an explicit opt-in.

Default registry:

```text
https://cardputerzero.github.io/generated/registry.json
```

CN region registry:

```text
https://cardputer-zero-repo.oss-cn-shenzhen.aliyuncs.com/packages/cn/registry.json
```

AppStore performs a fresh registry sync when it starts. Registry HTTP requests
send `Cache-Control: no-cache` and a timestamp query parameter so GitHub Pages
or intermediate CDN cache does not hide newly published entries. If a registry
cannot be loaded, the UI shows the load failure; if a previous catalog exists,
AppStore keeps showing the cached app list and marks the registry as cached.

Registry metadata and artwork use libhv asynchronous networking. Artwork is
downloaded in bounded batches of six requests so synchronization remains
responsive without exhausting device sockets. When an HTTP proxy is configured,
AppStore uses libhv's asynchronous thread pool with curl because libhv's native
async client does not implement HTTPS CONNECT proxy tunneling. Debian package
downloads continue to use curl so interrupted downloads can be resumed safely.

## Backend Commands

```bash
./M5CardputerZero-AppStore --summary
./M5CardputerZero-AppStore --registries
./M5CardputerZero-AppStore --regions
./M5CardputerZero-AppStore --set-region CN
./M5CardputerZero-AppStore --add-registry https://example.com/generated/registry.json
./M5CardputerZero-AppStore --sync
./M5CardputerZero-AppStore --plan <app-id>
./M5CardputerZero-AppStore --install <app-id>
./M5CardputerZero-AppStore --upgrade <app-id>
./M5CardputerZero-AppStore --uninstall <app-id>
./M5CardputerZero-AppStore --prepare-package install <app-id>
./M5CardputerZero-AppStore --finalize-package install <app-id> <transaction-id>
```

`<app-id>` accepts either the app UUID or its `share_code` from registry metadata.

The LVGL application uses a three-stage package transaction. The unprivileged
`--prepare-package` stage downloads and verifies the package and records a
pending transaction. The UI then runs the emitted package helper command with
privileges. Finally, `--finalize-package` checks the real package state through
`dpkg-query` before updating AppStore's installed-app records. The original
`--install`, `--reinstall`, `--upgrade`, and `--uninstall` commands remain
available and use the same verification and record-update behavior.

Pending transactions are stored in the AppStore state directory using atomic
file replacement. AppStore reconciles them against the dpkg database on backend
startup and during summary refresh, so an interrupted finalize stage can be
completed after a restart. An interrupted transaction that provably left the
installed package state unchanged is cleared so it cannot block later
operations. A transaction remains pending when its outcome cannot be determined;
its diagnostic is reported again on the next reconciliation.

Package commands produce tab-separated, machine-readable status records.
`PACKAGE_JOB` describes the privileged step, `PACKAGE_RESULT` is the verified
terminal result, `WARNING` reports a non-fatal issue such as desktop-entry
repair, and `ERROR` reports a failed operation. Consumers must determine success
from the process exit code and `PACKAGE_RESULT`, not by searching free-form
package-manager output for words such as `ERROR`.

Desktop-entry repair is best effort. Failure to find or rewrite an APPLaunch
desktop entry is reported as `WARNING` after a successful package operation and
does not change the verified package result.

Multiple registries are supported. On the Registries settings screen, use the
region radio buttons to choose the built-in Default or CN registry. Add another
registry from the same screen or with `--add-registry`; the selected region
registry and all enabled custom registries are synced and merged into one app
list. Apps with the same UUID/share code are de-duplicated, with later registry
metadata taking precedence. On the home screen, press `S` to open Registry
Settings and `C` to open the share code input page. AppStore syncs the registry
automatically when it starts.

Registry entries must provide a Debian package download:

```yaml
download:
  type: deb
  package: lofibox
  url: https://github.com/CardputerZero/packages/raw/main/pool/main/lofibox/lofibox_0.2.0-1~lofibox23_arm64.deb
  md5: 6a9798c1208e6d8bf5a9f28914160483
```

## Share Code Flow

On the AppStore home screen, press `C` to open the share code input page. Type
the code from CardputerZero Hub, then press `Enter` to jump to that app's detail
page. From there, use the normal install/reinstall flow.
