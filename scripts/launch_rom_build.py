#!/usr/bin/env python3
"""Launch ROM build on server in background and track PID."""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))
from server_control import run_remote_command

cmd = """
nohup bash /serverhive3/eiarf/build_and_publish.sh > /serverhive3/eiarf/rom_build.log 2>&1 &
PID=$!
echo "$PID" > /serverhive3/eiarf/rom_build.pid
echo "Launched build PID $PID"
sleep 2
ps -p "$PID" -o pid,stat,cmd
"""

rc, out = run_remote_command(cmd)
print(out)
sys.exit(rc)
