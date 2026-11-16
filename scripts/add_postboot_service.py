with open(r'C:\Users\Angger\.gemini\antigravity\scratch\init.power.rc', 'r') as f:
    text = f.read()

addition = """
service vendor.postboot /vendor/bin/init.postboot.sh
    user system
    group system
    oneshot
    disabled

on property:sys.boot_completed=1
    setprop vendor.powerhal.init 1
    start vendor.postboot
"""

# Replace the existing on property:sys.boot_completed=1 block
if 'start vendor.postboot' not in text:
    target = 'on property:sys.boot_completed=1\n    setprop vendor.powerhal.init 1'
    if target in text:
        text = text.replace(target, addition.strip())
    else:
        text += addition

with open(r'C:\Users\Angger\.gemini\antigravity\scratch\init.power.rc', 'w', newline='\n') as f:
    f.write(text)
print("Updated init.power.rc with vendor.postboot!")
