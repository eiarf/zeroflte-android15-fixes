with open(r'C:\Users\Angger\.gemini\antigravity\scratch\vendor_build.prop', 'r') as f:
    text = f.read()

props_to_add = [
    "persist.sys.sf.disable_blurs=1\n",
    "ro.surface_flinger.supports_background_blur=0\n",
    "ro.sf.blurs_are_expensive=1\n",
    "ro.config.locksound=0\n",
]

for p in props_to_add:
    key = p.split('=')[0]
    if key not in text:
        text += p

with open(r'C:\Users\Angger\.gemini\antigravity\scratch\vendor_build.prop', 'w', newline='\n') as f:
    f.write(text)
print("Updated vendor_build.prop successfully!")
