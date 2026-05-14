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
import shlex
import shutil
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Optional


DEFAULT_REGISTRY_URL = "https://cardputerzero.github.io/generated/registry.json"
DEFAULT_REGISTRY_NAME = "CardputerZero Hub"
USER_AGENT = "CardputerZero-AppStore/0.1"
CACHE_BUST_PARAM = "_cz_appstore_ts"


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
    print("\t".join(tsv_escape(field) for field in fields), flush=True)


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
        return DEFAULT_REGISTRY_URL
    if value.startswith(("http://", "https://", "file://")):
        parsed = urllib.parse.urlparse(value)
        if parsed.path.endswith(".json"):
            return value
        return value.rstrip("/") + "/generated/registry.json"
    path = Path(value).expanduser()
    if path.suffix == ".json":
        return path.resolve().as_uri()
    return (path / "generated" / "registry.json").resolve().as_uri()


def load_config() -> dict[str, Any]:
    ensure_dirs()
    data = read_json(config_path(), {})
    registries = data.get("registries")
    if not isinstance(registries, list) or not registries:
        registries = [{"name": DEFAULT_REGISTRY_NAME, "url": DEFAULT_REGISTRY_URL, "enabled": True}]
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
            "enabled": enabled_value(item.get("enabled", True)),
        })
    data["registries"] = normalized or [{"name": DEFAULT_REGISTRY_NAME, "url": DEFAULT_REGISTRY_URL, "enabled": True}]
    return data


def save_config(data: dict[str, Any]) -> None:
    write_json(config_path(), data)


def registry_name_from_url(url: str) -> str:
    parsed = urllib.parse.urlparse(url)
    if parsed.netloc:
        return parsed.netloc
    return Path(parsed.path).stem or "Local Registry"


def enabled_value(value: Any) -> bool:
    if isinstance(value, str):
        return value.strip().lower() not in {"0", "false", "no", "off", "disabled"}
    return bool(value)


def cache_busted_url(url: str) -> str:
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme not in {"http", "https"}:
        return url
    query = [(key, value) for key, value in urllib.parse.parse_qsl(parsed.query, keep_blank_values=True)
             if key != CACHE_BUST_PARAM]
    query.append((CACHE_BUST_PARAM, str(int(time.time() * 1000))))
    return parsed._replace(query=urllib.parse.urlencode(query), fragment="").geturl()


def clean_json_url(url: str) -> str:
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme in {"http", "https", "file"}:
        return parsed._replace(query="", fragment="").geturl()
    return url


def compact_error(exc: Exception) -> str:
    if isinstance(exc, urllib.error.HTTPError):
        return f"HTTP {exc.code} {exc.reason}"
    if isinstance(exc, urllib.error.URLError):
        return str(exc.reason)
    return str(exc)


def registry_error_message(url: str, exc: Exception) -> str:
    name = Path(urllib.parse.urlparse(clean_json_url(url)).path).name or "registry"
    return f"Unable to load {name}: {compact_error(exc)}"


def request_json(url: str) -> Any:
    headers = {
        "User-Agent": USER_AGENT,
        "Accept": "application/json",
        "Cache-Control": "no-cache",
        "Pragma": "no-cache",
    }
    request = urllib.request.Request(cache_busted_url(url), headers=headers)
    with urllib.request.urlopen(request, timeout=15) as response:
        return json.loads(response.read().decode("utf-8"))


def content_range_total(value: str) -> int:
    try:
        return int(value.rsplit("/", 1)[1])
    except Exception:
        return 0


def download_file(url: str, dest: Path, progress_stage: str = "", resume: bool = False) -> None:
    existing = dest.stat().st_size if resume and dest.exists() else 0
    headers = {"User-Agent": USER_AGENT}
    if existing:
        headers["Range"] = f"bytes={existing}-"
    request = urllib.request.Request(url, headers=headers)
    try:
        response_ctx = urllib.request.urlopen(request, timeout=30)
    except Exception:
        if existing:
            headers.pop("Range", None)
            request = urllib.request.Request(url, headers=headers)
            response_ctx = urllib.request.urlopen(request, timeout=30)
            existing = 0
        else:
            raise
    with response_ctx as response:
        append = existing and getattr(response, "status", 200) == 206
        mode = "ab" if append else "wb"
        total_text = response.headers.get("Content-Length") or "0"
        try:
            total = int(total_text)
        except ValueError:
            total = 0
        if append:
            total = content_range_total(response.headers.get("Content-Range") or "") or (existing + total)
        else:
            existing = 0
        done = 0
        if progress_stage and existing:
            percent = int(existing * 100 / total) if total else -1
            emit("PROGRESS", progress_stage, existing, total, percent, "Resuming download")
        done = existing
        next_emit = 0
        with dest.open(mode) as handle:
            while True:
                chunk = response.read(256 * 1024)
                if not chunk:
                    break
                handle.write(chunk)
                done += len(chunk)
                if progress_stage and (done >= next_emit or done == total):
                    percent = int(done * 100 / total) if total else -1
                    emit("PROGRESS", progress_stage, done, total, percent, "Downloading")
                    next_emit = done + 512 * 1024
            if progress_stage:
                percent = 100 if total and done >= total else -1
                emit("PROGRESS", progress_stage, done, total, percent, "Download complete")


def registry_site_root(index_url: str) -> str:
    parsed = urllib.parse.urlparse(clean_json_url(index_url))
    if parsed.scheme in {"http", "https", "file"} and "/generated/" in parsed.path:
        prefix = parsed.path.split("/generated/", 1)[0].rstrip("/") + "/"
        return urllib.parse.urlunparse((parsed.scheme, parsed.netloc, prefix, "", "", ""))
    return urllib.parse.urljoin(clean_json_url(index_url), "./")


def full_registry_url(index_url: str) -> str:
    clean_url = clean_json_url(index_url)
    if clean_url.endswith("/registry-index.json"):
        return clean_url[:-len("registry-index.json")] + "registry.json"
    if clean_url.endswith("/registry.json"):
        return clean_url
    return urllib.parse.urljoin(clean_url, "registry.json")


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
            full_url = full_registry_url(url)
            record["full"] = index if clean_json_url(full_url) == clean_json_url(url) else request_json(full_url)
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
        record["error"] = registry_error_message(url, exc)
        record["last_attempt_at"] = now_text()
        cached = read_json(cache_file_for(url), {})
        if isinstance(cached, dict) and cached.get("index"):
            cached["status"] = "cached"
            cached["error"] = record["error"]
            cached["last_attempt_at"] = record["last_attempt_at"]
            write_json(cache_file_for(url), cached)
            return cached
        write_json(cache_file_for(url), record)
    return record


def validated_registry_record(name: str, url: str) -> tuple[str, dict[str, Any], int]:
    normalized = normalize_registry_url(url)
    if not name.strip():
        raise ValueError("registry name is required")
    if not normalized:
        raise ValueError("registry URL is required")
    record = sync_one_registry({"name": name.strip(), "url": normalized, "enabled": True})
    if record.get("status") == "error":
        raise ValueError(str(record.get("error") or "registry unavailable"))
    index = record.get("index")
    if not isinstance(index, dict) or not isinstance(index.get("apps"), list):
        raise ValueError("invalid registry: apps array missing")
    return normalized, record, len(index.get("apps", []))


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
        if isinstance(cached, dict) and (cached.get("index") or cached.get("status") == "error"):
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


def normalize_locale(value: str) -> str:
    value = (value or "").split(":", 1)[0].split(".", 1)[0].split("@", 1)[0].replace("_", "-")
    if not value:
        return "en"
    lower = value.lower()
    if lower.startswith("zh-tw") or lower.startswith("zh-hk"):
        return "zh-TW"
    if lower.startswith("zh"):
        return "zh-CN"
    if lower.startswith("ja"):
        return "ja"
    if lower.startswith("en"):
        return "en"
    return value


def resolve_locale() -> str:
    for name in ("M5APPSTORE_LOCALE", "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"):
        value = os.environ.get(name)
        if value:
            return normalize_locale(value)
    return "en"


def locale_candidates(locale: str) -> list[str]:
    base = locale.split("-", 1)[0]
    candidates = [locale, base]
    if base == "zh":
        candidates += ["zh-CN", "zh-TW"]
    candidates += ["en", "zh-CN"]
    out = []
    for candidate in candidates:
        if candidate and candidate not in out:
            out.append(candidate)
    return out


def localized_text(app: dict[str, Any], field: str, locale: str) -> str:
    sources = []
    for key in ("i18n", "locales"):
        value = app.get(key)
        if isinstance(value, dict):
            sources.append(value)
    for source in sources:
        for candidate in locale_candidates(locale):
            entry = source.get(candidate)
            if isinstance(entry, dict) and entry.get(field):
                return str(entry[field])
    return str(app.get(field) or "")


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
        repository = str(source.get("repository") or "")
        if repository:
            return repository
    return str(app.get("source_repo") or app.get("repository") or app.get("git_url") or "")


def download_meta(app: dict[str, Any]) -> dict[str, Any]:
    download = app.get("download")
    return download if isinstance(download, dict) else {}


def download_url(app: dict[str, Any]) -> str:
    return str(download_meta(app).get("url") or "")


def download_type(app: dict[str, Any]) -> str:
    return str(download_meta(app).get("type") or "").lower()


def download_md5(app: dict[str, Any]) -> str:
    download = download_meta(app)
    return str(download.get("md5") or download.get("md5sum") or "").lower().strip()


def download_size(app: dict[str, Any]) -> str:
    return str(download_meta(app).get("size") or "")


def deb_package_name(app: dict[str, Any]) -> str:
    download = download_meta(app)
    return str(download.get("package") or app.get("package") or app.get("deb_package") or "").strip()


def is_deb_url(url: str) -> bool:
    path = urllib.parse.urlparse(url).path.lower()
    return path.endswith(".deb")


def is_deb_download(app: dict[str, Any]) -> bool:
    url = download_url(app)
    dtype = download_type(app)
    return bool(url) and (dtype in {"deb", "debian"} or is_deb_url(url))


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


def applaunch_exec(app: dict[str, Any]) -> str:
    return str(applaunch_meta(app).get("exec") or "").strip()


def exec_binary_path(exec_value: str) -> str:
    try:
        parts = shlex.split(exec_value)
    except ValueError:
        parts = exec_value.split()
    if not parts:
        return ""
    command = parts[0]
    if os.path.isabs(command):
        return command
    if "/" in command:
        return str(app_root() / command)
    return shutil.which(command) or ""


def executable_exists(exec_value: str) -> bool:
    binary = exec_binary_path(exec_value)
    return bool(binary) and Path(binary).exists() and os.access(binary, os.X_OK)


def package_installed(package: str) -> bool:
    if not package or not shutil.which("dpkg-query"):
        return False
    try:
        result = subprocess.run(
            ["dpkg-query", "-W", "-f=${Status}", package],
            check=False,
            capture_output=True,
            text=True,
        )
        return "install ok installed" in result.stdout
    except Exception:
        return False


def candidate_execs(app: dict[str, Any], files: list[str]) -> list[str]:
    candidates = []
    preferred = applaunch_exec(app)
    if preferred:
        candidates.append(preferred)
    package = deb_package_name(app)
    if package:
        candidates += [
            f"/usr/lib/{package}/{package}_zero_device",
            f"/usr/bin/{package}",
            f"/usr/lib/{package}/{package}",
        ]
    wanted_names = {
        Path(exec_binary_path(preferred)).name if preferred else "",
        package,
        f"{package}_zero_device" if package else "",
    }
    for path in files:
        p = Path(path)
        if p.name in wanted_names:
            candidates.append(path)
    return list(dict.fromkeys(candidate for candidate in candidates if candidate))


def rewrite_desktop_exec(desktop: Path, exec_value: str) -> None:
    lines = desktop.read_text(encoding="utf-8", errors="ignore").splitlines()
    replaced = False
    out = []
    for line in lines:
        if line.startswith("Exec="):
            out.append(f"Exec={exec_value}")
            replaced = True
        else:
            out.append(line)
    if not replaced:
        out.append(f"Exec={exec_value}")
    desktop.write_text("\n".join(out) + "\n", encoding="utf-8")


def repair_applaunch_desktop(app: dict[str, Any], files: list[str]) -> str:
    desktop = desktop_path_for(app)
    if not desktop.exists():
        return ""
    for exec_value in candidate_execs(app, files):
        binary = exec_binary_path(exec_value)
        if binary and Path(binary).exists() and os.access(binary, os.X_OK):
            rewrite_desktop_exec(desktop, binary)
            return binary
    return ""


def installed_records() -> dict[str, Any]:
    data = read_json(installed_path(), {})
    return data if isinstance(data, dict) else {}


def is_installed(app: dict[str, Any]) -> bool:
    package = deb_package_name(app)
    if package and shutil.which("dpkg-query"):
        return package_installed(package)
    key = app_key(app)
    records = installed_records()
    if key in records:
        record = records[key] if isinstance(records[key], dict) else {}
        package = record.get("package") if isinstance(record, dict) else ""
        if package and shutil.which("dpkg-query"):
            return package_installed(str(package))
        files = record.get("files", [])
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
    locale = resolve_locale()
    return sorted(merged.values(), key=lambda app: (not bool(app.get("featured")), localized_text(app, "title", locale).lower()))


def free_space_text() -> str:
    try:
        usage = shutil.disk_usage(app_root() if app_root().exists() else "/")
        if usage.free > 1024 * 1024 * 1024:
            return f"{usage.free / (1024 * 1024 * 1024):.1f}G"
        return f"{usage.free // (1024 * 1024)}M"
    except Exception:
        return "-"


def summary(sync_if_empty: bool = False) -> None:
    records = load_registry_records(sync_if_empty=sync_if_empty)
    apps = merge_apps(records)
    locale = resolve_locale()
    ok = sum(1 for record in records if record.get("status") == "ok")
    cached = sum(1 for record in records if record.get("status") == "cached")
    failed = sum(1 for record in records if record.get("status") == "error")
    usable = ok + cached
    if failed and not apps:
        status = "registry unavailable"
    elif cached:
        status = f"{len(apps)} apps/cache"
    else:
        status = f"{len(apps)} apps/{usable} registries"
    emit("META", 1, status, free_space_text(), app_root())
    warning = ""
    if failed:
        warning = next((str(record.get("error") or "") for record in records if record.get("status") == "error"), "")
    elif cached:
        warning = "Registry offline; using cached catalog"
    if warning:
        emit("WARN", warning)
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
        review = review_status(app)
        featured = bool(app.get("featured")) or str(review) in {"approved", "ci-passed"}
        icon = app.get("_icon_local") or ""
        size = download_size(app)
        title = localized_text(app, "title", locale) or key
        summary_text = localized_text(app, "summary", locale) or localized_text(app, "description", locale)
        emit(
            "APP",
            key,
            title,
            app.get("version") or "",
            categories_for_app[0] if categories_for_app else "Other",
            "1" if is_installed(app) else "0",
            "1" if featured else "0",
            size or "online",
            summary_text,
            author_text(app),
            source_repo(app),
            icon,
            dependencies_text(app),
            app.get("share_code") or "",
            app.get("_registry_name") or "",
            app.get("updated_at") or app.get("published_at") or "",
            review,
            "1" if is_installable(app) else "0",
        )


def registries() -> None:
    records = {record.get("url"): record for record in load_registry_records(sync_if_empty=False)}
    for source in load_config()["registries"]:
        record = records.get(source["url"], {})
        count = len(record.get("index", {}).get("apps", [])) if isinstance(record.get("index"), dict) else 0
        emit(
            "REG",
            source["url"],
            record.get("status") or "not synced",
            count,
            record.get("synced_at") or record.get("last_attempt_at") or "",
            record.get("error") or "",
            "1" if source.get("enabled", True) else "0",
            source.get("name") or registry_name_from_url(source["url"]),
        )


def find_app(app_id: str) -> Optional[dict[str, Any]]:
    locale = resolve_locale()
    for app in merge_apps(load_registry_records(sync_if_empty=True)):
        if app_key(app) == app_id or app.get("share_code") == app_id or app.get("title") == app_id or localized_text(app, "title", locale) == app_id:
            return app
    return None


def review_status(app: dict[str, Any]) -> str:
    if isinstance(app.get("review"), dict):
        return str(app.get("review_status") or app["review"].get("status") or "")
    return str(app.get("review_status") or "")


def is_installable(app: dict[str, Any]) -> bool:
    return review_status(app) == "approved"


def plan(app_id: str) -> int:
    app = find_app(app_id)
    if not app:
        emit("ERROR", "app not found", app_id)
        return 1
    title = localized_text(app, "title", resolve_locale()) or app_key(app)
    missing = []
    if not is_installable(app):
        missing.append("review-approved")
    if not download_url(app):
        missing.append("package")
    elif not is_deb_download(app):
        missing.append("deb-only")
    if not download_md5(app):
        missing.append("md5")
    if not deb_package_name(app):
        missing.append("package-name")
    if not os.access(app_root(), os.W_OK):
        missing.append("root-write")
    emit(
        "PLAN",
        app_key(app),
        title,
        app.get("version") or "",
        download_size(app) or "deb",
        free_space_text(),
        dependencies_text(app),
        ",".join(missing),
    )
    return 0 if not missing or missing == ["root-write"] else 1


def verify_md5(path: Path, expected: str) -> None:
    emit("PROGRESS", "verify", 0, 0, -1, "Verifying MD5")
    expected = expected.lower().strip()
    if len(expected) != 32:
        raise RuntimeError("download md5 is missing or invalid")
    digest = hashlib.md5()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    actual = digest.hexdigest()
    if actual != expected:
        raise RuntimeError(f"md5 mismatch: expected {expected}, got {actual}")
    emit("PROGRESS", "verify", 1, 1, 100, "MD5 verified")


def deb_cache_path(url: str) -> Path:
    name = Path(urllib.parse.unquote(urllib.parse.urlparse(url).path)).name
    if not name.lower().endswith(".deb"):
        name = short_hash(url) + ".deb"
    safe_name = "".join(ch if ch.isalnum() or ch in "._+-" else "-" for ch in name)
    return cache_dir() / "downloads" / f"{short_hash(url)}-{safe_name}"


def download_deb(app: dict[str, Any]) -> Path:
    url = download_url(app)
    if not url:
        raise RuntimeError("download url is missing")
    if not is_deb_download(app):
        raise RuntimeError("only .deb downloads are supported")
    expected_md5 = download_md5(app)
    if not expected_md5:
        raise RuntimeError("download md5 is required")
    dest = deb_cache_path(url)
    if dest.exists():
        try:
            verify_md5(dest, expected_md5)
            return dest
        except Exception:
            pass
    download_file(url, dest, progress_stage="download", resume=True)
    try:
        verify_md5(dest, expected_md5)
    except Exception:
        dest.unlink(missing_ok=True)
        raise
    return dest


def command_error(result: subprocess.CompletedProcess[str]) -> str:
    text = (result.stdout or "") + (result.stderr or "")
    text = text.strip()
    if not text:
        text = f"command failed: {' '.join(result.args)}"
    return text[-500:]


def run_package_command(args: list[str]) -> None:
    env = os.environ.copy()
    env["DEBIAN_FRONTEND"] = "noninteractive"
    result = subprocess.run(args, check=False, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        raise RuntimeError(command_error(result))


def repair_dpkg_state() -> None:
    if not shutil.which("dpkg"):
        return
    audit = subprocess.run(
        ["dpkg", "--audit"],
        check=False,
        capture_output=True,
        text=True,
    )
    if (audit.stdout or audit.stderr).strip():
        emit("PROGRESS", "apt", 0, 0, -1, "Repairing package database")
        run_package_command(["dpkg", "--configure", "-a"])


def package_files(package: str) -> list[str]:
    if not package or not shutil.which("dpkg-query"):
        return []
    result = subprocess.run(
        ["dpkg-query", "-L", package],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return []
    return [line for line in result.stdout.splitlines() if line.startswith("/")]


def uninstall(app_id: str) -> int:
    app = find_app(app_id)
    records = installed_records()
    key = app_key(app) if app else app_id
    record = records.pop(key, {})
    if key != app_id:
        records.pop(app_id, None)
    package = deb_package_name(app) if app else ""
    if not package and isinstance(record, dict):
        package = str(record.get("package") or "")
    if not package:
        emit("ERROR", "deb package name missing", app_id)
        return 1
    try:
        repair_dpkg_state()
        if shutil.which("apt-get"):
            emit("PROGRESS", "uninstall", 0, 0, -1, "Removing package")
            run_package_command(["apt-get", "-y", "remove", package])
        elif shutil.which("dpkg"):
            emit("PROGRESS", "uninstall", 0, 0, -1, "Removing package")
            run_package_command(["dpkg", "-r", package])
        else:
            raise RuntimeError("apt-get or dpkg is required to uninstall deb packages")
        write_json(installed_path(), records)
        emit("PROGRESS", "uninstall", 1, 1, 100, "Remove complete")
        emit("UNINSTALLED", app_id)
        return 0
    except Exception as exc:
        emit("ERROR", str(exc))
        return 1


def install(app_id: str, reinstall: bool = False, upgrade: bool = False) -> int:
    app = find_app(app_id)
    if not app:
        emit("ERROR", "app not found", app_id)
        return 1
    try:
        if not is_installable(app):
            raise RuntimeError("only approved apps can be installed")
        deb_path = download_deb(app)
        package = deb_package_name(app)
        if not package:
            raise RuntimeError("deb package name missing")
        repair_dpkg_state()
        stage = "upgrade" if upgrade else "install"
        operation = "Upgrading" if upgrade else "Installing"
        complete = "Upgrade complete" if upgrade else "Install complete"
        if shutil.which("apt-get"):
            args = ["apt-get", "-y"]
            if reinstall:
                args.append("--reinstall")
            args += ["install", str(deb_path)]
            emit("PROGRESS", stage, 0, 0, -1, f"{operation} package")
            run_package_command(args)
        elif shutil.which("dpkg"):
            emit("PROGRESS", stage, 0, 0, -1, f"{operation} package")
            run_package_command(["dpkg", "-i", str(deb_path)])
        else:
            raise RuntimeError("apt-get or dpkg is required to install deb packages")
        records = installed_records()
        files = package_files(package)
        repaired_exec = repair_applaunch_desktop(app, files)
        records[app_key(app)] = {
            "installed_at": now_text(),
            "title": localized_text(app, "title", resolve_locale()) or app.get("title"),
            "package": package,
            "deb_path": str(deb_path),
            "exec": repaired_exec or applaunch_exec(app),
            "files": files,
        }
        write_json(installed_path(), records)
        emit("PROGRESS", stage, 1, 1, 100, complete)
        emit("UPGRADED" if upgrade else "INSTALLED", app_key(app), localized_text(app, "title", resolve_locale()) or app_key(app))
        return 0
    except Exception as exc:
        emit("ERROR", str(exc))
        return 1


def add_registry(url: str, name: str = "") -> int:
    config = load_config()
    normalized = normalize_registry_url(url)
    if any(item["url"] == normalized for item in config["registries"]):
        emit("ERROR", "registry already exists", normalized)
        return 1
    try:
        final_name = name.strip() or registry_name_from_url(normalized)
        normalized, record, app_count = validated_registry_record(final_name, normalized)
    except Exception as exc:
        emit("ERROR", str(exc), normalized)
        return 1
    config["registries"].append({"name": final_name, "url": normalized, "enabled": True})
    save_config(config)
    emit("REGISTRY", "ADDED", normalized, record.get("status"), app_count, final_name)
    return 0


def remove_registry(url: str) -> int:
    config = load_config()
    normalized = normalize_registry_url(url)
    config["registries"] = [item for item in config["registries"] if item["url"] != normalized]
    save_config(config)
    emit("REGISTRY", "REMOVED", normalized)
    return 0


def set_registry_enabled(url: str, enabled: bool) -> int:
    config = load_config()
    normalized = normalize_registry_url(url)
    for item in config["registries"]:
        if item["url"] == normalized:
            item["enabled"] = enabled
            save_config(config)
            emit("REGISTRY", "ENABLED" if enabled else "DISABLED", normalized)
            return 0
    emit("ERROR", "registry not found", normalized)
    return 1


def edit_registry(old_url: str, new_url: str, name: str = "") -> int:
    config = load_config()
    old_normalized = normalize_registry_url(old_url)
    new_normalized = normalize_registry_url(new_url)
    updated = False
    rewritten = []
    final_name = name.strip() or registry_name_from_url(new_normalized)
    try:
        new_normalized, record, app_count = validated_registry_record(final_name, new_normalized)
    except Exception as exc:
        emit("ERROR", str(exc), new_normalized)
        return 1
    for item in config["registries"]:
        if item["url"] == old_normalized:
            item = dict(item)
            item["url"] = new_normalized
            item["name"] = final_name
            updated = True
        if all(existing["url"] != item["url"] for existing in rewritten):
            rewritten.append(item)
    if not updated:
        emit("ERROR", "registry not found", old_normalized)
        return 1
    config["registries"] = rewritten
    save_config(config)
    emit("REGISTRY", "UPDATED", old_normalized, new_normalized, record.get("status"), app_count, final_name)
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", action="store_true")
    parser.add_argument("--summary-sync-if-empty", action="store_true")
    parser.add_argument("--registries", action="store_true")
    parser.add_argument("--sync", action="store_true")
    parser.add_argument("--add-registry")
    parser.add_argument("--registry-name")
    parser.add_argument("--remove-registry")
    parser.add_argument("--enable-registry")
    parser.add_argument("--disable-registry")
    parser.add_argument("--edit-registry", nargs=2, metavar=("OLD_URL", "NEW_URL"))
    parser.add_argument("--plan")
    parser.add_argument("--install")
    parser.add_argument("--reinstall")
    parser.add_argument("--upgrade")
    parser.add_argument("--uninstall")
    return parser.parse_args()


def main() -> int:
    ensure_dirs()
    args = parse_args()
    if args.summary:
        summary(sync_if_empty=args.summary_sync_if_empty)
        return 0
    if args.registries:
        registries()
        return 0
    if args.sync:
        records = sync_all()
        ok = sum(1 for record in records if record.get("status") == "ok")
        cached = sum(1 for record in records if record.get("status") == "cached")
        failed = sum(1 for record in records if record.get("status") == "error")
        usable = ok + cached
        apps = sum(len(record.get("index", {}).get("apps", [])) for record in records)
        if failed and not usable:
            message = next((str(record.get("error") or "") for record in records if record.get("status") == "error"), "")
        elif failed or cached:
            message = "Registry offline; using cached catalog"
        elif apps:
            message = "Catalog synced"
        else:
            message = "No apps loaded"
        emit("SYNC", apps, f"{usable}/{len(records)} registries", cached, failed, message)
        if failed and not usable and message:
            emit("ERROR", message)
        return 0 if usable else 1
    if args.add_registry:
        return add_registry(args.add_registry, args.registry_name or "")
    if args.remove_registry:
        return remove_registry(args.remove_registry)
    if args.enable_registry:
        return set_registry_enabled(args.enable_registry, True)
    if args.disable_registry:
        return set_registry_enabled(args.disable_registry, False)
    if args.edit_registry:
        return edit_registry(args.edit_registry[0], args.edit_registry[1], args.registry_name or "")
    if args.plan:
        return plan(args.plan)
    if args.install:
        return install(args.install, reinstall=False)
    if args.reinstall:
        return install(args.reinstall, reinstall=True)
    if args.upgrade:
        return install(args.upgrade, upgrade=True)
    if args.uninstall:
        return uninstall(args.uninstall)
    summary()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
