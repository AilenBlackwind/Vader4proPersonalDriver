from __future__ import annotations

import json
import os

from ConfigEditor.constants import CONFIG_DIR, DEFAULT_CONFIG_PATH, MASTER_CONFIG_PATH, ROOT_DIR
from ConfigEditor.models import AppMapping, ConfigDict, FieldDef, Predicate


def strip_json_comments(text: str) -> str:
    result: list[str] = []
    in_string = False
    escape = False
    i = 0

    while i < len(text):
        ch = text[i]

        if in_string:
            result.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            i += 1
            continue

        if ch == '"':
            in_string = True
            result.append(ch)
            i += 1
            continue

        if ch == "/" and i + 1 < len(text) and text[i + 1] == "/":
            while i < len(text) and text[i] != "\n":
                i += 1
            continue

        result.append(ch)
        i += 1

    return "".join(result)


def load_jsonc(path: str) -> ConfigDict:
    with open(path, "r", encoding="utf-8") as f:
        return json.loads(strip_json_comments(f.read()))


def _round_floats(obj: Any, ndigits: int = 9) -> Any:
    if isinstance(obj, float):
        return round(obj, ndigits)
    if isinstance(obj, dict):
        return {k: _round_floats(v, ndigits) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_round_floats(v, ndigits) for v in obj]
    return obj


def save_json(path: str, data: ConfigDict) -> None:
    with open(path, "w", encoding="utf-8") as f:
        json.dump(_round_floats(data), f, indent=2, ensure_ascii=False)
        f.write("\n")


def ensure_parent_dir(path: str) -> None:
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)


def get_path(data: ConfigDict, path: tuple[str, ...], default: Any = None) -> Any:
    cur: Any = data
    for part in path:
        if not isinstance(cur, dict) or part not in cur:
            return default
        cur = cur[part]
    return cur


def set_path(data: ConfigDict, path: tuple[str, ...], value: Any) -> None:
    cur: Any = data
    for part in path[:-1]:
        if part not in cur or not isinstance(cur[part], dict):
            cur[part] = {}
        cur = cur[part]
    cur[path[-1]] = value


def remove_path(data: ConfigDict, path: tuple[str, ...]) -> None:
    cur: Any = data
    parents: list[tuple[dict[str, Any], str]] = []
    for part in path[:-1]:
        if not isinstance(cur, dict) or part not in cur:
            return
        parents.append((cur, part))
        cur = cur[part]
    if isinstance(cur, dict):
        cur.pop(path[-1], None)
    for parent, key in reversed(parents):
        value = parent.get(key)
        if isinstance(value, dict) and not value:
            parent.pop(key, None)
        else:
            break


def choice_is(path: tuple[str, ...], expected: Any) -> Predicate:
    return lambda data: get_path(data, path) == expected


def bool_is(path: tuple[str, ...], expected: bool = True) -> Predicate:
    return lambda data: bool(get_path(data, path, False)) is expected


def group_predicates(*predicates: Predicate) -> Predicate:
    return lambda data: all(pred(data) for pred in predicates)


def profile_switch_getter(key: str) -> Callable[[ConfigDict], bool]:
    if key == "led":
        return lambda data: get_path(data, ("led_brightness",), 0.0) > 0.0
    if key == "accel":
        return lambda data: get_path(data, ("accel_rate",), 0.0) > 0.0
    if key == "smoothing":
        return lambda data: get_path(data, ("smooth_max_strength",), 0.0) > 0.0
    raise KeyError(key)


def profile_switch_setter(key: str) -> Callable[[ConfigDict, bool], None]:
    def setter(data: ConfigDict, enabled: bool) -> None:
        if key == "led":
            if enabled and get_path(data, ("led_brightness",), 0.0) <= 0.0:
                set_path(data, ("led_brightness",), 0.1)
            if not enabled:
                set_path(data, ("led_brightness",), 0.0)
            return

        if key == "accel":
            if enabled and get_path(data, ("accel_rate",), 0.0) <= 0.0:
                set_path(data, ("accel_rate",), 0.1)
            if not enabled:
                set_path(data, ("accel_rate",), 0.0)
            return

        if key == "smoothing":
            if enabled:
                if get_path(data, ("smooth_max_strength",), 0.0) <= 0.0:
                    set_path(data, ("smooth_speed_threshold",), 100.0)
                    set_path(data, ("smooth_max_strength",), 0.3)
                    set_path(data, ("smooth_lerp_speed",), 5.0)
            else:
                set_path(data, ("smooth_max_strength",), 0.0)
                set_path(data, ("smooth_lerp_speed",), 0.0)
            return

        raise KeyError(key)

    return setter


def field(
    section: str,
    group: str,
    label: str,
    kind: str,
    **kwargs: Any,
) -> FieldDef:
    return FieldDef(section=section, group=group, label=label, kind=kind, **kwargs)


def discover_config_files() -> list[str]:
    paths: set[str] = set()
    for candidate in (DEFAULT_CONFIG_PATH, os.path.join(CONFIG_DIR, "default.json")):
        if os.path.exists(candidate):
            paths.add(normalize_path(candidate))
    for root, _dirs, files in os.walk(CONFIG_DIR):
        for name in files:
            if name.lower().endswith(".json"):
                paths.add(normalize_path(os.path.join(root, name)))
    return sorted(paths, key=str.casefold)


def normalize_path(path: str) -> str:
    return os.path.normpath(os.path.abspath(path))


def display_path(path: str) -> str:
    absolute = normalize_path(path)
    try:
        relative = os.path.relpath(absolute, ROOT_DIR)
        if not relative.startswith(".."):
            return relative.replace("\\", "/")
    except ValueError:
        pass
    return absolute


def to_repo_relative(path: str) -> str:
    absolute = normalize_path(path)
    try:
        relative = os.path.relpath(absolute, ROOT_DIR)
        if not relative.startswith(".."):
            return relative.replace("\\", "/")
    except ValueError:
        pass
    return absolute


def load_master_config() -> ConfigDict:
    if not os.path.exists(MASTER_CONFIG_PATH):
        return {"apps": {}}
    try:
        data = load_jsonc(MASTER_CONFIG_PATH)
        if not isinstance(data, dict):
            return {"apps": {}}
        if "apps" not in data or not isinstance(data["apps"], dict):
            data["apps"] = {}
        return data
    except Exception:
        return {"apps": {}}


def resolve_config_path(path: str) -> str:
    if os.path.isabs(path):
        return normalize_path(path)
    return normalize_path(os.path.join(ROOT_DIR, path))


def build_app_targets(master_config: ConfigDict) -> list:
    raw = master_config.get("default")
    if raw and isinstance(raw, str):
        default_path = resolve_config_path(raw)
    else:
        default_path = DEFAULT_CONFIG_PATH
    items = [AppMapping("Default fallback", None, default_path)]
    apps = master_config.get("apps", {})
    if isinstance(apps, dict):
        for app_name in sorted(apps, key=str.casefold):
            config_path = resolve_config_path(str(apps[app_name]))
            items.append(AppMapping(f"{app_name} -> {display_path(config_path)}", app_name, config_path))
    return items
