#!/bin/bash
# Project owner only: makes this computer's account the official "Guts" staff account.
cd "$(dirname "$0")/.."
python3 install.py --staff
