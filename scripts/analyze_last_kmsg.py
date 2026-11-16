import re

with open(r"C:\Users\Angger\.gemini\antigravity\scratch\last_kmsg.txt", "r", encoding="utf-8", errors="ignore") as f:
    lines = f.readlines()

print(f"Total lines in last_kmsg: {len(lines)}")
print("--- LAST 60 LINES ---")
for l in lines[-60:]:
    print(l.strip())

print("\n--- SEARCH FOR EXT4 / FS_MGR / PANIC / VOLD / DATA ---")
for i, l in enumerate(lines):
    if any(k in l.lower() for k in ["fs_mgr", "cannot be mounted", "panic", "sda23", "ext4-fs error", "failed to mount", "init:"]):
        print(f"[{i}] {l.strip()}")
