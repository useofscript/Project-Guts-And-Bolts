#!/bin/bash
# Starts the Guts&Bolts server on this computer. Keep the window open while people play.
# Everything it stores (accounts, Bolts, uploads) goes in the server_data folder.
cd "$(dirname "$0")/.."
python3 install.py --server
