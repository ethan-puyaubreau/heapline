"""Sphinx configuration of the heapline documentation."""

import pathlib
import re

_cmake = (pathlib.Path(__file__).parent.parent / "CMakeLists.txt").read_text()

project = "heapline"
author = "Ethan Puyaubreau"
copyright = "2026, Ethan Puyaubreau"
release = re.search(r"project\(heapline VERSION ([0-9.]+)", _cmake).group(1)
version = release

extensions = ["myst_parser"]
source_suffix = {".md": "markdown"}
exclude_patterns = ["_build", "_generated"]
myst_heading_anchors = 3

html_theme = "furo"
html_title = f"heapline {release}"
