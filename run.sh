#!/bin/bash
cd "$(dirname "$0")/companion_app"
pip install -q -r requirements.txt
python3 nowplaying_server.py "$@"
