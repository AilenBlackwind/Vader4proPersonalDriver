from __future__ import annotations

import ctypes
import os
import sys
import time
from typing import Any

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QCloseEvent
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDialog,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QTabWidget,
    QVBoxLayout,
    QWidget,
    QColorDialog,
)

from ConfigEditor.constants import (
    CONFIG_DIR,
    DEFAULT_CONFIG_PATH,
    HIDAPI_DLL_PATH,
    MASTER_CONFIG_PATH,
    PIPE_NAME,
    ROOT_DIR,
    VADER_PRODUCT_ID,
    VADER_VENDOR_ID,
    tr,
)
from ConfigEditor.helpers import (
    build_app_targets,
    discover_config_files,
    display_path,
    ensure_parent_dir,
    get_path,
    load_jsonc,
    load_master_config,
    normalize_path,
    remove_path,
    resolve_config_path,
    save_json,
    set_path,
    to_repo_relative,
)
from ConfigEditor.models import AppMapping, ConfigDict, FieldDef
from ConfigEditor.schema import SCHEMA
from ConfigEditor.widgets import AppMappingDialog, KeyCaptureButton

GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
ERROR_PIPE_BUSY = 231

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_create_file = _kernel32.CreateFileW
_create_file.argtypes = [
    ctypes.c_wchar_p,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_void_p,
]
_create_file.restype = ctypes.c_void_p

_write_file = _kernel32.WriteFile
_write_file.argtypes = [
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.c_void_p,
]
_write_file.restype = ctypes.c_int

_wait_named_pipe = _kernel32.WaitNamedPipeW
_wait_named_pipe.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32]
_wait_named_pipe.restype = ctypes.c_int

_close_handle = _kernel32.CloseHandle
_close_handle.argtypes = [ctypes.c_void_p]
_close_handle.restype = ctypes.c_int

_hidapi = ctypes.WinDLL(HIDAPI_DLL_PATH)
_hid_init = _hidapi.hid_init
_hid_init.argtypes = []
_hid_init.restype = ctypes.c_int

_hid_exit = _hidapi.hid_exit
_hid_exit.argtypes = []
_hid_exit.restype = ctypes.c_int

_hid_open = _hidapi.hid_open
_hid_open.argtypes = [ctypes.c_ushort, ctypes.c_ushort, ctypes.c_wchar_p]
_hid_open.restype = ctypes.c_void_p

_hid_write = _hidapi.hid_write
_hid_write.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t]
_hid_write.restype = ctypes.c_int

_hid_close = _hidapi.hid_close
_hid_close.argtypes = [ctypes.c_void_p]
_hid_close.restype = None


class ConfigEditorWindow(QMainWindow):
    def __init__(self, config_path: str):
        super().__init__()
        self.config_path = normalize_path(config_path)
        self.data: ConfigDict = {}
        self.dirty = False
        self.show_advanced = False
        self.widgets: list[tuple[FieldDef, QWidget, QLabel]] = []
        self.status_label = QLabel()
        self.advanced_toggle = QCheckBox(tr("Advanced"))
        self.section_groups: dict[str, QGroupBox] = {}
        self.group_boxes: dict[tuple[str, str], QGroupBox] = {}
        self.profile_tabs = QTabWidget()
        self.profile_led_buttons: dict[str, QPushButton] = {}
        self.config_files: list[str] = []
        self.master_config: ConfigDict = {}
        self.app_targets: list[AppMapping] = []
        self._loading = False

        self.setWindowTitle(tr("VaderDriver Config Editor"))
        self.resize(1180, 940)

        central = QWidget()
        self.setCentralWidget(central)
        root_layout = QVBoxLayout(central)

        target_bar = QHBoxLayout()
        target_bar.addWidget(QLabel(tr("Editing target")))
        self.target_combo = QComboBox()
        self.target_combo.currentIndexChanged.connect(self.on_target_changed)
        target_bar.addWidget(self.target_combo, 2)
        self.manage_target_button = QPushButton(tr("Add app override"))
        self.manage_target_button.clicked.connect(self.add_app_override)
        target_bar.addWidget(self.manage_target_button)
        self.remove_target_button = QPushButton(tr("Remove app override"))
        self.remove_target_button.clicked.connect(self.remove_app_override)
        target_bar.addWidget(self.remove_target_button)
        self.rescan_button = QPushButton(tr("Rescan"))
        self.rescan_button.clicked.connect(self.refresh_targets)
        target_bar.addWidget(self.rescan_button)
        root_layout.addLayout(target_bar)

        source_bar = QHBoxLayout()
        source_bar.addWidget(QLabel(tr("Config file")))
        self.config_combo = QComboBox()
        self.config_combo.currentIndexChanged.connect(self.on_config_file_changed)
        source_bar.addWidget(self.config_combo, 2)
        self.browse_button = QPushButton(tr("Browse"))
        self.browse_button.clicked.connect(self.browse_for_config)
        source_bar.addWidget(self.browse_button)
        self.open_folder_button = QPushButton(tr("Open in configs"))
        self.open_folder_button.clicked.connect(self.create_config_copy_in_configs)
        source_bar.addWidget(self.open_folder_button)
        self.set_default_button = QPushButton(tr("Set default"))
        self.set_default_button.clicked.connect(self.set_as_default_config)
        source_bar.addWidget(self.set_default_button)
        self.active_file_label = QLabel()
        self.active_file_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        source_bar.addWidget(self.active_file_label, 3)
        root_layout.addLayout(source_bar)

        top_bar = QHBoxLayout()
        self.advanced_toggle.toggled.connect(self.on_advanced_toggled)
        top_bar.addWidget(self.advanced_toggle)
        top_bar.addStretch(1)

        self.save_button = QPushButton(tr("Save"))
        self.save_button.clicked.connect(self.save_config)
        top_bar.addWidget(self.save_button)

        self.reload_button = QPushButton(tr("Reload"))
        self.reload_button.clicked.connect(self.reload_config)
        top_bar.addWidget(self.reload_button)

        self.test_led_button = QPushButton(tr("Test LED"))
        self.test_led_button.clicked.connect(self.test_led)
        top_bar.addWidget(self.test_led_button)

        self.test_rumble_button = QPushButton(tr("Test rumble"))
        self.test_rumble_button.clicked.connect(self.test_rumble)
        top_bar.addWidget(self.test_rumble_button)
        root_layout.addLayout(top_bar)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll_body = QWidget()
        scroll_layout = QVBoxLayout(scroll_body)
        scroll_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        scroll.setWidget(scroll_body)
        root_layout.addWidget(scroll)

        for name in ("General", "Mode Switching"):
            box = QGroupBox(tr(name))
            box_layout = QVBoxLayout(box)
            box_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
            self.section_groups[name] = box
            scroll_layout.addWidget(box)

        self.profile_tabs.addTab(self._make_profile_tab("Low Profile"), tr("Low Profile"))
        self.profile_tabs.addTab(self._make_profile_tab("High Profile"), tr("High Profile"))
        scroll_layout.addWidget(self.profile_tabs)

        self.status_label.setText(tr("Ready"))
        root_layout.addWidget(self.status_label)

        self._build_form()
        self.refresh_targets(select_path=self.config_path)
        self.reload_config()

    def _make_profile_tab(self, section: str) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        led_bar = QFrame()
        led_layout = QHBoxLayout(led_bar)
        led_layout.setContentsMargins(0, 0, 0, 0)
        led_layout.addWidget(QLabel(f"{tr(section)} LED"))
        led_layout.addStretch(1)
        color_button = QPushButton(tr("Pick color"))
        color_button.clicked.connect(lambda _=False, sec=section: self.pick_led_color(sec))
        self.profile_led_buttons[section] = color_button
        led_layout.addWidget(color_button)
        layout.addWidget(led_bar)

        container = QGroupBox(tr(section))
        container_layout = QVBoxLayout(container)
        container_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.section_groups[section] = container
        layout.addWidget(container)
        layout.addStretch(1)
        return tab

    def _build_form(self) -> None:
        for field_def in SCHEMA:
            row_widget = self._create_widget(field_def)
            label = QLabel(tr(field_def.label))
            if field_def.help_text:
                label.setToolTip(field_def.help_text)
                row_widget.setToolTip(field_def.help_text)

            form = self._ensure_group_form(field_def.section, field_def.group)
            form.addRow(label, row_widget)
            self.widgets.append((field_def, row_widget, label))

    def _ensure_group_form(self, section: str, group: str) -> QFormLayout:
        key = (section, group)
        if key in self.group_boxes:
            layout = self.group_boxes[key].layout()
            assert isinstance(layout, QFormLayout)
            return layout

        box = QGroupBox(tr(group))
        form = QFormLayout()
        form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
        box.setLayout(form)
        section_layout = self.section_groups[section].layout()
        assert isinstance(section_layout, QVBoxLayout)
        section_layout.addWidget(box)
        self.group_boxes[key] = box
        return form

    def _create_widget(self, field_def: FieldDef) -> QWidget:
        if field_def.kind == "bool":
            widget = QCheckBox()
            widget.toggled.connect(lambda value, fd=field_def: self._set_value(fd, value))
            return widget

        if field_def.kind == "choice":
            widget = QComboBox()
            for text, value in field_def.options:
                widget.addItem(text, value)
            widget.currentIndexChanged.connect(
                lambda _index, fd=field_def, w=widget: self._set_value(fd, w.currentData())
            )
            return widget

        if field_def.kind == "int":
            widget = QSpinBox()
            widget.setRange(int(field_def.min_value or -999999), int(field_def.max_value or 999999))
            widget.setSingleStep(int(field_def.step))
            widget.valueChanged.connect(lambda value, fd=field_def: self._set_value(fd, int(value)))
            return widget

        if field_def.kind == "float":
            widget = QDoubleSpinBox()
            widget.setRange(float(field_def.min_value or -999999.0), float(field_def.max_value or 999999.0))
            widget.setSingleStep(field_def.step)
            widget.setDecimals(field_def.decimals)
            widget.valueChanged.connect(lambda value, fd=field_def: self._set_value(fd, float(value)))
            return widget

        if field_def.kind == "hotkey":
            widget = KeyCaptureButton()
            widget.keyCaptured.connect(lambda code, fd=field_def: self._set_value(fd, code))
            return widget

        raise ValueError(f"Unsupported field kind: {field_def.kind}")

    def _get_value(self, field_def: FieldDef) -> Any:
        if field_def.getter:
            return field_def.getter(self.data)
        if field_def.path is None:
            return None
        return get_path(self.data, field_def.path)

    def _set_value(self, field_def: FieldDef, value: Any) -> None:
        if self._loading:
            return

        current_value = self._get_value(field_def)
        if current_value == value:
            return

        if field_def.setter:
            field_def.setter(self.data, value)
        elif field_def.path is not None:
            set_path(self.data, field_def.path, value)
        self._mark_dirty()
        self.refresh_visibility()
        self.refresh_led_buttons()

    def _set_widget_value(self, widget: QWidget, value: Any) -> None:
        widget.blockSignals(True)
        try:
            if isinstance(widget, QCheckBox):
                widget.setChecked(bool(value))
            elif isinstance(widget, QComboBox):
                for index in range(widget.count()):
                    if widget.itemData(index) == value:
                        widget.setCurrentIndex(index)
                        break
            elif isinstance(widget, QSpinBox):
                widget.setValue(int(value))
            elif isinstance(widget, QDoubleSpinBox):
                widget.setValue(float(value))
            elif isinstance(widget, KeyCaptureButton):
                widget.set_key_code(int(value))
        finally:
            widget.blockSignals(False)

    def refresh_targets(self, select_path: str | None = None) -> None:
        self.config_files = discover_config_files()
        self.master_config = load_master_config()
        self.app_targets = build_app_targets(self.master_config)

        self._rebuild_target_combo(select_path or self.config_path)
        self._rebuild_config_combo(select_path or self.config_path)
        self.refresh_target_controls()

    def _rebuild_target_combo(self, select_path: str) -> None:
        self.target_combo.blockSignals(True)
        self.target_combo.clear()
        selected_index = 0

        for index, item in enumerate(self.app_targets):
            self.target_combo.addItem(item.label, item)
            if normalize_path(item.config_path) == normalize_path(select_path):
                selected_index = index

        self.target_combo.setCurrentIndex(selected_index)
        self.target_combo.blockSignals(False)

    def _rebuild_config_combo(self, select_path: str) -> None:
        desired = normalize_path(select_path)
        if desired not in self.config_files:
            self.config_files.append(desired)
            self.config_files.sort(key=str.casefold)

        self.config_combo.blockSignals(True)
        self.config_combo.clear()
        selected_index = 0
        for index, path in enumerate(self.config_files):
            self.config_combo.addItem(display_path(path), path)
            if normalize_path(path) == desired:
                selected_index = index
        self.config_combo.setCurrentIndex(selected_index)
        self.config_combo.blockSignals(False)

    def refresh_target_controls(self) -> None:
        item = self.current_target()
        is_app_override = bool(item and item.app_name)
        self.remove_target_button.setEnabled(is_app_override)
        self.active_file_label.setText(display_path(self.config_path))
        self.refresh_led_buttons()
        self._update_status("Ready" if not self.dirty else "Unsaved changes")

    def current_target(self) -> AppMapping | None:
        data = self.target_combo.currentData()
        return data if isinstance(data, AppMapping) else None

    def on_target_changed(self, _index: int) -> None:
        if self._loading:
            return
        item = self.current_target()
        if item is None:
            return
        if normalize_path(item.config_path) == normalize_path(self.config_path):
            self.refresh_target_controls()
            return
        if not self._confirm_discard_if_needed("Switch target without saving?"):
            self._rebuild_target_combo(self.config_path)
            return
        self.load_config_path(item.config_path)

    def on_config_file_changed(self, _index: int) -> None:
        if self._loading:
            return
        selected_path = self.config_combo.currentData()
        if not isinstance(selected_path, str):
            return
        if normalize_path(selected_path) == normalize_path(self.config_path):
            return
        if not self._confirm_discard_if_needed("Switch config file without saving?"):
            self._rebuild_config_combo(self.config_path)
            return
        self.load_config_path(selected_path)

    def set_as_default_config(self) -> None:
        master = load_master_config()
        master["default"] = to_repo_relative(self.config_path)
        save_json(MASTER_CONFIG_PATH, master)
        self.master_config = master
        self.refresh_targets(select_path=self.config_path)
        self._update_status("Default config updated")

    def load_config_path(self, path: str) -> None:
        self.config_path = normalize_path(path)
        self.refresh_targets(select_path=self.config_path)
        self.reload_config()

    def reload_config(self) -> None:
        if not self._loading and not self._confirm_discard_if_needed("Reload and lose unsaved changes?"):
            return
        try:
            self._loading = True
            self.data = load_jsonc(self.config_path)
            self.dirty = False
            for field_def, widget, _label in self.widgets:
                value = self._get_value(field_def)
                if value is not None:
                    self._set_widget_value(widget, value)
            self.refresh_visibility()
            self.refresh_led_buttons()
            self._update_status("Config reloaded")
        except Exception as exc:
            QMessageBox.critical(self, "Reload failed", str(exc))
        finally:
            self._loading = False

    def save_config(self) -> bool:
        try:
            ensure_parent_dir(self.config_path)
            save_json(self.config_path, self.data)
            self.dirty = False
            self.refresh_targets(select_path=self.config_path)
            self._update_status("Config saved")
            return True
        except Exception as exc:
            QMessageBox.critical(self, "Save failed", str(exc))
            return False

    def refresh_visibility(self) -> None:
        visible_counts: dict[str, int] = {name: 0 for name in self.section_groups}
        group_visible_counts: dict[tuple[str, str], int] = {key: 0 for key in self.group_boxes}

        for field_def, widget, label in self.widgets:
            visible = True
            if field_def.advanced and not self.show_advanced:
                visible = False
            if visible and field_def.visible_if:
                visible = field_def.visible_if(self.data)

            label.setVisible(visible)
            widget.setVisible(visible)
            if visible:
                visible_counts[field_def.section] += 1
                group_visible_counts[(field_def.section, field_def.group)] += 1

        for key, box in self.group_boxes.items():
            box.setVisible(group_visible_counts.get(key, 0) > 0)

        for name, group in self.section_groups.items():
            group.setVisible(visible_counts.get(name, 0) > 0)

    def refresh_led_buttons(self) -> None:
        for section, button in self.profile_led_buttons.items():
            prefix = ("low_profile",) if section == "Low Profile" else ("high_profile",)
            profile = get_path(self.data, prefix, {})
            enabled = self._profile_switch_getter("led")(profile)
            r = int(get_path(profile, ("led_r",), 0))
            g = int(get_path(profile, ("led_g",), 0))
            b = int(get_path(profile, ("led_b",), 0))
            brightness = float(get_path(profile, ("led_brightness",), 0.0))
            button.setEnabled(enabled)
            preview = QColor(r, g, b).name()
            button.setStyleSheet(
                f"background-color: {preview}; color: {'#000000' if (r + g + b) > 382 else '#ffffff'};"
            )
            button.setText(f"{tr('Pick color')} ({brightness:.2f})" if enabled else tr("Pick color"))

    def _profile_switch_getter(self, key: str):
        if key == "led":
            return lambda data: get_path(data, ("led_brightness",), 0.0) > 0.0
        raise KeyError(key)

    def pick_led_color(self, section: str) -> None:
        prefix = ("low_profile",) if section == "Low Profile" else ("high_profile",)
        profile = get_path(self.data, prefix, {})
        current = QColor(
            int(get_path(profile, ("led_r",), 0)),
            int(get_path(profile, ("led_g",), 0)),
            int(get_path(profile, ("led_b",), 0)),
        )
        color = QColorDialog.getColor(current, self, f"Choose {section} LED color")
        if not color.isValid():
            return
        set_path(self.data, prefix + ("led_r",), color.red())
        set_path(self.data, prefix + ("led_g",), color.green())
        set_path(self.data, prefix + ("led_b",), color.blue())
        for field_def, widget, _label in self.widgets:
            if field_def.path in {
                prefix + ("led_r",),
                prefix + ("led_g",),
                prefix + ("led_b",),
            }:
                value = self._get_value(field_def)
                if value is not None:
                    self._set_widget_value(widget, value)
        self._mark_dirty()
        self.refresh_led_buttons()

    def _current_profile_prefix(self) -> tuple[str, ...]:
        return ("high_profile",) if self.profile_tabs.currentIndex() == 1 else ("low_profile",)

    def _current_profile_name(self) -> str:
        return "High Profile" if self.profile_tabs.currentIndex() == 1 else "Low Profile"

    def browse_for_config(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Choose config file",
            ROOT_DIR,
            "JSON files (*.json);;All files (*.*)",
        )
        if not path:
            return
        if not self._confirm_discard_if_needed("Open another config without saving?"):
            return
        self.load_config_path(path)

    def create_config_copy_in_configs(self) -> None:
        target, _ = QFileDialog.getSaveFileName(
            self,
            "Save config copy",
            os.path.join(CONFIG_DIR, "new_profile.json"),
            "JSON files (*.json)",
        )
        if not target:
            return
        target = normalize_path(target)
        if os.path.exists(target):
            reply = QMessageBox.question(
                self,
                "Overwrite config",
                f"{display_path(target)} already exists. Overwrite it?",
            )
            if reply != QMessageBox.StandardButton.Yes:
                return
        ensure_parent_dir(target)
        save_json(target, self.data)
        self.config_path = target
        self.dirty = False
        self.refresh_targets(select_path=target)
        self._update_status("Saved as new config")

    def add_app_override(self) -> None:
        config_choices = list(self.config_files)
        if self.config_path not in config_choices:
            config_choices.append(self.config_path)
        dialog = AppMappingDialog(self, sorted(config_choices, key=str.casefold))
        dialog.app_name_edit.setText("")
        current_target = self.current_target()
        if current_target is not None:
            index = dialog.config_combo.findData(self.config_path)
            if index >= 0:
                dialog.config_combo.setCurrentIndex(index)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        app_name, config_path = dialog.get_values()
        if not app_name:
            QMessageBox.warning(self, "App override", "Executable name is required.")
            return
        apps = self.master_config.setdefault("apps", {})
        apps[app_name] = to_repo_relative(config_path)
        save_json(MASTER_CONFIG_PATH, self.master_config)
        self.refresh_targets(select_path=config_path)
        match_index = self.target_combo.findText(f"{app_name} -> {display_path(config_path)}")
        if match_index >= 0:
            self.target_combo.setCurrentIndex(match_index)
        self._update_status("App override added")

    def remove_app_override(self) -> None:
        item = self.current_target()
        if item is None or not item.app_name:
            return
        reply = QMessageBox.question(
            self,
            "Remove app override",
            f"Remove mapping for {item.app_name}?",
        )
        if reply != QMessageBox.StandardButton.Yes:
            return
        apps = self.master_config.setdefault("apps", {})
        apps.pop(item.app_name, None)
        save_json(MASTER_CONFIG_PATH, self.master_config)
        raw = self.master_config.get("default")
        fallback = resolve_config_path(raw) if (raw and isinstance(raw, str)) else DEFAULT_CONFIG_PATH
        self.refresh_targets(select_path=fallback)
        self.load_config_path(fallback)
        self._update_status("App override removed")

    def test_rumble(self) -> None:
        try:
            self._send_rumble_test()
            self._update_status("Rumble test sent")
        except Exception as exc:
            QMessageBox.warning(self, "Rumble test failed", str(exc))

    def test_led(self) -> None:
        try:
            prefix = self._current_profile_prefix()
            profile = get_path(self.data, prefix, {})
            enabled = self._profile_switch_getter("led")(profile)
            if not enabled:
                QMessageBox.information(
                    self,
                    tr("Test LED"),
                    "LED у текущего профиля выключен. Включи `Profile LED`, чтобы отправить тестовый цвет.",
                )
                return

            r = int(get_path(profile, ("led_r",), 0))
            g = int(get_path(profile, ("led_g",), 0))
            b = int(get_path(profile, ("led_b",), 0))
            brightness = float(get_path(profile, ("led_brightness",), 0.0))
            scaled_r = max(0, min(255, int(r * brightness)))
            scaled_g = max(0, min(255, int(g * brightness)))
            scaled_b = max(0, min(255, int(b * brightness)))

            if self.save_config():
                self._send_led_test_packet(scaled_r, scaled_g, scaled_b)
                self._update_status("LED test sent")
                QMessageBox.information(
                    self,
                    tr("Test LED"),
                    f"Отправлен прямой LED-тест для профиля «{tr(self._current_profile_name())}».\n"
                    f"Цвет: {scaled_r}, {scaled_g}, {scaled_b}",
                )
        except Exception as exc:
            QMessageBox.warning(self, tr("Test LED"), str(exc))

    def _send_rumble_test(self) -> None:
        duration_ms = 900
        payload = (ctypes.c_ubyte * 6)(255, 255, 0, 0, duration_ms & 0xFF, (duration_ms >> 8) & 0xFF)

        handle = _create_file(PIPE_NAME, GENERIC_WRITE, 0, None, OPEN_EXISTING, 0, None)
        if handle == INVALID_HANDLE_VALUE:
            error = ctypes.get_last_error()
            if error == ERROR_PIPE_BUSY:
                if not _wait_named_pipe(PIPE_NAME, 1000):
                    raise RuntimeError("Haptic pipe is busy")
                handle = _create_file(PIPE_NAME, GENERIC_WRITE, 0, None, OPEN_EXISTING, 0, None)
            if handle == INVALID_HANDLE_VALUE:
                raise RuntimeError("Runtime haptic pipe is not available")

        try:
            written = ctypes.c_uint32(0)
            ok = _write_file(handle, payload, 6, ctypes.byref(written), None)
            if not ok or written.value != 6:
                raise RuntimeError("Failed to write rumble packet")
            time.sleep(duration_ms / 1000.0 + 0.05)
        finally:
            _close_handle(handle)

    def _send_led_test_packet(self, r: int, g: int, b: int) -> None:
        if _hid_init() != 0:
            raise RuntimeError("hidapi init failed")

        device = _hid_open(VADER_VENDOR_ID, VADER_PRODUCT_ID, None)
        if not device:
            _hid_exit()
            raise RuntimeError(
                "Не удалось открыть контроллер для прямого LED-теста. "
                "Возможно, устройство сейчас занято runtime."
            )

        try:
            packet = (ctypes.c_ubyte * 12)(0x05, 0xE0, r, g, b, 0, 0, 0, 0, 0, 0, 0)
            written = _hid_write(device, packet, 12)
            if written < 0:
                raise RuntimeError("Не удалось отправить LED-пакет")
        finally:
            _hid_close(device)
            _hid_exit()

    def _mark_dirty(self) -> None:
        self.dirty = True
        self._update_status("Unsaved changes")

    def _update_status(self, text: str) -> None:
        self.status_label.setText(tr(text))
        suffix = " *" if self.dirty else ""
        self.setWindowTitle(f"{tr('VaderDriver Config Editor')} - {display_path(self.config_path)}{suffix}")
        self.save_button.setEnabled(self.dirty)

    def _confirm_discard_if_needed(self, text: str) -> bool:
        if not self.dirty:
            return True
        reply = QMessageBox.question(
            self,
            "Unsaved changes",
            text,
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        return reply == QMessageBox.StandardButton.Yes

    def on_advanced_toggled(self, checked: bool) -> None:
        self.show_advanced = checked
        self.refresh_visibility()

    def closeEvent(self, event: QCloseEvent) -> None:
        if self._confirm_discard_if_needed("Close without saving changes?"):
            event.accept()
        else:
            event.ignore()
