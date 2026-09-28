#!/bin/sh
# Run me to install Guts and Bolts on Linux:   ./install.sh
cd "$(dirname "$0")"
if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is needed. Install it with your package manager, e.g.:"
    echo "   sudo apt install python3 python3-tk"
    exit 1
fi
if ! python3 -c "import tkinter" >/dev/null 2>&1; then
    echo "(Tip: install python3-tk to get the installer window. Using the text version for now.)"
    exec python3 install.py --cli "$@"
fi
exec python3 install.py "$@"
