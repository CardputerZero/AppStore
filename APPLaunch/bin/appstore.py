#!/usr/bin/env python3
"""CardputerZero AppStore registry backend.

The LVGL UI consumes this script through a small TSV protocol. The backend keeps
state on device, syncs JSON registries, caches icons, and installs APPLaunch
packages into /usr/share/APPLaunch.
"""

from __future__ import annotations

import argparse
import contextlib
import fcntl
import functools
import hashlib
import io
import json
import os
import signal
import shlex
import shutil
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Optional


DEFAULT_REGISTRY_URL = "https://cardputerzero.github.io/generated/registry.json"
DEFAULT_REGISTRY_NAME = "CardputerZero Hub"
CN_REGISTRY_URL = "https://cardputer-zero-repo.oss-cn-shenzhen.aliyuncs.com/packages/cn/registry.json"
CN_REGISTRY_NAME = "CardputerZero Hub CN"
REGION_REGISTRIES = {
    "default": {"name": DEFAULT_REGISTRY_NAME, "url": DEFAULT_REGISTRY_URL, "label": "Default"},
    "CN": {"name": CN_REGISTRY_NAME, "url": CN_REGISTRY_URL, "label": "China"},
}
BUILTIN_REGISTRY_URLS = {item["url"] for item in REGION_REGISTRIES.values()}
USER_AGENT = "CardputerZero-AppStore/0.1"
CACHE_BUST_PARAM = "_cz_appstore_ts"
_DPKG_STATUS_CACHE: Optional[dict[str, tuple[bool, str]]] = None
DEBUG_LOG_PATH = Path(os.environ.get("M5APPSTORE_DEBUG_LOG", "/tmp/appstore-backend.log"))
SERVICE_LOCK = threading.Lock()
PACKAGE_COMMAND_TIMEOUT_SECONDS = 15 * 60
SYNC_STATUS_LOCK = threading.Lock()
SYNC_CANCEL_EVENT = threading.Event()
PACKAGE_CANCEL_EVENT = threading.Event()
SYNC_STATUS: dict[str, Any] = {
    "running": False,
    "cancel_requested": False,
    "url": "",
    "detail": "Idle",
    "percent": -1,
    "phase": "idle",
    "updated_at": "",
}


class SyncCancelled(RuntimeError):
    pass


def log_debug(message: str) -> None:
    line = f"[AppStore] {now_text()} pid={os.getpid()} {message}"
    print(line, file=sys.stderr, flush=True)
    try:
        with DEBUG_LOG_PATH.open("a", encoding="utf-8") as fp:
            fp.write(line + "\n")
    except Exception:
        pass


def state_dir() -> Path:
    return Path(os.environ.get("M5APPSTORE_STATE_DIR", "~/.local/share/cardputerzero-appstore")).expanduser()


def app_root() -> Path:
    return Path(os.environ.get("M5APPSTORE_APP_ROOT", "/usr/share/APPLaunch"))


def cache_dir() -> Path:
    return Path(os.environ.get("M5APPSTORE_CACHE_DIR", "~/.cache/cardputerzero-appstore")).expanduser()


def config_path() -> Path:
    return state_dir() / "registries.json"


def installed_path() -> Path:
    return state_dir() / "installed.json"


def pending_package_path() -> Path:
    return state_dir() / "pending-package.json"


def completed_package_path() -> Path:
    return state_dir() / "completed-package.json"


def completed_package_records() -> dict[str, Any]:
    data = read_json(completed_package_path(), {})
    if not isinstance(data, dict):
        return {}
    if data.get("transaction_id"):
        return {str(data["transaction_id"]): data}
    return data


def record_completed_package(transaction_id: str, action: str, app_id: str,
                             package: str, version: str) -> None:
    records = completed_package_records()
    records[transaction_id] = {
        "transaction_id": transaction_id, "action": action, "app_id": app_id,
        "package": package, "version": version, "completed_at": now_text(),
    }
    if len(records) > 32:
        records = dict(sorted(records.items(), key=lambda item: item[1].get("completed_at", ""))[-32:])
    write_json(completed_package_path(), records)


def package_transaction_locked(function):
    @functools.wraps(function)
    def wrapped(*args, **kwargs):
        state_dir().mkdir(parents=True, exist_ok=True)
        with (state_dir() / "package-transaction.lock").open("a+") as lock_file:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
            return function(*args, **kwargs)
    return wrapped


def now_text() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")


def tsv_escape(value: Any) -> str:
    text = "" if value is None else str(value)
    return text.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def tsv_unescape(value: str) -> str:
    out = []
    escaped = False
    for ch in value:
        if escaped:
            if ch == "t":
                out.append("\t")
            elif ch == "n":
                out.append("\n")
            elif ch == "r":
                out.append("\r")
            else:
                out.append(ch)
            escaped = False
        elif ch == "\\":
            escaped = True
        else:
            out.append(ch)
    if escaped:
        out.append("\\")
    return "".join(out)


def split_tsv_line(line: str) -> list[str]:
    return [tsv_unescape(field) for field in line.rstrip("\r\n").split("\t")]


def emit(*fields: Any) -> None:
    print("\t".join(tsv_escape(field) for field in fields), flush=True)


def update_sync_status(
    *,
    running: Optional[bool] = None,
    url: Optional[str] = None,
    detail: Optional[str] = None,
    percent: Optional[int] = None,
    phase: Optional[str] = None,
    cancel_requested: Optional[bool] = None,
) -> None:
    with SYNC_STATUS_LOCK:
        if running is not None:
            SYNC_STATUS["running"] = running
        if url is not None:
            SYNC_STATUS["url"] = url
        if detail is not None:
            SYNC_STATUS["detail"] = detail
        if percent is not None:
            SYNC_STATUS["percent"] = percent
        if phase is not None:
            SYNC_STATUS["phase"] = phase
        if cancel_requested is not None:
            SYNC_STATUS["cancel_requested"] = cancel_requested
        SYNC_STATUS["updated_at"] = now_text()


def sync_status_snapshot() -> dict[str, Any]:
    with SYNC_STATUS_LOCK:
        return dict(SYNC_STATUS)


def emit_sync_status() -> None:
    status = sync_status_snapshot()
    emit(
        "STATUS",
        1 if status.get("running") else 0,
        1 if status.get("cancel_requested") else 0,
        status.get("url", ""),
        status.get("detail", ""),
        status.get("percent", -1),
        status.get("phase", ""),
        status.get("updated_at", ""),
    )


def request_sync_cancel() -> None:
    SYNC_CANCEL_EVENT.set()
    update_sync_status(cancel_requested=True, detail="Cancelling sync...", phase="cancel")


def check_sync_cancel() -> None:
    if SYNC_CANCEL_EVENT.is_set() and sync_status_snapshot().get("running"):
        update_sync_status(cancel_requested=True, detail="Sync cancelled", percent=-1, phase="cancelled")
        raise SyncCancelled("sync cancelled")


def short_hash(value: str) -> str:
    return hashlib.sha1(value.encode("utf-8")).hexdigest()[:16]


def ensure_dirs() -> None:
    for path in (state_dir(), cache_dir(), cache_dir() / "icons",
                 cache_dir() / "screenshots", cache_dir() / "downloads"):
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
    try:
        current = path.stat()
        os.chmod(tmp, current.st_mode & 0o777)
        if os.geteuid() == 0:
            os.chown(tmp, current.st_uid, current.st_gid)
    except FileNotFoundError:
        pass
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


def normalize_region(value: Any) -> str:
    text = str(value or "default").strip()
    if not text:
        return "default"
    if text in REGION_REGISTRIES:
        return text
    upper = text.upper()
    if upper in REGION_REGISTRIES:
        return upper
    lower = text.lower()
    if lower == "china":
        return "CN"
    return "default"


def normalize_region_mode(value: Any) -> str:
    text = str(value or "auto").strip()
    if not text:
        return "auto"
    if text.lower() == "auto":
        return "auto"
    return normalize_region(text)


def region_registry(region: str) -> dict[str, Any]:
    code = normalize_region(region)
    item = REGION_REGISTRIES[code]
    return {
        "name": item["name"],
        "url": item["url"],
        "enabled": True,
        "builtin": True,
        "region": code,
    }


def is_builtin_registry_url(url: str) -> bool:
    return normalize_registry_url(url) in BUILTIN_REGISTRY_URLS


def load_config() -> dict[str, Any]:
    ensure_dirs()
    data = read_json(config_path(), {})
    if not isinstance(data, dict):
        data = {}
    selected_mode = normalize_region_mode(data.get("region", "auto"))
    active_region = normalize_region(data.get(
        "active_region",
        "default" if selected_mode == "auto" else selected_mode,
    ))
    registries = data.get("registries")
    if not isinstance(registries, list):
        registries = []
    builtin = region_registry(active_region)
    normalized = [builtin]
    seen = {builtin["url"]}
    for item in registries:
        if isinstance(item, str):
            item = {"url": item}
        if not isinstance(item, dict):
            continue
        url = normalize_registry_url(str(item.get("url", "")))
        if url in seen:
            if url == builtin["url"]:
                builtin["enabled"] = enabled_value(item.get("enabled", True))
            continue
        if url in BUILTIN_REGISTRY_URLS:
            continue
        seen.add(url)
        normalized.append({
            "name": item.get("name") or registry_name_from_url(url),
            "url": url,
            "enabled": enabled_value(item.get("enabled", True)),
        })
    data["region"] = selected_mode
    data["active_region"] = active_region
    data["registries"] = normalized
    log_debug(
        "load_config "
        f"path={config_path()} region={selected_mode} active={active_region} "
        f"registries={len(normalized)} cache_dir={cache_dir()}"
    )
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


def curl_text(url: str, timeout: int = 5) -> str:
    curl = shutil.which("curl")
    if not curl:
        log_debug("region_detect skip: curl not found")
        return ""
    try:
        result = subprocess.run(
            [curl, "-fsSL", "--max-time", str(timeout), url],
            check=False,
            capture_output=True,
            text=True,
        )
    except Exception as exc:
        log_debug(f"region_detect curl failed url={url} error={compact_error(exc)}")
        return ""
    if result.returncode != 0:
        error = (result.stderr or result.stdout or "").strip()
        log_debug(f"region_detect curl error url={url} rc={result.returncode} error={compact_error(error)}")
        return ""
    return result.stdout.strip()


def detect_country_code() -> str:
    ip = curl_text("ifconfig.me")
    if not ip:
        return ""
    quoted_ip = urllib.parse.quote(ip.splitlines()[0].strip(), safe=":.")
    country = curl_text(f"https://ipinfo.io/{quoted_ip}/country").splitlines()
    if not country:
        return ""
    code = country[0].strip().upper()
    if len(code) >= 2:
        return code[:2]
    return ""


def auto_region_for_sync() -> str:
    country = detect_country_code()
    region = "CN" if country == "CN" else "default"
    log_debug(f"region_detect country={country or '-'} selected={region}")
    return region


def config_with_region(config: dict[str, Any], region: str) -> dict[str, Any]:
    mode = normalize_region_mode(region)
    selected = normalize_region(config.get("active_region", "default") if mode == "auto" else mode)
    custom_registries = [
        item for item in config.get("registries", [])
        if isinstance(item, dict) and not item.get("builtin") and not is_builtin_registry_url(str(item.get("url", "")))
    ]
    config["region"] = mode
    config["active_region"] = selected
    config["registries"] = [region_registry(selected), *custom_registries]
    return config


def config_for_sync(config: dict[str, Any]) -> dict[str, Any]:
    mode = normalize_region_mode(config.get("region", "auto"))
    active = auto_region_for_sync() if mode == "auto" else normalize_region(mode)
    config = config_with_region(config, mode)
    config["active_region"] = active
    custom_registries = [
        item for item in config.get("registries", [])
        if isinstance(item, dict) and not item.get("builtin") and not is_builtin_registry_url(str(item.get("url", "")))
    ]
    config["registries"] = [region_registry(active), *custom_registries]
    return config


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


def request_json(url: str, detail: str = "") -> Any:
    headers = {
        "User-Agent": USER_AGENT,
        "Accept": "application/json",
        "Cache-Control": "no-cache",
        "Pragma": "no-cache",
    }
    fetch_url = cache_busted_url(url)
    if detail:
        update_sync_status(url=url, detail=detail, percent=-1, phase="download")
    log_debug(f"request_json start url={url} fetch_url={fetch_url}")
    request = urllib.request.Request(fetch_url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            raw = response.read()
            status = getattr(response, "status", "file")
            log_debug(f"request_json ok url={url} status={status} bytes={len(raw)}")
            if detail:
                update_sync_status(url=url, detail=f"{detail} complete", percent=-1, phase="parse")
            return json.loads(raw.decode("utf-8"))
    except Exception as exc:
        log_debug(f"request_json failed url={url} error={compact_error(exc)}")
        raise


def content_range_total(value: str) -> int:
    try:
        return int(value.rsplit("/", 1)[1])
    except Exception:
        return 0


def download_file(url: str, dest: Path, progress_stage: str = "", resume: bool = False) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    existing = dest.stat().st_size if resume and dest.exists() else 0
    headers = {"User-Agent": USER_AGENT}
    if existing:
        headers["Range"] = f"bytes={existing}-"
    log_debug(
        f"download start url={url} dest={dest} resume={int(resume)} existing={existing} "
        f"stage={progress_stage or '-'}"
    )
    request = urllib.request.Request(url, headers=headers)
    try:
        response_ctx = urllib.request.urlopen(request, timeout=30)
    except Exception as exc:
        if existing:
            log_debug(f"download resume failed url={url} error={compact_error(exc)}; retry full")
            headers.pop("Range", None)
            request = urllib.request.Request(url, headers=headers)
            try:
                response_ctx = urllib.request.urlopen(request, timeout=30)
            except Exception as retry_exc:
                log_debug(f"download failed url={url} dest={dest} error={compact_error(retry_exc)}")
                raise
            existing = 0
        else:
            log_debug(f"download failed url={url} dest={dest} error={compact_error(exc)}")
            raise
    with response_ctx as response:
        append = existing and getattr(response, "status", 200) == 206
        mode = "ab" if append else "wb"
        status = getattr(response, "status", "file")
        total_text = response.headers.get("Content-Length") or "0"
        try:
            total = int(total_text)
        except ValueError:
            total = 0
        if append:
            total = content_range_total(response.headers.get("Content-Range") or "") or (existing + total)
        else:
            existing = 0
        log_debug(
            f"download response url={url} status={status} dest={dest} mode={mode} "
            f"content_length={total_text} total={total}"
        )
        done = 0
        if progress_stage and existing:
            percent = int(existing * 100 / total) if total else -1
            emit("PROGRESS", progress_stage, existing, total, percent, "Resuming download")
        done = existing
        next_emit = 0
        with dest.open(mode) as handle:
            while True:
                if PACKAGE_CANCEL_EVENT.is_set():
                    raise RuntimeError("package preparation cancelled")
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
    try:
        final_size = dest.stat().st_size
    except OSError:
        final_size = -1
    log_debug(f"download ok url={url} dest={dest} bytes={final_size}")


def registry_site_root(index_url: str) -> str:
    parsed = urllib.parse.urlparse(clean_json_url(index_url))
    if parsed.scheme in {"http", "https", "file"} and "/generated/" in parsed.path:
        prefix = parsed.path.split("/generated/", 1)[0].rstrip("/") + "/"
        return urllib.parse.urlunparse((parsed.scheme, parsed.netloc, prefix, "", "", ""))
    return urllib.parse.urljoin(clean_json_url(index_url), "./")


def full_registry_url(index_url: str) -> str:
    clean_url = clean_json_url(index_url)
    parsed = urllib.parse.urlparse(clean_url)
    if Path(parsed.path).suffix == ".json":
        return clean_url
    return urllib.parse.urljoin(clean_url.rstrip("/") + "/", "registry.json")


def cache_file_for(url: str) -> Path:
    return cache_dir() / f"registry-{short_hash(url)}.json"


def cache_media(index_url: str, media_ref: str, subdir: str) -> str:
    if not media_ref:
        return ""
    if media_ref.startswith("file://"):
        path = urllib.parse.urlparse(media_ref).path
        log_debug(f"cache_media file subdir={subdir} media_ref={media_ref} path={path}")
        return path
    if media_ref.startswith(("http://", "https://")):
        media_url = media_ref
    else:
        media_url = urllib.parse.urljoin(registry_site_root(index_url), media_ref)
    suffix = Path(urllib.parse.urlparse(media_url).path).suffix or ".png"
    dest = cache_dir() / subdir / f"{short_hash(media_url)}{suffix}"
    if dest.exists() and dest.stat().st_size > 0:
        log_debug(f"cache_media hit subdir={subdir} url={media_url} dest={dest} bytes={dest.stat().st_size}")
        return str(dest)
    try:
        log_debug(f"cache_media fetch subdir={subdir} index={index_url} ref={media_ref} url={media_url} dest={dest}")
        download_file(media_url, dest)
        return str(dest)
    except Exception as exc:
        log_debug(f"cache_media failed subdir={subdir} url={media_url} dest={dest} error={compact_error(exc)}")
        return ""


def cache_icon(index_url: str, icon_ref: str) -> str:
    return cache_media(index_url, icon_ref, "icons")


def cache_screenshot(index_url: str, screenshot_ref: str) -> str:
    return cache_media(index_url, screenshot_ref, "screenshots")


def sync_one_registry(source: dict[str, Any]) -> dict[str, Any]:
    url = source["url"]
    cache_path = cache_file_for(url)
    update_sync_status(url=url, detail="Accessing registry", percent=-1, phase="registry")
    check_sync_cancel()
    log_debug(f"sync_registry start name={source.get('name') or '-'} url={url} cache={cache_path}")
    record: dict[str, Any] = {
        "name": source.get("name") or registry_name_from_url(url),
        "url": url,
        "status": "ok",
        "synced_at": now_text(),
        "index": {},
        "full": {},
        "icons": {},
        "screenshots": {},
    }
    try:
        index = request_json(url, "Downloading registry index")
        record["index"] = index
        check_sync_cancel()
        try:
            full_url = full_registry_url(url)
            record["full"] = index if clean_json_url(full_url) == clean_json_url(url) else request_json(
                full_url, "Downloading full registry"
            )
        except Exception as exc:
            record["full_error"] = str(exc)
            record["full"] = {}
        check_sync_cancel()
        index_apps = [app for app in index.get("apps", []) if isinstance(app, dict)]
        full_apps = [app for app in record["full"].get("apps", []) if isinstance(app, dict)]
        full_by_key = {app_key(app): app for app in full_apps if app_key(app)}
        merged_for_assets = []
        seen_asset_keys = set()
        for item in index_apps:
            key = app_key(item)
            if not key:
                continue
            full = dict(full_by_key.get(key, {}))
            full.update({k: v for k, v in item.items() if v not in (None, "", [])})
            merged_for_assets.append(full)
            seen_asset_keys.add(key)
        for item in full_apps:
            key = app_key(item)
            if key and key not in seen_asset_keys:
                merged_for_assets.append(item)
                seen_asset_keys.add(key)

        total_assets = max(1, len(merged_for_assets))
        for index, app in enumerate(merged_for_assets):
            check_sync_cancel()
            key = app_key(app)
            if not key:
                continue
            percent = min(95, 20 + int((index + 1) * 70 / total_assets))
            update_sync_status(url=url, detail="Caching app artwork", percent=percent, phase="assets")
            assets = app.get("assets") if isinstance(app.get("assets"), dict) else {}
            icon = app.get("icon") or assets.get("icon")
            local_icon = cache_icon(url, str(icon or ""))
            if local_icon:
                record["icons"][key] = local_icon
            local_screenshots = []
            for screenshot in list_value(assets.get("screenshots") or app.get("screenshots")):
                local = cache_screenshot(url, screenshot)
                if local:
                    local_screenshots.append(local)
            if local_screenshots:
                record["screenshots"][key] = local_screenshots
        update_sync_status(url=url, detail="Writing registry cache", percent=98, phase="cache")
        write_json(cache_path, record)
        log_debug(
            f"sync_registry ok url={url} apps={len(index_apps)} full_apps={len(full_apps)} "
            f"icons={len(record['icons'])} screenshots={len(record['screenshots'])} cache={cache_path}"
        )
    except Exception as exc:
        if isinstance(exc, SyncCancelled):
            log_debug(f"sync_registry cancelled url={url} cache={cache_path}")
            raise
        log_debug(f"sync_registry failed url={url} error={compact_error(exc)} cache={cache_path}")
        record["status"] = "error"
        record["error"] = registry_error_message(url, exc)
        record["last_attempt_at"] = now_text()
        cached = read_json(cache_path, {})
        if isinstance(cached, dict) and cached.get("index"):
            cached["status"] = "cached"
            cached["error"] = record["error"]
            cached["last_attempt_at"] = record["last_attempt_at"]
            write_json(cache_path, cached)
            log_debug(f"sync_registry fallback_cached url={url} cache={cache_path}")
            return cached
        write_json(cache_path, record)
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
    SYNC_CANCEL_EVENT.clear()
    update_sync_status(
        running=True,
        cancel_requested=False,
        url="",
        detail="Loading registry configuration",
        percent=-1,
        phase="config",
    )
    config = load_config()
    check_sync_cancel()
    config = config_for_sync(config)
    save_config(config)
    log_debug(
        "sync_all "
        f"region={config.get('region')} active={config.get('active_region')} "
        f"enabled={sum(1 for item in config['registries'] if item.get('enabled', True))} "
        f"registries={len(config['registries'])}"
    )
    records = []
    enabled_sources = [source for source in config["registries"] if source.get("enabled", True)]
    total = max(1, len(enabled_sources))
    try:
        for index, source in enumerate(enabled_sources):
            check_sync_cancel()
            update_sync_status(
                url=source.get("url", ""),
                detail=f"Syncing registry {index + 1}/{total}",
                percent=int(index * 100 / total),
                phase="registry",
            )
            records.append(sync_one_registry(source))
            update_sync_status(
                url=source.get("url", ""),
                detail=f"Registry {index + 1}/{total} synced",
                percent=int((index + 1) * 100 / total),
                phase="registry",
            )
    except SyncCancelled:
        update_sync_status(running=False, cancel_requested=True, detail="Sync cancelled", percent=-1, phase="cancelled")
        raise
    except Exception as exc:
        update_sync_status(running=False, detail=f"Sync error: {compact_error(exc)}", percent=-1, phase="error")
        raise
    update_sync_status(running=False, cancel_requested=False, detail="Catalog synced", percent=100, phase="complete")
    return records


def load_registry_records(sync_if_empty: bool = False) -> list[dict[str, Any]]:
    config = load_config()
    log_debug(f"load_registry_records start sync_if_empty={sync_if_empty} registries={len(config['registries'])}")
    records = []
    for source in config["registries"]:
        if not source.get("enabled", True):
            log_debug(f"load_registry_records skip disabled url={source.get('url')}")
            continue
        cache_path = cache_file_for(source["url"])
        cached = read_json(cache_path, {})
        if isinstance(cached, dict) and (cached.get("index") or cached.get("status") == "error"):
            records.append(cached)
            app_count = 0
            if isinstance(cached.get("index"), dict):
                app_count = len(cached.get("index", {}).get("apps", []))
            log_debug(
                "load_registry_records hit "
                f"url={source['url']} cache={cache_path} status={cached.get('status')} apps={app_count}"
            )
        else:
            log_debug(f"load_registry_records miss url={source['url']} cache={cache_path}")
    if not records and sync_if_empty:
        log_debug("load_registry_records empty: syncing")
        records = sync_all()
    log_debug(f"load_registry_records done records={len(records)}")
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


def is_executable_file(path: str | Path) -> bool:
    p = Path(path)
    return p.is_file() and os.access(p, os.X_OK)


def executable_exists(exec_value: str) -> bool:
    binary = exec_binary_path(exec_value)
    return bool(binary) and is_executable_file(binary)


def package_dpkg_status_is_installed(status: str) -> bool:
    # The second abbreviation character is the current state. The first is
    # only the requested action, so `ri` is still installed after a failed
    # removal. A third character indicates a dpkg error and is not healthy.
    return len(status) == 2 and status[0] in "uihpr" and status[1] == "i"


def dpkg_status_cache() -> dict[str, tuple[bool, str]]:
    global _DPKG_STATUS_CACHE
    if _DPKG_STATUS_CACHE is not None:
        return _DPKG_STATUS_CACHE
    _DPKG_STATUS_CACHE = {}
    if not shutil.which("dpkg-query"):
        return _DPKG_STATUS_CACHE
    try:
        result = subprocess.run(
            ["dpkg-query", "-W", "-f=${Package}\t${db:Status-Abbrev}\t${Version}\n"],
            check=False,
            capture_output=True,
            text=True,
        )
    except Exception:
        return _DPKG_STATUS_CACHE
    if result.returncode != 0:
        return _DPKG_STATUS_CACHE
    for line in result.stdout.splitlines():
        parts = line.split("\t", 2)
        if len(parts) < 3:
            continue
        package, status, version = parts
        if package:
            _DPKG_STATUS_CACHE[package] = (
                package_dpkg_status_is_installed(status.strip()), version.strip()
            )
    return _DPKG_STATUS_CACHE


def invalidate_dpkg_status_cache(reason: str = "") -> None:
    global _DPKG_STATUS_CACHE
    _DPKG_STATUS_CACHE = None
    log_debug(f"dpkg_status_cache invalidated reason={reason or '-'}")


def package_installed(package: str) -> bool:
    if not package:
        return False
    return dpkg_status_cache().get(package, (False, ""))[0]


def package_version(package: str) -> str:
    if not package:
        return ""
    installed, version = dpkg_status_cache().get(package, (False, ""))
    return version if installed else ""


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
    app_bin = app_root() / "bin"
    for path in files:
        p = Path(path)
        if p.parent == app_bin or (package and p.parent == Path("/usr/lib") / package):
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
        if binary and is_executable_file(binary):
            try:
                rewrite_desktop_exec(desktop, binary)
            except PermissionError as exc:
                log_debug(f"desktop repair skipped path={desktop} error={compact_error(exc)}")
                return binary
            return binary
    return ""


def installed_records() -> dict[str, Any]:
    data = read_json(installed_path(), {})
    return data if isinstance(data, dict) else {}


def is_installed(app: dict[str, Any]) -> bool:
    package = deb_package_name(app)
    if package:
        return package_installed(package)
    key = app_key(app)
    records = installed_records()
    if key in records:
        record = records[key] if isinstance(records[key], dict) else {}
        package = record.get("package") if isinstance(record, dict) else ""
        if package:
            return package_installed(str(package))
        files = record.get("files", [])
        if any(Path(path).exists() for path in files):
            return True
    return desktop_path_for(app).exists()


def installed_version(app: dict[str, Any]) -> str:
    package = deb_package_name(app)
    if package:
        version = package_version(package)
        if version:
            return version
    key = app_key(app)
    record = installed_records().get(key, {})
    if isinstance(record, dict):
        package = str(record.get("package") or "")
        if package:
            version = package_version(package)
            if version:
                return version
        return str(record.get("version") or "")
    return ""


def merge_apps(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    merged: dict[str, dict[str, Any]] = {}
    for record in records:
        index_apps = record.get("index", {}).get("apps", [])
        full_apps = record.get("full", {}).get("apps", [])
        full_by_key = {app_key(app): app for app in full_apps if isinstance(app, dict) and app_key(app)}
        icons = record.get("icons", {})
        screenshots = record.get("screenshots", {})
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
            full["_screenshots_local"] = screenshots.get(key, [])
            merged[key] = full
        for item in full_apps:
            if isinstance(item, dict) and app_key(item) and app_key(item) not in merged:
                key = app_key(item)
                item = dict(item)
                item["_registry_url"] = record.get("url", "")
                item["_registry_name"] = record.get("name", "")
                item["_registry_status"] = record.get("status", "")
                item["_icon_local"] = icons.get(key, "")
                item["_screenshots_local"] = screenshots.get(key, [])
                merged[key] = item
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
    log_debug(
        "summary "
        f"sync_if_empty={sync_if_empty} records={len(records)} apps={len(apps)} "
        f"ok={ok} cached={cached} failed={failed} usable={usable} status={status}"
    )
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
        images = [icon] if icon else []
        images += [str(path) for path in app.get("_screenshots_local", []) if path]
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
            ",".join(images),
            dependencies_text(app),
            app.get("share_code") or "",
            app.get("_registry_name") or "",
            app.get("updated_at") or app.get("published_at") or "",
            review,
            "1" if is_installable(app) else "0",
            installed_version(app),
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
            "1" if source.get("builtin") else "0",
            source.get("region") or "",
        )


def regions() -> None:
    config = load_config()
    selected = normalize_region_mode(config.get("region", "auto"))
    active = normalize_region(config.get("active_region", "default"))
    current = REGION_REGISTRIES[active]
    label = "Auto" if selected == "auto" else REGION_REGISTRIES[normalize_region(selected)]["label"]
    emit("REGION", selected, label, current["url"], active)
    emit("REGION_OPTION", "auto", "Auto", current["url"], "1" if selected == "auto" else "0")
    for code, item in REGION_REGISTRIES.items():
        emit("REGION_OPTION", code, item["label"], item["url"], "1" if code == selected else "0")


def set_region(region: str) -> int:
    requested = str(region or "").strip()
    selected = normalize_region_mode(requested)
    if requested and selected == "default" and requested.lower() not in {"auto", "default", "global"}:
        emit("ERROR", "unknown region", requested)
        return 1
    config = load_config()
    config = config_with_region(config, selected)
    save_config(config)
    active = normalize_region(config.get("active_region", "default"))
    item = REGION_REGISTRIES[active]
    label = "Auto" if selected == "auto" else REGION_REGISTRIES[normalize_region(selected)]["label"]
    emit("REGION", selected, label, item["url"], active)
    return 0


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


def partial_deb_path(dest: Path) -> Path:
    return dest.with_name(dest.name + ".parted")


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
    partial = partial_deb_path(dest)
    log_debug(f"download_deb start app={app_key(app)} url={url} dest={dest} partial={partial}")
    if dest.exists():
        try:
            verify_md5(dest, expected_md5)
            partial.unlink(missing_ok=True)
            log_debug(f"download_deb cache_valid app={app_key(app)} dest={dest}")
            return dest
        except Exception as exc:
            log_debug(f"download_deb cache_invalid app={app_key(app)} dest={dest} error={compact_error(exc)}")
            dest.unlink(missing_ok=True)
    download_file(url, partial, progress_stage="download", resume=True)
    try:
        verify_md5(partial, expected_md5)
    except Exception:
        partial.unlink(missing_ok=True)
        raise
    partial.replace(dest)
    log_debug(f"download_deb ok app={app_key(app)} dest={dest}")
    return dest


def deb_file_field(path: Path, field: str) -> str:
    tool = shutil.which("dpkg-deb")
    if not tool:
        raise RuntimeError("dpkg-deb is required to inspect package version")
    result = subprocess.run([tool, "-f", str(path), field], check=False,
                            capture_output=True, text=True, timeout=30)
    if result.returncode != 0 or not result.stdout.strip():
        raise RuntimeError(command_error(result))
    return result.stdout.strip()


def deb_file_version(path: Path) -> str:
    return deb_file_field(path, "Version")


def deb_file_package(path: Path) -> str:
    return deb_file_field(path, "Package")


def command_error(result: subprocess.CompletedProcess[str]) -> str:
    text = (result.stdout or "") + (result.stderr or "")
    text = text.strip()
    if not text:
        text = f"command failed: {' '.join(result.args)}"
    return text[-500:]


def package_command_args(args: list[str]) -> list[str]:
    if os.geteuid() != 0:
        raise RuntimeError("package operations must run through the privileged helper")
    return args


def run_package_command(args: list[str]) -> None:
    env = os.environ.copy()
    env["DEBIAN_FRONTEND"] = "noninteractive"
    result = subprocess.run(
        package_command_args(args),
        check=False,
        capture_output=True,
        text=True,
        env=env,
        timeout=PACKAGE_COMMAND_TIMEOUT_SECONDS,
    )
    if result.returncode != 0:
        raise RuntimeError(command_error(result))


def dpkg_audit_detail() -> str:
    if not shutil.which("dpkg"):
        return ""
    result = subprocess.run(
        ["dpkg", "--audit"], check=False, capture_output=True, text=True, timeout=60
    )
    detail = compact_error((result.stdout or "") + (result.stderr or ""))
    if result.returncode != 0:
        return detail or f"dpkg --audit failed with exit code {result.returncode}"
    return detail


def deb_dependencies_satisfied(path: str) -> bool:
    if not shutil.which("dpkg-deb"):
        return False
    dependencies = []
    for field in ("Pre-Depends", "Depends"):
        result = subprocess.run(
            ["dpkg-deb", "-f", path, field], check=False,
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            return False
        value = result.stdout.strip()
        if value:
            dependencies.append(value)
    if not dependencies:
        return True
    if not shutil.which("dpkg-checkbuilddeps"):
        return False
    result = subprocess.run(
        ["dpkg-checkbuilddeps", "-d", ", ".join(dependencies), "/dev/null"],
        check=False, capture_output=True, text=True, timeout=60,
    )
    return result.returncode == 0


def discard_unstarted_package_job(transaction_id: str, pending_path_value: str) -> None:
    if not transaction_id or not pending_path_value:
        return
    path = Path(pending_path_value)
    pending = read_json(path, {})
    if (isinstance(pending, dict) and
            str(pending.get("transaction_id") or "") == transaction_id and
            not pending.get("helper_completed")):
        path.unlink(missing_ok=True)


def package_dpkg_status(package: str) -> tuple[str, str]:
    if not package or not shutil.which("dpkg-query"):
        return "unknown", ""
    env = os.environ.copy()
    env["LC_ALL"] = "C"
    result = subprocess.run(
        ["dpkg-query", "-W", "-f=${db:Status-Abbrev}\t${Version}", package],
        check=False, capture_output=True, text=True, timeout=30, env=env,
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        if result.returncode == 1 and "no packages found matching" in detail:
            return "absent", ""
        return "unknown", ""
    status, _, version = result.stdout.partition("\t")
    return status.strip() or "unknown", version.strip()


def package_dpkg_status_is_absent(status: str) -> bool:
    if status == "absent":
        return True
    # dpkg's second status-abbrev character is the current package state:
    # `n` is not-installed and `c` is config-files-only.
    return (len(status) in (2, 3) and status[0] in "uihpr" and
            status[1] in ("n", "c"))


def mark_package_helper_failed(transaction_id: str, pending_path_value: str,
                               exit_code: int) -> None:
    if not transaction_id or not pending_path_value:
        return
    path = Path(pending_path_value)
    pending = read_json(path, {})
    if (not isinstance(pending, dict) or
            str(pending.get("transaction_id") or "") != transaction_id):
        return
    pending["helper_failed"] = True
    pending["helper_exit_code"] = exit_code
    pending["helper_failed_at"] = now_text()
    write_json(path, pending)


def pending_package_state_unchanged(pending: dict[str, Any], status: str,
                                    version: str) -> bool:
    previously_installed = pending.get("previously_installed")
    if not isinstance(previously_installed, bool):
        return False
    if previously_installed:
        previous_version = pending.get("previous_version")
        if not isinstance(previous_version, str) or not previous_version:
            return False
        return package_dpkg_status_is_installed(status) and version == previous_version
    return package_dpkg_status_is_absent(status)


def pending_action_requires_state_change(pending: dict[str, Any]) -> bool:
    return pending.get("action") in ("install", "reinstall", "upgrade", "uninstall")


def discard_failed_job_if_state_unchanged(transaction_id: str,
                                          pending_path_value: str) -> bool:
    if not transaction_id or not pending_path_value:
        return False
    path = Path(pending_path_value)
    pending = read_json(path, {})
    if (not isinstance(pending, dict) or
            str(pending.get("transaction_id") or "") != transaction_id):
        return False
    status, version = package_dpkg_status(str(pending.get("package") or ""))
    unchanged = pending_package_state_unchanged(pending, status, version)
    if unchanged:
        path.unlink(missing_ok=True)
    return unchanged


def run_package_helper_command(args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    process = subprocess.Popen(
        args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        env=env, start_new_session=True,
    )
    def terminate_group(sig: int) -> None:
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            pass

    def forward_signal(signum, _frame) -> None:
        terminate_group(signal.SIGTERM)
        raise InterruptedError(f"package command interrupted by signal {signum}")

    previous_handlers = {}
    if threading.current_thread() is threading.main_thread():
        for sig in (signal.SIGTERM, signal.SIGINT):
            previous_handlers[sig] = signal.signal(sig, forward_signal)
    try:
        output, _ = process.communicate(timeout=PACKAGE_COMMAND_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as exc:
        terminate_group(signal.SIGTERM)
        try:
            output, _ = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            terminate_group(signal.SIGKILL)
            output, _ = process.communicate()
        exc.stdout = output
        raise
    except InterruptedError:
        terminate_group(signal.SIGTERM)
        try:
            process.communicate(timeout=0.25)
        except subprocess.TimeoutExpired:
            terminate_group(signal.SIGKILL)
            process.communicate()
        raise
    except BaseException:
        terminate_group(signal.SIGTERM)
        try:
            process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            terminate_group(signal.SIGKILL)
            process.communicate()
        raise
    finally:
        for sig, previous in previous_handlers.items():
            signal.signal(sig, previous)
    return subprocess.CompletedProcess(args, process.returncode, output, "")


def repair_desktop_as_root(desktop_value: str, exec_values: list[str]) -> str:
    if not desktop_value:
        return ""
    desktop = Path(desktop_value)
    allowed_root = Path("/usr/share/APPLaunch/applications").resolve()
    try:
        resolved = desktop.resolve(strict=True)
    except OSError as exc:
        raise RuntimeError(f"desktop entry unavailable: {exc}") from exc
    if resolved.parent != allowed_root or resolved.suffix != ".desktop":
        raise RuntimeError("desktop entry is outside the APPLaunch applications directory")
    for exec_value in exec_values:
        binary = exec_binary_path(exec_value)
        if binary and Path(binary).is_absolute() and is_executable_file(binary):
            rewrite_desktop_exec(resolved, binary)
            return binary
    raise RuntimeError("no installed executable found for desktop entry")


def mark_package_helper_complete(transaction_id: str, pending_path_value: str = "") -> None:
    if not transaction_id:
        return
    path = Path(pending_path_value) if pending_path_value else pending_package_path()
    data = read_json(path, {})
    pending = data if isinstance(data, dict) else {}
    if str(pending.get("transaction_id") or "") != transaction_id:
        raise RuntimeError("package transaction changed while helper was running")
    pending["helper_completed"] = True
    pending["helper_completed_at"] = now_text()
    write_json(path, pending)


@package_transaction_locked
def package_helper(action: str, value: str, reinstall: bool = False,
                   desktop: str = "", exec_values: Optional[list[str]] = None,
                   transaction_id: str = "", pending_path_value: str = "") -> int:
    if os.geteuid() != 0:
        emit("ERROR", "package helper requires root")
        return 1
    command_started = False
    try:
        if action == "install":
            # The downloaded artifact is already verified. Install that one
            # package directly so an unrelated half-configured package cannot
            # pull this transaction into its postinst through apt repair.
            if shutil.which("dpkg") and deb_dependencies_satisfied(value):
                args = ["dpkg", "--install", value]
            elif shutil.which("apt-get"):
                audit = dpkg_audit_detail()
                if audit:
                    raise RuntimeError(
                        "cannot resolve package dependencies while dpkg has unrelated "
                        f"unfinished packages: {audit}"
                    )
                args = ["apt-get", "-y"]
                if reinstall:
                    args.append("--reinstall")
                args += ["install", value]
            else:
                raise RuntimeError("apt-get or dpkg is required to install deb packages")
        elif action == "uninstall":
            # Removing one package must not configure every unrelated package.
            # A half-configured package with a slow postinst otherwise blocks
            # an independent uninstall in repair_dpkg_state()/apt-get.
            if shutil.which("dpkg"):
                args = ["dpkg", "--remove", value]
            elif shutil.which("apt-get"):
                args = ["apt-get", "-y", "remove", value]
            else:
                raise RuntimeError("apt-get or dpkg is required to uninstall deb packages")
        else:
            raise RuntimeError("unsupported package helper action")
        env = os.environ.copy()
        env["DEBIAN_FRONTEND"] = "noninteractive"
        try:
            command_started = True
            result = run_package_helper_command(args, env)
        except subprocess.TimeoutExpired as exc:
            detail = compact_error(exc.stdout or exc.stderr or "")
            mark_package_helper_failed(transaction_id, pending_path_value, 124)
            if discard_failed_job_if_state_unchanged(transaction_id, pending_path_value):
                emit("WARNING", "failed package command left installed package state unchanged")
            emit("ERROR", "package command timed out" + (f": {detail}" if detail else ""), 124)
            return 124
        if result.stdout:
            sys.stdout.write(result.stdout)
        if result.returncode != 0:
            detail = compact_error(result.stdout or "")
            mark_package_helper_failed(transaction_id, pending_path_value, result.returncode)
            if discard_failed_job_if_state_unchanged(transaction_id, pending_path_value):
                emit("WARNING", "failed package command left installed package state unchanged")
            emit("ERROR", detail or "package command failed", result.returncode)
            return result.returncode
        if action == "install" and desktop:
            try:
                repaired_exec = repair_desktop_as_root(desktop, exec_values or [])
                if repaired_exec:
                    emit("REPAIRED_EXEC", repaired_exec)
            except Exception as exc:
                emit("WARNING", f"desktop repair failed: {exc}")
        mark_package_helper_complete(transaction_id, pending_path_value)
        return 0
    except Exception as exc:
        if not command_started:
            discard_unstarted_package_job(transaction_id, pending_path_value)
        else:
            mark_package_helper_failed(transaction_id, pending_path_value, 1)
            if discard_failed_job_if_state_unchanged(transaction_id, pending_path_value):
                emit("WARNING", "failed package command left installed package state unchanged")
        emit("ERROR", str(exc), 1)
        return 1


def repair_dpkg_state() -> None:
    if not shutil.which("dpkg"):
        return
    audit = subprocess.run(
        ["dpkg", "--audit"],
        check=False,
        capture_output=True,
        text=True,
        timeout=60,
    )
    if (audit.stdout or audit.stderr).strip():
        emit("PROGRESS", "apt", 0, 0, -1, "Repairing package database")
        run_package_command(["dpkg", "--configure", "-a"])


def package_files(package: str) -> list[str]:
    if not package:
        raise RuntimeError("deb package name missing")
    if not shutil.which("dpkg-query"):
        raise RuntimeError("dpkg-query is required to inspect installed package files")
    result = subprocess.run(
        ["dpkg-query", "-L", package],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(command_error(result))
    return [line for line in result.stdout.splitlines() if line.startswith("/")]


def pending_package_job() -> dict[str, Any]:
    data = read_json(pending_package_path(), {})
    return data if isinstance(data, dict) else {}


def _write_pending_package_job(action: str, app: dict[str, Any], package: str,
                               deb_path: str = "", expected_package_version: str = "") -> None:
    existing = pending_package_job()
    if existing:
        raise RuntimeError(
            f"another package transaction is pending: {existing.get('app_id') or 'unknown'}"
        )
    installed, previous_version = package_state(package)
    write_json(pending_package_path(), {
        "schema_version": 2,
        "transaction_id": uuid.uuid4().hex,
        "action": action,
        "app_id": app_key(app),
        "package": package,
        "expected_version": str(app.get("version") or ""),
        "expected_package_version": expected_package_version,
        "previously_installed": installed,
        "previous_version": previous_version,
        "deb_path": deb_path,
        "helper_completed": False,
        "app_snapshot": {
            "id": app_key(app),
            "share_code": str(app.get("share_code") or ""),
            "title": app.get("title") or app_key(app),
            "version": str(app.get("version") or ""),
            "download": dict(app.get("download") or {}),
            "applaunch": dict(app.get("applaunch") or {}),
        },
        "created_at": now_text(),
    })


@package_transaction_locked
def write_pending_package_job(action: str, app: dict[str, Any], package: str,
                              deb_path: str = "", expected_package_version: str = "") -> None:
    _write_pending_package_job(action, app, package, deb_path, expected_package_version)


def clear_pending_package_job() -> None:
    pending_package_path().unlink(missing_ok=True)


def package_state(package: str) -> tuple[bool, str]:
    if not shutil.which("dpkg-query"):
        raise RuntimeError("dpkg-query is required to verify package state")
    invalidate_dpkg_status_cache("verify")
    return package_installed(package), package_version(package)


def update_installed_record(app: dict[str, Any], package: str, version: str,
                            deb_path: str) -> list[str]:
    files = package_files(package)
    repaired_exec = ""
    try:
        repaired_exec = repair_applaunch_desktop(app, files)
        if not repaired_exec:
            emit("WARNING", "desktop entry was not repaired")
    except Exception as exc:
        emit("WARNING", f"desktop repair failed: {exc}")
    records = installed_records()
    records[app_key(app)] = {
        "installed_at": now_text(),
        "title": localized_text(app, "title", resolve_locale()) or app.get("title"),
        "version": version,
        "package": package,
        "deb_path": deb_path,
        "exec": repaired_exec or applaunch_exec(app),
        "files": files,
    }
    write_json(installed_path(), records)
    return files


@package_transaction_locked
def reconcile_pending_package_job(emit_result: bool = False) -> bool:
    pending = pending_package_job()
    if not pending:
        return True
    action = str(pending.get("action") or "")
    app_id = str(pending.get("app_id") or "")
    package = str(pending.get("package") or "")
    if action not in ("install", "reinstall", "upgrade", "uninstall") or not app_id or not package:
        emit("WARNING", "pending package transaction is invalid and was retained")
        return False
    app = pending.get("app_snapshot") if isinstance(pending.get("app_snapshot"), dict) else None
    if not app:
        app = find_app(app_id)
    if not app:
        emit("WARNING", f"pending package transaction cannot find app: {app_id}")
        return False
    try:
        installed, version = package_state(package)
        helper_effect_verified = bool(pending.get("helper_completed"))
        if int(pending.get("schema_version") or 1) >= 2 and not pending.get("helper_completed"):
            expected = str(pending.get("expected_package_version") or "")
            previously_installed = bool(pending.get("previously_installed"))
            previous_version = str(pending.get("previous_version") or "")
            dpkg_status, dpkg_version = package_dpkg_status(package)
            if (pending_action_requires_state_change(pending) and
                    pending_package_state_unchanged(pending, dpkg_status, dpkg_version)):
                clear_pending_package_job()
                emit("WARNING", f"interrupted {action} left installed package state unchanged; "
                     "cleared stale transaction")
                return True
            state_proves_applied = (
                action == "uninstall" and previously_installed and
                package_dpkg_status_is_absent(dpkg_status)
            ) or (
                action in ("install", "reinstall", "upgrade") and installed and expected and
                version == expected and
                (not previously_installed or previous_version != version)
            )
            if not state_proves_applied:
                emit("WARNING", f"{action} outcome is not known; transaction was retained")
                return False
            helper_effect_verified = True
            emit("WARNING", f"recovered {action} from verified package state")
        if action == "uninstall":
            if installed:
                update_installed_record(app, package, version,
                                        str(pending.get("deb_path") or ""))
                emit("WARNING", f"uninstall was not applied; restored installed record for {package}")
                if int(pending.get("schema_version") or 1) >= 2:
                    return False
            else:
                records = installed_records()
                records.pop(app_key(app), None)
                records.pop(app_id, None)
                write_json(installed_path(), records)
        else:
            if not installed or not version:
                if int(pending.get("schema_version") or 1) >= 2:
                    emit("WARNING", f"{action} package state verification failed; transaction was retained")
                    return False
                records = installed_records()
                records.pop(app_key(app), None)
                records.pop(app_id, None)
                write_json(installed_path(), records)
                emit("WARNING", f"{action} was not applied; cleared stale record for {package}")
            elif helper_effect_verified or (
                int(pending.get("schema_version") or 1) < 2 and
                action == "install" and not pending.get("previously_installed")
            ):
                expected = str(pending.get("expected_package_version") or "")
                if int(pending.get("schema_version") or 1) >= 2 and (
                        not expected or version != expected):
                    emit("WARNING", f"{action} did not install the expected package version; transaction was retained")
                    return False
                update_installed_record(app, package, version,
                                        str(pending.get("deb_path") or ""))
            else:
                emit("WARNING", f"{action} outcome is not known; transaction was retained")
                return False
        record_completed_package(str(pending.get("transaction_id") or ""), action,
                                 app_id, package, version)
        clear_pending_package_job()
        if emit_result:
            emit("PACKAGE_RESULT", action, app_id, package, version)
        return True
    except Exception as exc:
        emit("WARNING", f"pending package reconciliation failed: {exc}")
        return False


def uninstall(app_id: str) -> int:
    return run_legacy_package_job("uninstall", app_id)


def install(app_id: str, reinstall: bool = False, upgrade: bool = False) -> int:
    return run_legacy_package_job("upgrade" if upgrade else ("reinstall" if reinstall else "install"), app_id)


@package_transaction_locked
def prepare_package_job(action: str, app_id: str) -> int:
    PACKAGE_CANCEL_EVENT.clear()
    app = find_app(app_id)
    if not app:
        emit("ERROR", "app not found", app_id)
        return 1
    try:
        existing = pending_package_job()
        if existing:
            same_app = str(existing.get("app_id") or "") in (app_id, app_key(app))
            recovery_uninstall = (
                action == "uninstall" and same_app and existing.get("helper_failed") and
                str(existing.get("action") or "") in ("install", "reinstall", "upgrade")
            )
            if recovery_uninstall:
                clear_pending_package_job()
                existing = {}
            elif (str(existing.get("action") or "") != action or not same_app or
                  existing.get("helper_completed")):
                raise RuntimeError(
                    f"another package transaction is pending: {existing.get('app_id') or 'unknown'}"
                )
        if existing:
            package = str(existing.get("package") or "")
            transaction_id = str(existing.get("transaction_id") or "")
            if action == "uninstall":
                emit("PACKAGE_JOB", "uninstall", package, "0", "", transaction_id,
                     str(pending_package_path()))
            else:
                if not existing.get("expected_package_version"):
                    deb_path = Path(str(existing.get("deb_path") or ""))
                    if not deb_path.is_file():
                        snapshot = existing.get("app_snapshot")
                        if not isinstance(snapshot, dict) or not snapshot.get("download"):
                            raise RuntimeError("pending package cache is missing; restart the transaction")
                        deb_path = download_deb(snapshot)
                        existing["deb_path"] = str(deb_path)
                    if deb_file_package(deb_path) != package:
                        raise RuntimeError("cached deb package does not match pending transaction")
                    existing["expected_package_version"] = deb_file_version(deb_path)
                    write_json(pending_package_path(), existing)
                candidates = candidate_execs(app, [])
                candidates += [f"/usr/lib/{package}/{package}_zero_device",
                               f"/usr/bin/{package}", f"/usr/lib/{package}/{package}"]
                emit("PACKAGE_JOB", "install", str(existing.get("deb_path") or ""),
                     "1" if action == "reinstall" else "0", str(desktop_path_for(app)),
                     transaction_id, str(pending_package_path()), *dict.fromkeys(candidates))
            return 0
        if action == "uninstall":
            records = installed_records()
            record = records.get(app_key(app), records.get(app_id, {}))
            package = str(record.get("package") or "") if isinstance(record, dict) else ""
            if not package:
                package = deb_package_name(app)
            if not package:
                raise RuntimeError("deb package name missing")
            _write_pending_package_job(action, app, package)
            transaction_id = str(pending_package_job().get("transaction_id") or "")
            emit("PACKAGE_JOB", "uninstall", package, "0", "", transaction_id,
                 str(pending_package_path()))
            return 0
        if action not in ("install", "reinstall", "upgrade"):
            raise RuntimeError("unsupported package action")
        if not is_installable(app):
            raise RuntimeError("only approved apps can be installed")
        deb_path = download_deb(app)
        expected_package_version = deb_file_version(deb_path)
        package = deb_package_name(app)
        if not package:
            raise RuntimeError("deb package name missing")
        if deb_file_package(deb_path) != package:
            raise RuntimeError("downloaded deb package name does not match registry metadata")
        candidates = candidate_execs(app, [])
        candidates += [f"/usr/lib/{package}/{package}_zero_device",
                       f"/usr/bin/{package}", f"/usr/lib/{package}/{package}"]
        _write_pending_package_job(action, app, package, str(deb_path), expected_package_version)
        transaction_id = str(pending_package_job().get("transaction_id") or "")
        emit("PACKAGE_JOB", "install", str(deb_path), "1" if action == "reinstall" else "0",
             str(desktop_path_for(app)), transaction_id, str(pending_package_path()),
             *dict.fromkeys(candidates))
        return 0
    except Exception as exc:
        emit("ERROR", str(exc), 1)
        return 1


@package_transaction_locked
def finalize_package_job(action: str, app_id: str, transaction_id: str = "") -> int:
    app = find_app(app_id)
    try:
        pending = pending_package_job()
        if not pending:
            completed = completed_package_records().get(transaction_id, {})
            completed_key = app_key(app) if app else app_id
            if (isinstance(completed, dict) and transaction_id and
                    str(completed.get("transaction_id") or "") == transaction_id and
                    str(completed.get("action") or "") == action and
                    str(completed.get("app_id") or "") in (app_id, completed_key)):
                emit("PACKAGE_RESULT", action, completed_key, completed.get("package", ""),
                     completed.get("version", ""))
                return 0
            raise RuntimeError("no pending package transaction")
        if not app and isinstance(pending.get("app_snapshot"), dict):
            app = pending["app_snapshot"]
        if not app:
            raise RuntimeError(f"app not found: {app_id}")
        key = app_key(app)
        if not transaction_id or str(pending.get("transaction_id") or "") != transaction_id:
            raise RuntimeError("pending package transaction id does not match finalize request")
        if int(pending.get("schema_version") or 1) >= 2 and not pending.get("helper_completed"):
            raise RuntimeError("package helper completion was not recorded")
        package = str(pending.get("package") or deb_package_name(app))
        if pending and (str(pending.get("action") or "") != action or
                        str(pending.get("app_id") or "") not in (app_id, key)):
            raise RuntimeError("pending package transaction does not match finalize request")
        installed, actual_version = package_state(package)
        if action == "uninstall":
            if installed:
                raise RuntimeError(f"package is still installed after uninstall: {package}")
            records = installed_records()
            records.pop(key, None)
            records.pop(app_id, None)
            write_json(installed_path(), records)
            record_completed_package(transaction_id, action, app_id, package, "")
            clear_pending_package_job()
            emit("PROGRESS", "uninstall", 1, 1, 100, "Remove complete")
            emit("UNINSTALLED", app_id)
            emit("PACKAGE_RESULT", action, key, package, "")
            return 0
        if action not in ("install", "reinstall", "upgrade"):
            raise RuntimeError("unsupported package action")
        if not installed or not actual_version:
            raise RuntimeError(f"package is not installed after {action}: {package}")
        expected = str(pending.get("expected_package_version") or "")
        if int(pending.get("schema_version") or 1) >= 2 and (
                not expected or actual_version != expected):
            raise RuntimeError(f"{action} did not install the expected package version")
        deb_path = str(pending.get("deb_path") or deb_cache_path(download_url(app)))
        update_installed_record(app, package, actual_version, deb_path)
        record_completed_package(transaction_id, action, key, package, actual_version)
        clear_pending_package_job()
        stage = "upgrade" if action == "upgrade" else "install"
        emit("PROGRESS", stage, 1, 1, 100,
             "Upgrade complete" if action == "upgrade" else "Install complete")
        emit("UPGRADED" if action == "upgrade" else "INSTALLED", key,
             localized_text(app, "title", resolve_locale()) or key)
        emit("PACKAGE_RESULT", action, key, package, actual_version)
        return 0
    except Exception as exc:
        emit("ERROR", str(exc), 1)
        return 1


def run_legacy_package_job(action: str, app_id: str) -> int:
    if os.geteuid() != 0:
        emit("ERROR", "legacy package operations require root", 1)
        return 1
    if prepare_package_job(action, app_id) != 0:
        return 1
    pending = pending_package_job()
    package = str(pending.get("package") or "")
    value = package if action == "uninstall" else str(pending.get("deb_path") or "")
    app = find_app(app_id)
    desktop = str(desktop_path_for(app)) if app and action != "uninstall" else ""
    candidates = candidate_execs(app, []) if app and action != "uninstall" else []
    helper_action = "uninstall" if action == "uninstall" else "install"
    transaction_id = str(pending.get("transaction_id") or "")
    if package_helper(helper_action, value, action == "reinstall", desktop, candidates,
                      transaction_id) != 0:
        return 1
    return finalize_package_job(action, app_id, transaction_id)


def add_registry(url: str, name: str = "") -> int:
    config = load_config()
    normalized = normalize_registry_url(url)
    if is_builtin_registry_url(normalized):
        emit("ERROR", "use region selection for built-in registries", normalized)
        return 1
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
    if is_builtin_registry_url(normalized):
        emit("ERROR", "registry is managed by region selection", normalized)
        return 1
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
    if is_builtin_registry_url(old_normalized):
        emit("ERROR", "registry is managed by region selection", old_normalized)
        return 1
    if is_builtin_registry_url(new_normalized):
        emit("ERROR", "use region selection for built-in registries", new_normalized)
        return 1
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


def registry_config() -> int:
    config = load_config()
    emit(
        "CONFIG",
        config.get("region", "auto"),
        config.get("active_region", "default"),
        len(config.get("registries", [])),
    )
    for index, item in enumerate(config.get("registries", [])):
        if not isinstance(item, dict):
            continue
        emit(
            "CONFIG_REG",
            index,
            item.get("name") or registry_name_from_url(str(item.get("url", ""))),
            item.get("url", ""),
            "1" if item.get("enabled", True) else "0",
            "1" if item.get("builtin") else "0",
            item.get("region") or "",
        )
    return 0


def replace_registry_config(payload: str) -> int:
    try:
        incoming = json.loads(payload)
    except Exception as exc:
        emit("ERROR", f"invalid registry config: {compact_error(exc)}")
        return 1
    if not isinstance(incoming, dict):
        emit("ERROR", "invalid registry config: object required")
        return 1

    config: dict[str, Any] = {
        "region": normalize_region_mode(incoming.get("region", "auto")),
        "active_region": normalize_region(incoming.get("active_region", "default")),
        "registries": [],
    }
    registries = incoming.get("registries")
    if not isinstance(registries, list):
        registries = []
    for item in registries:
        if not isinstance(item, dict):
            continue
        url = normalize_registry_url(str(item.get("url", "")))
        if not url:
            continue
        entry: dict[str, Any] = {
            "name": item.get("name") or registry_name_from_url(url),
            "url": url,
            "enabled": enabled_value(item.get("enabled", True)),
        }
        if item.get("builtin") or is_builtin_registry_url(url):
            entry["builtin"] = True
            entry["region"] = normalize_region(item.get("region", config["active_region"]))
        elif item.get("region"):
            entry["region"] = str(item.get("region"))
        config["registries"].append(entry)

    config = config_with_region(config, config["region"])
    if config["region"] == "auto":
        config["active_region"] = normalize_region(incoming.get("active_region", config.get("active_region", "default")))
    else:
        config["active_region"] = normalize_region(config["region"])
    custom_registries = [
        item for item in config.get("registries", [])
        if isinstance(item, dict) and not item.get("builtin") and not is_builtin_registry_url(str(item.get("url", "")))
    ]
    config["registries"] = [region_registry(config["active_region"]), *custom_registries]
    save_config(config)
    emit("CONFIG", config.get("region", "auto"), config.get("active_region", "default"), len(config["registries"]))
    return 0


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", action="store_true")
    parser.add_argument("--summary-sync-if-empty", action="store_true")
    parser.add_argument("--registries", action="store_true")
    parser.add_argument("--regions", action="store_true")
    parser.add_argument("--set-region")
    parser.add_argument("--sync", action="store_true")
    parser.add_argument("--add-registry")
    parser.add_argument("--registry-name")
    parser.add_argument("--remove-registry")
    parser.add_argument("--enable-registry")
    parser.add_argument("--disable-registry")
    parser.add_argument("--edit-registry", nargs=2, metavar=("OLD_URL", "NEW_URL"))
    parser.add_argument("--registry-config", action="store_true")
    parser.add_argument("--replace-registry-config")
    parser.add_argument("--plan")
    parser.add_argument("--install")
    parser.add_argument("--reinstall")
    parser.add_argument("--upgrade")
    parser.add_argument("--uninstall")
    parser.add_argument("--prepare-package", nargs=2, metavar=("ACTION", "APP_ID"))
    parser.add_argument("--finalize-package", nargs=3, metavar=("ACTION", "APP_ID", "TRANSACTION_ID"))
    parser.add_argument("--package-helper", choices=("install", "uninstall"))
    parser.add_argument("--package-value")
    parser.add_argument("--package-reinstall", action="store_true")
    parser.add_argument("--package-desktop")
    parser.add_argument("--package-exec", action="append", default=[])
    parser.add_argument("--package-transaction")
    parser.add_argument("--package-pending-path")
    parser.add_argument("--serve", action="store_true")
    parser.add_argument("--port", type=int, default=8895)
    return parser.parse_args(argv)


def execute_args(args: argparse.Namespace) -> int:
    if args.summary:
        summary(sync_if_empty=args.summary_sync_if_empty)
        return 0
    if args.registries:
        registries()
        return 0
    if args.regions:
        regions()
        return 0
    if args.set_region:
        return set_region(args.set_region)
    if args.sync:
        try:
            records = sync_all()
        except SyncCancelled:
            emit("ERROR", "sync cancelled")
            return 1
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
    if args.registry_config:
        return registry_config()
    if args.replace_registry_config:
        return replace_registry_config(args.replace_registry_config)
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
    if args.prepare_package:
        return prepare_package_job(args.prepare_package[0], args.prepare_package[1])
    if args.finalize_package:
        return finalize_package_job(args.finalize_package[0], args.finalize_package[1],
                                    args.finalize_package[2])
    summary()
    return 0


def execute_reconciled_args(args: argparse.Namespace) -> int:
    reconcile_pending_package_job()
    return execute_args(args)


class AppStoreHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


class AppStoreRequestHandler(BaseHTTPRequestHandler):
    server_version = "CardputerZeroAppStore/0.1"

    def log_message(self, fmt: str, *args: Any) -> None:
        log_debug("http " + (fmt % args))

    def do_GET(self) -> None:
        if self.path == "/sync-status":
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                emit_sync_status()
            body = buffer.getvalue().encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/tab-separated-values; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path != "/health":
            self.send_error(404, "not found")
            return
        body = b"OK\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self) -> None:
        if self.path == "/cancel-package":
            PACKAGE_CANCEL_EVENT.set()
            raw = b"OK\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        if self.path == "/cancel-sync":
            request_sync_cancel()
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                emit_sync_status()
            raw = buffer.getvalue().encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/tab-separated-values; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        if self.path != "/run":
            self.send_error(404, "not found")
            return
        try:
            length = int(self.headers.get("Content-Length") or "0")
            body = self.rfile.read(length).decode("utf-8")
            fields = split_tsv_line(body.splitlines()[0] if body else "")
            argv = fields
            output, rc = run_service_command(argv)
            raw = output.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/tab-separated-values; charset=utf-8")
            self.send_header("X-AppStore-RC", str(rc))
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
        except Exception as exc:
            log_debug(f"http run failed error={compact_error(exc)}")
            output = f"ERROR\t{tsv_escape(str(exc))}\n".encode("utf-8")
            self.send_response(500)
            self.send_header("Content-Type", "text/tab-separated-values; charset=utf-8")
            self.send_header("X-AppStore-RC", "1")
            self.send_header("Content-Length", str(len(output)))
            self.end_headers()
            self.wfile.write(output)


def run_service_command(argv: list[str]) -> tuple[str, int]:
    with SERVICE_LOCK:
        old_argv = sys.argv[:]
        buffer = io.StringIO()
        rc = 1
        try:
            sys.argv = [old_argv[0], *argv]
            log_debug(f"http run argv={shlex.join(argv)}")
            args = parse_args(argv)
            with contextlib.redirect_stdout(buffer):
                rc = execute_reconciled_args(args)
        except SystemExit as exc:
            rc = int(exc.code or 0) if isinstance(exc.code, int) else 1
        except Exception as exc:
            rc = 1
            print("ERROR", str(exc), sep="\t", file=buffer)
            log_debug(f"http command exception argv={shlex.join(argv)} error={compact_error(exc)}")
        finally:
            sys.argv = old_argv
        return buffer.getvalue(), rc


def serve(port: int) -> int:
    ensure_dirs()
    address = ("127.0.0.1", int(port))
    log_debug(
        f"serve start host={address[0]} port={address[1]} app_root={app_root()} "
        f"cache_dir={cache_dir()} state_dir={state_dir()} config={config_path()} debug_log={DEBUG_LOG_PATH}"
    )
    server = AppStoreHTTPServer(address, AppStoreRequestHandler)
    try:
        server.serve_forever()
    finally:
        server.server_close()
    return 0


def main() -> int:
    args = parse_args()
    if args.package_helper:
        if not args.package_value:
            emit("ERROR", "package helper value missing")
            return 1
        if args.package_pending_path:
            os.environ["M5APPSTORE_STATE_DIR"] = str(Path(args.package_pending_path).parent)
        return package_helper(args.package_helper, args.package_value, args.package_reinstall,
                              args.package_desktop or "", args.package_exec,
                              args.package_transaction or "", args.package_pending_path or "")
    ensure_dirs()
    log_debug(
        "main "
        f"argv={shlex.join(sys.argv[1:])} app_root={app_root()} cache_dir={cache_dir()} "
        f"state_dir={state_dir()} config={config_path()} debug_log={DEBUG_LOG_PATH}"
    )
    if args.serve:
        return serve(args.port)
    return execute_reconciled_args(args)


if __name__ == "__main__":
    raise SystemExit(main())
