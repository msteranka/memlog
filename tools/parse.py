import json, sys

json_file = '../src/memlog.json'

with open(json_file, 'r') as f:
    data = json.load(f)

for x in data['events']:
    event_type = x['type']
    if event_type == 0:
        print('MALLOC EVENT:')
    elif event_type == 1:
        print('FREE EVENT:')
    elif event_type == 2:
        print('READ EVENT:')
    elif event_type == 3:
        print('WRITE EVENT:')
    else:
        print('INVALID EVENT: ' + str(x['type']))
        sys.exit()
    print('\tAddress: ' + str(x['addr']))
    print('\tSize: ' + str(x['size']))
    print('\tThread ID: ' + str(x['tid']))
    print('\tTime: ' + str(x['time']))
    print('\tBacktrace: ')
    b = x['backtrace']
    print('\t\t' + str(b['0']))
    print('\t\t' + str(b['1']))
    print('\t\t' + str(b['2']))
