#!/usr/bin/env python3
"""Check the status of the ongoing ROM build on the build server."""

import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
from server_control import run_remote_command

script = """
if [ -f /serverhive3/eiarf/rom_build.pid ]; then
    PID=$(cat /serverhive3/eiarf/rom_build.pid)
    echo "=== BUILD PROCESS (PID $PID) ==="
    ps -p "$PID" -o pid,stat,%cpu,%mem,etime,cmd || echo "Process $PID not running"
else
    echo "No PID file found"
fi

echo ""
echo "=== ACTIVE NINJA / SOONG / JAVA COMPILER PROCESSES ==="
ps -u eiarf -o pid,stat,%cpu,%mem,cmd | grep -E "ninja|soong|kati|java|mka|build" | grep -v grep | head -n 10 || echo "None"

echo ""
echo "=== LOG TAIL (last 25 lines) ==="
tail -n 25 /serverhive3/eiarf/rom_build.log 2>/dev/null || echo "Log not found"

echo ""
echo "=== WEB DIRECTORY ARTIFACTS ==="
ls -lh /var/www/html/eiarf/ 2>/dev/null || echo "Empty"
"""

run_remote_command(script)
