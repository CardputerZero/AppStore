#!/usr/bin/env python3
"""Cardputer Zero AppStore registry backend.

The LVGL UI consumes this script through a small TSV protocol. The backend keeps
state on device, syncs JSON registries, caches icons, and installs APPLaunch
packages into /usr/share/APPLaunch.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path
from typing import Any, Optional


DEFAULT_INDEX_URL = "https://cardputerzero.github.io/generated/registry-index.json"
DEFAULT_REGISTRY_NAME = "CardputerZero Hub"
USER_AGENT = "CardputerZero-AppStore/0.1"


def state_dir() -> Path:
    return Path(os.environ.get("M5APPSTORE_STATE_DIR", "~/.local/share/cardputerzero-appstore")).expanduser()


def app_root() -> Path:
    return Path(os.environ.get("M5APPSTORE_APP_ROOT", "/usr/share/APPLaunch"))


def cache_dir() -> Path:
    return state_dir() / "cache"


def config_path() -> Path:
    return state_dir() / "registries.json"


def installed_path() -> Path:
    return state_dir() / "installed.json"


def now_text() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")


def tsv_escape(value: Any) -> str:
    text = "" if value is None else str(value)
    return text.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def emit(*fields: Any) -> None:
    print("\t".join(tsv_escape(field) for field in fields))


def short_hash(value: str) -> str:
    return hashlib.sha1(value.encode("utf-8")).hexdigest()[:16]


def ensure_dirs() -> None:
    for path in (state_dir(), cache_dir(), cache_dir() / "icons", cache_dir() / "downloads"):
        path.mkdir(parents=True, exist_ok=True)


def read_json(path: Path, default: Any) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return default


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    tmp.replace(path)


def normalize_registry_url(value: str) -> str:
    value = value.strip()
    if not value:
        return DEFAULT_INDEX_URL
    if value.startswith(("http://", "https://", "file://")):
        parsed = urllib.parse.urlparse(value)
        if parsed.path.endswith(".json"):
            return value
        return value.rstrip("/") + "/generated/registry-index.json"
    path = Path(value).expanduser()
    if path.suffix == ".json":
        return path.resolve().as_uri()
    return (path / "generated" / "registry-index.json").resolve().as_uri()


def load_config() -> dict[str, Any]:
    ensure_dirs()
    data = read_json(config_path(), {})
    registries = data.get("registries")
    if not isinstance(registries, list) or not registries:
        registries = [{"name": DEFAULT_REGISTRY_NAME, "url": DEFAULT_INDEX_URL, "enabled": True}]
    normalized = []
    seen = set()
    for item in registries:
        if isinstance(item, str):
            item = {"url": item}
        if not isinstance(item, dict):
            continue
        url = normalize_registry_url(str(item.get("url", "")))
        if url in seen:
            continue
        seen.add(url)
        normalized.append({
            "name": item.get("name") or registry_name_from_url(url),
            "url": url,
            "enabled": bool(item.get("enabled", True)),
        })
    data["registries"] = normalized or [{"name": DEFAULT_REGISTRY_NAME, "url": DEFAULT_INDEX_URL, "enabled": True}]
    return data


def save_config(data: dict[str, Any]) -> None:
    write_json(config_path(), data)


def registry_name_from_url(url: str) -> str:
    parsed = urllib.parse.urlparse(url)
    if parsed.netloc:
        return parsed.netloc
    return Path(parsed.path).stem or "Local Registry"


def request_json(url: str) -> Any:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=15) as response:
        return json.loads(response.read().decode("utf-8"))


def download_file(url: str, dest: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=30) as response, dest.open("wb") as handle:
        shutil.copyfileobj(response, handle)


def registry_site_root(index_url: str) -> str:
    parsed = urllib.parse.urlparse(index_url)
    if parsed.scheme in {"http", "https"} and "/generated/" in parsed.path:
        prefix = parsed.path.split("/generated/", 1)[0].rstrip("/") + "/"
        return urllib.parse.urlunparse((parsed.scheme, parsed.netloc, prefix, "", "", ""))
    return urllib.parse.urljoin(index_url, "./")


def full_registry_url(index_url: str) -> str:
    if index_url.endswith("/registry-index.json"):
        return index_url[:-len("registry-index.json")] + "registry.json"
    if index_url.endswith("/registry.json"):
        return index_url
    return urllib.parse.urljoin(index_url, "registry.json")


def cache_file_for(url: str) -> Path:
    return cache_dir() / f"registry-{short_hash(url)}.json"


def cache_icon(index_url: str, icon_ref: str) -> str:
    if not icon_ref:
        return ""
    if icon_ref.startswith("file://"):
        return urllib.parse.urlparse(icon_ref).path
    if icon_ref.startswith(("http://", "https://")):
        icon_url = icon_ref
    else:
        icon_url = urllib.parse.urljoin(registry_site_root(index_url), icon_ref)
    suffix = Path(urllib.parse.urlparse(icon_url).path).suffix or ".png"
    dest = cache_dir() / "icons" / f"{short_hash(icon_url)}{suffix}"
    if dest.exists() and dest.stat().st_size > 0:
        return str(dest)
    try:
        download_file(icon_url, dest)
        return str(dest)
    except Exception:
        return ""


def sync_one_registry(source: dict[str, Any]) -> dict[str, Any]:
    url = source["url"]
    record: dict[str, Any] = {
        "name": source.get("name") or registry_name_from_url(url),
        "url": url,
        "status": "ok",
        "synced_at": now_text(),
        "index": {},
        "full": {},
        "icons": {},
    }
    try:
        index = request_json(url)
        record["index"] = index
        try:
            record["full"] = request_json(full_registry_url(url))
        except Exception as exc:
            record["full_error"] = str(exc)
            record["full"] = {}
        for app in index.get("apps", []):
            if isinstance(app, dict):
                icon = app.get("icon") or app.get("assets", {}).get("icon")
                local_icon = cache_icon(url, str(icon or ""))
                if local_icon:
                    app_id = app.get("uuid") or app.get("share_code") or app.get("title")
                    record["icons"][str(app_id)] = local_icon
        write_json(cache_file_for(url), record)
    except Exception as exc:
        record["status"] = "error"
        record["error"] = str(exc)
        cached = read_json(cache_file_for(url), {})
        if isinstance(cached, dict) and cached.get("index"):
            cached["status"] = "cached"
            cached["error"] = str(exc)
            return cached
    return record


def sync_all() -> list[dict[str, Any]]:
    config = load_config()
    records = []
    for source in config["registries"]:
        if source.get("enabled", True):
            records.append(sync_one_registry(source))
    return records


def load_registry_records(sync_if_empty: bool = False) -> list[dict[str, Any]]:
    config = load_config()
    records = []
    for source in config["registries"]:
        if not source.get("enabled", True):
            continue
        cached = read_json(cache_file_for(source["url"]), {})
        if isinstance(cached, dict) and cached.get("index"):
            records.append(cached)
    if not records and sync_if_empty:
        records = sync_all()
    return records


def list_value(value: Any) -> list[str]:
    if isinstance(value, list):
        return [str(item) for item in value if item is not None]
    if isinstance(value, str) and value:
        return [value]
    return []


def app_key(app: dict[str, Any]) -> str:
    return str(app.get("uuid") or app.get("share_code") or app.get("id") or app.get("title") or "")


def author_text(app: dict[str, Any]) -> str:
    author = app.get("author")
    if isinstance(author, dict):
        return str(author.get("display_name") or author.get("github") or author.get("name") or "")
    return str(author or "")


def source_repo(app: dict[str, Any]) -> str:
    source = app.get("source")
    if isinstance(source, dict):
        return str(source.get("repository") or "")
    return str(app.get("repository") or app.get("git_url") or "")


def download_url(app: dict[str, Any]) -> str:
    download = app.get("download")
    if isinstance(download, dict):
        return str(download.get("url") or "")
    return ""


def dependencies_text(app: dict[str, Any]) -> str:
    app_meta = app.get("app")
    deps = []
    if isinstance(app_meta, dict):
        deps += list_value(app_meta.get("dependencies"))
    deps += list_value(app.get("dependencies"))
    return ",".join(dict.fromkeys(deps))


def applaunch_meta(app: dict[str, Any]) -> dict[str, Any]:
    meta = app.get("app")
    if isinstance(meta, dict) and isinstance(meta.get("applaunch"), dict):
        return meta["applaunch"]
    return {}


def desktop_path_for(app: dict[str, Any]) -> Path:
    entry = applaunch_meta(app).get("desktop_entry")
    if entry:
        return app_root() / str(entry)
    slug = str(app.get("share_code") or app.get("title") or app_key(app)).lower().replace(" ", "-")
    return app_root() / "applications" / f"{slug}.desktop"


def installed_records() -> dict[str, Any]:
    data = read_json(installed_path(), {})
    return data if isinstance(data, dict) else {}


def is_installed(app: dict[str, Any]) -> bool:
    key = app_key(app)
    records = installed_records()
    if key in records:
        files = records[key].get("files", [])
        if any(Path(path).exists() for path in files):
            return True
    return desktop_path_for(app).exists()


def merge_apps(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    merged: dict[str, dict[str, Any]] = {}
    for record in records:
        index_apps = record.get("index", {}).get("apps", [])
        full_apps = record.get("full", {}).get("apps", [])
        full_by_key = {app_key(app): app for app in full_apps if isinstance(app, dict) and app_key(app)}
        icons = record.get("icons", {})
        for item in index_apps:
            if not isinstance(item, dict):
                continue
            key = app_key(item)
            if not key:
                continue
            full = dict(full_by_key.get(key, {}))
            full.update({k: v for k, v in item.items() if v not in (None, "", [])})
            full["_registry_url"] = record.get("url", "")
            full["_registry_name"] = record.get("name", "")
            full["_registry_status"] = record.get("status", "")
            full["_icon_local"] = icons.get(key, "")
            merged[key] = full
        for item in full_apps:
            if isinstance(item, dict) and app_key(item) and app_key(item) not in merged:
                item = dict(item)
                item["_registry_url"] = record.get("url", "")
                item["_registry_name"] = record.get("name", "")
                merged[app_key(item)] = item
    return sorted(merged.values(), key=lambda app: (not bool(app.get("featured")), str(app.get("title", "")).lower()))


def free_space_text() -> str:
    try:
        usage = shutil.disk_usage(app_root() if app_root().exists() else "/")
        if usage.free > 1024 * 1024 * 1024:
            return f"{usage.free / (1024 * 1024 * 1024):.1f}G"
        return f"{usage.free // (1024 * 1024)}M"
    except Exception:
        return "-"


def summary() -> None:
    records = load_registry_records(sync_if_empty=True)
    apps = merge_apps(records)
    ok = sum(1 for record in records if record.get("status") in {"ok", "cached"})
    status = f"{len(apps)} apps/{ok} registries"
    emit("META", 1, status, free_space_text(), app_root())
    categories = ["Recommended", "All"]
    for app in apps:
        for category in list_value(app.get("categories")):
            if category and category not in categories:
                categories.append(category)
    for category in categories:
        emit("CAT", category)
    for app in apps:
        key = app_key(app)
        categories_for_app = list_value(app.get("categories"))
        review = app.get("review_status") or app.get("review", {}).get("status") if isinstance(app.get("review"), dict) else app.get("review_status")
        featured = bool(app.get("featured")) or str(review) in {"approved", "ci-passed"}
        icon = app.get("_icon_local") or ""
        size = app.get("download", {}).get("size") if isinstance(app.get("download"), dict) else ""
        emit(
            "APP",
            key,
            app.get("title") or key,
            app.get("version") or "",
            categories_for_app[0] if categories_for_app else "Other",
            "1" if is_installed(app) else "0",
            "1" if featured else "0",
            size or "online",
            app.get("summary") or app.get("description") or "",
            author_text(app),
            source_repo(app),
            icon,
            dependencies_text(app),
        )


def registries() -> None:
    records = {record.get("url"): record for record in load_registry_records(sync_if_empty=False)}
    for source in load_config()["registries"]:
        record = records.get(source["url"], {})
        count = len(record.get("index", {}).get("apps", [])) if isinstance(record.get("index"), dict) else 0
        emit("REG", source["url"], record.get("status") or "not synced", count, record.get("synced_at") or "")


def find_app(app_id: str) -> Optional[dict[str, Any]]:
    for app in merge_apps(load_registry_records(sync_if_empty=True)):
        if app_key(app) == app_id or app.get("share_code") == app_id or app.get("title") == app_id:
            return app
    return None


def plan(app_id: str) -> int:
    app = find_app(app_id)
    if not app:
        emit("ERROR", "app not found", app_id)
        return 1
    missing = []
    if not download_url(app) and not source_repo(app):
        missing.append("package")
    if not os.access(app_root(), os.W_OK):
        missing.append("root-write")
    emit(
        "PLAN",
        app_key(app),
        app.get("title") or app_key(app),
        app.get("version") or "",
        app.get("download", {}).get("size") if isinstance(app.get("download"), dict) else "online",
        free_space_text(),
        dependencies_text(app),
        ",".join(missing),
    )
    return 0 if not missing or missing == ["root-write"] else 1


def verify_sha256(path: Path, expected: str) -> None:
    if not expected:
        return
    expected = expected.lower().strip()
    if len(expected) != 64:
        return
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise RuntimeError(f"sha256 mismatch: {actual}")


def safe_extract_tar(path: Path, dest: Path) -> None:
    with tarfile.open(path) as archive:
        for member in archive.getmembers():
            target = (dest / member.name).resolve()
            if not str(target).startswith(str(dest.resolve())):
                raise RuntimeError("unsafe archive path")
        archive.extractall(dest)


def extract_archive(path: Path, dest: Path) -> Path:
    dest.mkdir(parents=True, exist_ok=True)
    name = path.name.lower()
    if name.endswith(".deb"):
        subprocess.run(["dpkg-deb", "-x", str(path), str(dest)], check=True)
    elif name.endswith((".tar.gz", ".tgz", ".tar", ".tar.xz")):
        safe_extract_tar(path, dest)
    elif name.endswith(".zip"):
        with zipfile.ZipFile(path) as archive:
            archive.extractall(dest)
    else:
        raise RuntimeError(f"unsupported package: {path.name}")
    children = [item for item in dest.iterdir() if item.is_dir()]
    return children[0] if len(children) == 1 else dest


def clone_or_download_source(app: dict[str, Any], dest: Path) -> Path:
    repo = source_repo(app)
    if not repo:
        raise RuntimeError("no source repository")
    if shutil.which("git"):
        subprocess.run(["git", "clone", "--depth", "1", repo, str(dest)], check=True)
        return dest
    archive_url = repo.rstrip("/") + "/archive/refs/heads/main.zip"
    archive = cache_dir() / "downloads" / f"{short_hash(archive_url)}.zip"
    download_file(archive_url, archive)
    return extract_archive(archive, dest)


def package_source(app: dict[str, Any], work: Path) -> Path:
    url = download_url(app)
    if url:
        package = cache_dir() / "downloads" / (short_hash(url) + Path(urllib.parse.urlparse(url).path).name[-24:])
        try:
            download_file(url, package)
            download = app.get("download")
            if isinstance(download, dict):
                verify_sha256(package, str(download.get("sha256") or ""))
            return extract_archive(package, work / "package")
        except Exception as exc:
            print(f"WARN\tdownload failed\t{exc}", file=sys.stderr)
    return clone_or_download_source(app, work / "source")


def find_layout_root(root: Path) -> Path:
    candidates = [root]
    candidates += [item for item in root.rglob("*") if item.is_dir() and item.name in {"APPLaunch", "applaunch"}]
    for candidate in candidates:
        if (candidate / "usr/share/APPLaunch").is_dir():
            return candidate / "usr/share/APPLaunch"
        if (candidate / "share/APPLaunch").is_dir():
            return candidate / "share/APPLaunch"
        if (candidate / "applications").is_dir() or (candidate / "dist").is_dir() or (candidate / "share").is_dir():
            return candidate
    return root


def copy_tree_contents(src: Path, dst: Path, files: list[str]) -> None:
    if not src.exists():
        return
    for item in src.rglob("*"):
        rel = item.relative_to(src)
        if any(part == "__MACOSX" or part.startswith("._") for part in rel.parts):
            continue
        target = dst / rel
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)
            files.append(str(target))


def executable_candidates(layout: Path, app: dict[str, Any]) -> list[Path]:
    candidates = []
    exec_ref = applaunch_meta(app).get("exec")
    if exec_ref:
        name = Path(str(exec_ref)).name
        candidates += list((layout / "dist").glob(name))
        candidates += list((layout / "bin").glob(name))
    if (layout / "dist").is_dir():
        candidates += [p for p in (layout / "dist").iterdir() if p.is_file() and os.access(p, os.X_OK)]
        candidates += [p for p in (layout / "dist").glob("*linux-aarch64*") if p.is_file()]
    if (layout / "bin").is_dir():
        candidates += [p for p in (layout / "bin").iterdir() if p.is_file()]
    unique = []
    seen = set()
    for path in candidates:
        if path not in seen:
            seen.add(path)
            unique.append(path)
    return unique


def build_source_if_needed(layout: Path) -> None:
    if executable_candidates(layout, {}):
        return
    if not (layout / "SConstruct").exists() or not shutil.which("scons"):
        return
    env = os.environ.copy()
    env["CardputerZero"] = "y"
    env["CONFIG_REPO_AUTOMATION"] = "y"
    subprocess.run(["scons", "-j1"], cwd=layout, env=env, check=True, timeout=1800)


def install_layout(layout: Path, app: dict[str, Any]) -> list[str]:
    root = app_root()
    if not root.exists():
        root.mkdir(parents=True, exist_ok=True)
    files: list[str] = []
    build_source_if_needed(layout)
    copy_tree_contents(layout / "applications", root / "applications", files)
    copy_tree_contents(layout / "share", root / "share", files)
    copy_tree_contents(layout / "lib", root / "lib", files)
    for binary in executable_candidates(layout, app):
        target = root / "bin" / binary.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(binary, target)
        target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        files.append(str(target))
    if not any(path.endswith(".desktop") for path in files):
        exec_ref = applaunch_meta(app).get("exec")
        if exec_ref and Path(str(exec_ref)).name:
            exec_value = "/usr/share/APPLaunch/bin/" + Path(str(exec_ref)).name
        else:
            binaries = [Path(path) for path in files if "/bin/" in path]
            if not binaries:
                raise RuntimeError("no executable found in package")
            exec_value = "/usr/share/APPLaunch/bin/" + binaries[0].name
        icon_ref = applaunch_meta(app).get("icon") or ""
        desktop = desktop_path_for(app)
        desktop.parent.mkdir(parents=True, exist_ok=True)
        desktop.write_text(
            "\n".join([
                "[Desktop Entry]",
                f"Name={app.get('title') or app_key(app)}",
                f"Exec={exec_value}",
                "Terminal=false",
                f"Icon={icon_ref}",
                "Type=Application",
                "",
            ]),
            encoding="utf-8",
        )
        files.append(str(desktop))
    return files


def uninstall(app_id: str) -> int:
    app = find_app(app_id)
    records = installed_records()
    record = records.pop(app_id, {})
    files = record.get("files", []) if isinstance(record, dict) else []
    if app:
        files.append(str(desktop_path_for(app)))
        exec_ref = applaunch_meta(app).get("exec")
        icon_ref = applaunch_meta(app).get("icon")
        if exec_ref:
            files.append(str(app_root() / "bin" / Path(str(exec_ref)).name))
        if icon_ref:
            files.append(str(app_root() / str(icon_ref)))
    for path in sorted(set(files), key=len, reverse=True):
        try:
            item = Path(path)
            if item.is_file() or item.is_symlink():
                item.unlink()
        except Exception:
            pass
    write_json(installed_path(), records)
    emit("UNINSTALLED", app_id)
    return 0


def install(app_id: str, reinstall: bool = False) -> int:
    app = find_app(app_id)
    if not app:
        emit("ERROR", "app not found", app_id)
        return 1
    if reinstall:
        uninstall(app_id)
    try:
        with tempfile.TemporaryDirectory(prefix="appstore-", dir=str(cache_dir())) as tmp:
            source = package_source(app, Path(tmp))
            layout = find_layout_root(source)
            files = install_layout(layout, app)
        records = installed_records()
        records[app_key(app)] = {"installed_at": now_text(), "title": app.get("title"), "files": sorted(set(files))}
        write_json(installed_path(), records)
        emit("INSTALLED", app_key(app), app.get("title") or app_key(app))
        return 0
    except Exception as exc:
        emit("ERROR", str(exc))
        return 1


def run_app(app_id: str) -> int:
    app = find_app(app_id)
    if not app:
        emit("ERROR", "app not found", app_id)
        return 1
    desktop = desktop_path_for(app)
    if not desktop.exists():
        emit("ERROR", "desktop entry missing", desktop)
        return 1
    exec_value = ""
    for line in desktop.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("Exec="):
            exec_value = line.split("=", 1)[1].strip()
            break
    if not exec_value:
        emit("ERROR", "desktop Exec missing")
        return 1
    subprocess.Popen(exec_value.split(), cwd=str(app_root()))
    emit("RUNNING", app_id)
    return 0


def add_registry(url: str) -> int:
    config = load_config()
    normalized = normalize_registry_url(url)
    if all(item["url"] != normalized for item in config["registries"]):
        config["registries"].append({"name": registry_name_from_url(normalized), "url": normalized, "enabled": True})
        save_config(config)
    record = sync_one_registry({"name": registry_name_from_url(normalized), "url": normalized, "enabled": True})
    emit("REGISTRY", "ADDED", normalized, record.get("status"), len(record.get("index", {}).get("apps", [])))
    return 0 if record.get("status") != "error" else 1


def remove_registry(url: str) -> int:
    config = load_config()
    normalized = normalize_registry_url(url)
    config["registries"] = [item for item in config["registries"] if item["url"] != normalized]
    save_config(config)
    emit("REGISTRY", "REMOVED", normalized)
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", action="store_true")
    parser.add_argument("--registries", action="store_true")
    parser.add_argument("--sync", action="store_true")
    parser.add_argument("--add-registry")
    parser.add_argument("--remove-registry")
    parser.add_argument("--plan")
    parser.add_argument("--install")
    parser.add_argument("--reinstall")
    parser.add_argument("--uninstall")
    parser.add_argument("--run")
    return parser.parse_args()


def main() -> int:
    ensure_dirs()
    args = parse_args()
    if args.summary:
        summary()
        return 0
    if args.registries:
        registries()
        return 0
    if args.sync:
        records = sync_all()
        ok = sum(1 for record in records if record.get("status") != "error")
        apps = sum(len(record.get("index", {}).get("apps", [])) for record in records)
        emit("SYNC", apps, f"{ok}/{len(records)} registries")
        return 0 if ok else 1
    if args.add_registry:
        return add_registry(args.add_registry)
    if args.remove_registry:
        return remove_registry(args.remove_registry)
    if args.plan:
        return plan(args.plan)
    if args.install:
        return install(args.install, reinstall=False)
    if args.reinstall:
        return install(args.reinstall, reinstall=True)
    if args.uninstall:
        return uninstall(args.uninstall)
    if args.run:
        return run_app(args.run)
    summary()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
