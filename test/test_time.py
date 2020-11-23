import json

json_file = '../src/memlog.json'

with open(json_file, 'r') as f:
    data = json.load(f)

for i in range(0, len(data['events']) - 1):
    cur_event_time = data['events'][i]['time']
    next_event_time = data['events'][i + 1]['time']
    assert (cur_event_time <= next_event_time), 'NOOOO!'

print('YAY!')
