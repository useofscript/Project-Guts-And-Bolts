#!/bin/bash
# Double-click me to install Guts and Bolts on a Mac.
cd "$(dirname "$0")"
if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is needed. macOS will now offer to install Apple's developer tools (they include it)."
    xcode-select --install
    echo "When that finishes, double-click Install.command again."
    read -r -p "Press Enter to close..."
    exit 1
fi
python3 install.py "$@"
