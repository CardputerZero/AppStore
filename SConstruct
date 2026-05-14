from pathlib import Path
import os
import platform
import shutil

arch = platform.machine()
homebrew_toolchain = "/opt/homebrew/bin"
has_homebrew_aarch64 = os.path.exists(os.path.join(homebrew_toolchain, "aarch64-linux-gnu-gcc"))

version = "v0.0.3"
static_lib = "static_lib"
update = False
local_path = Path(os.getcwd()).resolve()
workspace_path = local_path.parents[1] if len(local_path.parents) > 1 else local_path.parent

freetype_config_lines = [
    "CONFIG_V9_5_LV_USE_FREETYPE=y",
    "CONFIG_V9_5_LV_FREETYPE_CACHE_FT_GLYPH_CNT=512",
]


def resolve_sdk_path():
    candidates = []
    sdk_override = os.environ.get("SDK_PATH")
    if sdk_override:
        candidates.append(Path(sdk_override).expanduser())
    candidates.extend(
        [
            local_path.parent.parent / "SDK",
            local_path.parent / "SDK",
            workspace_path / "Launcher" / "SDK",
            workspace_path
            / "CardputerZero-AppBuilder"
            / "emulator"
            / "vendor"
            / "M5CardputerZero-UserDemo"
            / "SDK",
        ]
    )
    checked = []
    for candidate in candidates:
        sdk = candidate.resolve()
        project = sdk / "tools" / "scons" / "project.py"
        checked.append(str(project))
        if project.exists():
            return sdk
    raise RuntimeError(
        "Unable to find SDK tools/scons/project.py. Set SDK_PATH=/path/to/SDK or install one of:\n"
        + "\n".join(checked)
    )


def write_config_tmp(lines):
    path = local_path / "build" / "config" / "config_tmp.mk"
    path.parent.mkdir(parents=True, exist_ok=True)
    content = "\n".join(lines) + "\n"
    changed = not path.exists() or path.read_text() != content
    if changed:
        path.write_text(content)
    if changed or not generated_config_matches(lines):
        invalidate_generated_config()


def generated_config_matches(lines):
    path = local_path / "build" / "config" / "global_config.mk"
    if not path.exists():
        return True
    content = path.read_text()
    return all(line in content for line in lines)


def invalidate_generated_config():
    for filename in ("global_config.mk", "global_config.h", "lvgl_config.h"):
        path = local_path / "build" / "config" / filename
        if path.exists():
            path.unlink()


def static_lib_is_current(path):
    try:
        return (path / "version").read_text().strip() == version
    except Exception:
        return False


def remove_path(path):
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    elif path.exists() or path.is_symlink():
        path.unlink()


def download_file(url, dest):
    if dest.exists():
        return
    import urllib.request

    dest.parent.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve(url, dest)


def extract_tar_gz(archive, dest):
    import tarfile

    tmp = dest.with_name(dest.name + ".tmp")
    remove_path(tmp)
    tmp.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive, "r:gz") as tar:
        try:
            tar.extractall(path=tmp, filter="fully_trusted")
        except TypeError:
            tar.extractall(path=tmp)
    remove_path(dest)
    tmp.rename(dest)


def prepare_static_lib(down_url):
    archive = Path(os.environ["GIT_REPO_PATH"]) / "static_lib_{}.tar.gz".format(version)
    extracted = Path(str(archive)[:-7])
    download_file(down_url, archive)
    if not static_lib_is_current(extracted):
        extract_tar_gz(archive, extracted)
    return extracted


if "CardputerZero" in os.environ:
    sysroot_dir = local_path / static_lib
    os.environ["CARDPUTERZERO_STATIC_LIB_SYSROOT"] = str(sysroot_dir)
    generic_include = sysroot_dir / "usr" / "include"
    multiarch_include = sysroot_dir / "usr" / "include" / "aarch64-linux-gnu"
    freetype_include = sysroot_dir / "usr" / "include" / "freetype2"
    libpng_include = sysroot_dir / "usr" / "include" / "libpng16"
    multiarch_lib = sysroot_dir / "usr" / "lib" / "aarch64-linux-gnu"
    toolchain_flags = [
        f"-B{multiarch_lib}",
        f"-idirafter{generic_include}",
        f"-idirafter{multiarch_include}",
        f"-I{freetype_include}",
        f"-I{libpng_include}",
        f"-L{multiarch_lib}",
        f"-Wl,-rpath-link,{multiarch_lib}",
    ]
    config_lines = [
        "CONFIG_V9_5_LV_USE_LINUX_FBDEV=y",
        "CONFIG_V9_5_LV_DRAW_SW_ASM_NEON=y",
        "CONFIG_V9_5_LV_USE_DRAW_SW_ASM=1",
        "CONFIG_V9_5_LV_USE_EVDEV=y",
    ]
    config_lines.extend(freetype_config_lines)
    if has_homebrew_aarch64:
        config_lines.append(f'CONFIG_TOOLCHAIN_PATH="{homebrew_toolchain}"')
    config_lines.extend(
        [
            'CONFIG_TOOLCHAIN_PREFIX="aarch64-linux-gnu-"',
            f'CONFIG_TOOLCHAIN_FLAGS="{" ".join(toolchain_flags)}"',
        ]
    )
    write_config_tmp(config_lines)
elif arch != "aarch64":
    write_config_tmp(
        [
            "CONFIG_V9_5_LV_USE_SDL=y",
        ] + freetype_config_lines
    )
else:
    write_config_tmp(
        [
            "CONFIG_V9_5_LV_USE_LINUX_FBDEV=y",
            "CONFIG_V9_5_LV_DRAW_SW_ASM_NEON=y",
            "CONFIG_V9_5_LV_USE_DRAW_SW_ASM=1",
            "CONFIG_V9_5_LV_USE_EVDEV=y",
        ] + freetype_config_lines
    )

sdk_path = resolve_sdk_path()
os.environ["SDK_PATH"] = str(sdk_path)
os.environ.setdefault("EXT_COMPONENTS_PATH", str(sdk_path.parent / "ext_components"))

env = SConscript(
    str(sdk_path / "tools" / "scons" / "project.py"),
    variant_dir=os.getcwd(),
    duplicate=0,
)

if not os.path.exists(static_lib):
    update = True
else:
    try:
        with open(str(Path(static_lib) / "version"), "r") as f:
            if version != f.read().strip():
                update = True
    except Exception:
        update = True

if update and "CardputerZero" in os.environ:
    down_url = "https://github.com/dianjixz/M5CardputerZero-UserDemo/releases/download/{}/sdk_bsp.tar.gz".format(
        version
    )
    down_path = prepare_static_lib(down_url)
    remove_path(Path(static_lib))
    shutil.move(down_path, static_lib)
