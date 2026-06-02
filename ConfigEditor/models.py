from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable


ConfigDict = dict[str, Any]
Predicate = Callable[[ConfigDict], bool]


@dataclass
class FieldDef:
    section: str
    group: str
    label: str
    kind: str
    path: tuple[str, ...] | None = None
    help_text: str = ""
    advanced: bool = False
    min_value: float | int | None = None
    max_value: float | int | None = None
    step: float = 1.0
    decimals: int = 2
    options: list[tuple[str, Any]] = field(default_factory=list)
    visible_if: Predicate | None = None
    getter: Callable[[ConfigDict], Any] | None = None
    setter: Callable[[ConfigDict, Any], None] | None = None


@dataclass
class AppMapping:
    label: str
    app_name: str | None
    config_path: str
