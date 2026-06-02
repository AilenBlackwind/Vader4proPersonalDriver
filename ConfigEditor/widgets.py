from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QKeyEvent
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from ConfigEditor.constants import vk_code_to_name, tr
from ConfigEditor.helpers import display_path


class KeyCaptureButton(QPushButton):
    keyCaptured = Signal(int)

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self._key_code = 0
        self._capturing = False
        self.setFocusPolicy(Qt.StrongFocus)
        self.clicked.connect(self._start_capture)
        self._update_text()

    def _start_capture(self) -> None:
        self._capturing = True
        self.setText("...")
        self.setFocus()

    def _stop_capture(self) -> None:
        self._capturing = False
        self._update_text()

    def keyPressEvent(self, event: QKeyEvent | None) -> None:
        if self._capturing and event is not None:
            code = event.nativeVirtualKey()
            if code > 0 and code != 0x1B:
                self._key_code = code
                self._capturing = False
                self._update_text()
                self.keyCaptured.emit(code)
                return
            if code == 0x1B:
                self._capturing = False
                self._update_text()
                return
        super().keyPressEvent(event)

    def focusOutEvent(self, event) -> None:
        if self._capturing:
            self._capturing = False
            self._update_text()
        super().focusOutEvent(event)

    def mousePressEvent(self, event) -> None:
        if self._capturing:
            return
        super().mousePressEvent(event)

    def set_key_code(self, code: int) -> None:
        self._key_code = code
        self._update_text()

    def key_code(self) -> int:
        return self._key_code

    def _update_text(self) -> None:
        self.setText(vk_code_to_name(self._key_code))


class AppMappingDialog(QDialog):
    def __init__(self, parent: QWidget | None, config_paths: list[str]):
        super().__init__(parent)
        self.setWindowTitle(tr("Add app override"))
        self.resize(520, 160)
        self.app_name_edit = QLineEdit()
        self.config_combo = QComboBox()

        for path in config_paths:
            self.config_combo.addItem(display_path(path), path)

        layout = QVBoxLayout(self)
        form = QFormLayout()
        form.addRow(tr("Executable name"), self.app_name_edit)
        form.addRow(tr("Config file"), self.config_combo)
        layout.addLayout(form)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def get_values(self) -> tuple[str, str]:
        return self.app_name_edit.text().strip(), str(self.config_combo.currentData())
