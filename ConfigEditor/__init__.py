import sys

from PySide6.QtWidgets import QApplication

from ConfigEditor.constants import DEFAULT_CONFIG_PATH
from ConfigEditor.editor import ConfigEditorWindow


def main() -> int:
    app = QApplication(sys.argv)
    window = ConfigEditorWindow(DEFAULT_CONFIG_PATH)
    window.show()
    return app.exec()
