import json

with open(r'C:\Users\Angger\.gemini\antigravity\scratch\powerhint.json', 'r') as f:
    data = json.load(f)

for node in data.get('Nodes', []):
    if node.get('Name') == 'TouchInhibit':
        node['Values'] = ['0', '1']
        if 'DefaultIndex' in node:
            del node['DefaultIndex']
        print('Restored TouchInhibit node to original valid schema!')

with open(r'C:\Users\Angger\.gemini\antigravity\scratch\powerhint.json', 'w') as f:
    json.dump(data, f, indent=4)
print('Successfully saved valid powerhint.json!')
