"""Use the project-local command dependencies in browser and native workers."""
import os
from pathlib import Path
import sys


def activate():
    environment = Path(__file__).resolve().parents[2] / '.chibi/commands-venv'
    python = environment / 'bin/python'
    if python.is_file() and Path(sys.prefix).resolve() != environment.resolve():
        os.execv(str(python), [str(python), *sys.argv])
