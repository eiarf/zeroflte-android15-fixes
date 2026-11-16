import json

with open(r'C:\Users\Angger\.gemini\antigravity\scratch\powerhint.json', 'r') as f:
    data = json.load(f)

for node in data.get('Nodes', []):
    if node.get('Name') == 'TouchInhibit':
        node['Values'] = ['0', '0']
        node['DefaultIndex'] = 0
        print('Patched TouchInhibit node in powerhint.json!')

with open(r'C:\Users\Angger\.gemini\antigravity\scratch\powerhint.json', 'w') as f:
    json.dump(data, f, indent=4)
print('Saved patched powerhint.json!')
