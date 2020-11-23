import json

json_file = '../src/memlog.json'

with open(json_file, 'r') as f:
    data = json.load(f)

for x in data['events']:
    if x['type'] == 0:
        print('malloc(' + str(x['size']) + ') = ' + str(x['addr']))
    else:
        print('free(' + str(x['addr']))
