#!/usr/bin/env python3
"""Server control helper for waffle.serverhive.in via Paramiko SSH."""

import os
import sys
import time
import paramiko

HOST = "waffle.serverhive.in"
PORT = 22
USER = "eiarf"
PASS = "ObscureZephyr7316"


def get_ssh_client():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, port=PORT, username=USER, password=PASS, timeout=30)
    return client


def sftp_upload(local_path, remote_path):
    client = get_ssh_client()
    try:
        sftp = client.open_sftp()
        sftp.put(local_path, remote_path)
        sftp.close()
    finally:
        client.close()


def sftp_download(remote_path, local_path):
    client = get_ssh_client()
    try:
        sftp = client.open_sftp()
        sftp.get(remote_path, local_path)
        sftp.close()
    finally:
        client.close()


def run_remote_command(cmd, print_output=True):
    client = get_ssh_client()
    try:
        stdin, stdout, stderr = client.exec_command(cmd, get_pty=True)
        out_chunks = []
        while not stdout.channel.exit_status_ready() or stdout.channel.recv_ready():
            if stdout.channel.recv_ready():
                chunk = stdout.channel.recv(4096).decode("utf-8", errors="ignore")
                out_chunks.append(chunk)
                if print_output:
                    sys.stdout.write(chunk)
                    sys.stdout.flush()
            time.sleep(0.05)
        exit_code = stdout.channel.recv_exit_status()
        # flush remaining
        while stdout.channel.recv_ready():
            chunk = stdout.channel.recv(4096).decode("utf-8", errors="ignore")
            out_chunks.append(chunk)
            if print_output:
                sys.stdout.write(chunk)
                sys.stdout.flush()
        return exit_code, "".join(out_chunks)
    finally:
        client.close()


if __name__ == "__main__":
    if len(sys.argv) > 1:
        cmd = " ".join(sys.argv[1:])
        rc, _ = run_remote_command(cmd)
        sys.exit(rc)
    else:
        print("Usage: server_control.py <command>")
