with open(r'C:\Users\Angger\.gemini\antigravity\scratch\init.power.rc', 'r') as f:
    text = f.read()

old_block = """on property:vendor.touch.inhibit=1 && property:vendor.touch.stmfts.input=*
    write /sys/class/input/${vendor.touch.stmfts.input}/inhibited 0

on property:vendor.touch.inhibit=0 && property:vendor.touch.stmfts.input=*
    write /sys/class/input/${vendor.touch.stmfts.input}/inhibited 0

on property:vendor.touch.inhibit=1 && property:vendor.touch.touchkey.input=*
    write /sys/class/input/${vendor.touch.touchkey.input}/inhibited 0

on property:vendor.touch.inhibit=0 && property:vendor.touch.touchkey.input=*
    write /sys/class/input/${vendor.touch.touchkey.input}/inhibited 0"""

new_block = """on property:vendor.touch.inhibit=* && property:vendor.touch.stmfts.input=*
    write /sys/class/input/${vendor.touch.stmfts.input}/inhibited 0

on property:vendor.touch.inhibit=1 && property:vendor.touch.touchkey.input=*
    write /sys/class/input/${vendor.touch.touchkey.input}/inhibited 1

on property:vendor.touch.inhibit=0 && property:vendor.touch.touchkey.input=*
    write /sys/class/input/${vendor.touch.touchkey.input}/inhibited 0"""

if old_block in text:
    text = text.replace(old_block, new_block)
    print("Replaced old block successfully!")
else:
    print("Old block not found exactly, check formatting!")

with open(r'C:\Users\Angger\.gemini\antigravity\scratch\init.power.rc', 'w', newline='\n') as f:
    f.write(text)
