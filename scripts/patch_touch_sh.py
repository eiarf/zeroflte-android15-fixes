with open(r'C:\Users\Angger\.gemini\antigravity\scratch\init.touch.sh', 'r') as f:
    text = f.read()

text = text.replace('setprop vendor.touch.stmfts.input "${dev##*/}"',
                    'setprop vendor.touch.stmfts.input "${dev##*/}"\n        echo 0 > "$dev/inhibited" 2>/dev/null')
text = text.replace('setprop vendor.touch.touchkey.input "${dev##*/}"',
                    'setprop vendor.touch.touchkey.input "${dev##*/}"\n        echo 0 > "$dev/inhibited" 2>/dev/null')

with open(r'C:\Users\Angger\.gemini\antigravity\scratch\init.touch.sh', 'w', newline='\n') as f:
    f.write(text)
print("Updated init.touch.sh successfully!")
