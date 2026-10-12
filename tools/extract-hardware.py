"""Extract the previously saved EasyEDA snapshot without changing the PCB."""
import base64
import hashlib
import io
import json
import zipfile
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=Path(r'C:\Users\user\Documents\Codex\2026-09-12\referenced-chatgpt-conversation-this-is-an\work\review'))
SOURCE = parser.parse_args().source
OUT = Path(__file__).resolve().parents[1] / 'hardware/evidence'
OUT.mkdir(parents=True, exist_ok=True)

def unwrap(value):
    while True:
        if isinstance(value, dict) and 'content' in value:
            value = value['content'][0]['text']
        elif isinstance(value, str):
            try:
                value = json.loads(value)
            except json.JSONDecodeError:
                return value
        else:
            return value

def records(source):
    for line in source.splitlines():
        if '||' in line:
            head, body = line.split('||', 1)
            if body.removesuffix('|'):
                yield json.loads(head), json.loads(body.removesuffix('|'))

snapshot = unwrap(json.loads((SOURCE / 'release_source.json').read_text(encoding='utf-8')))
archive = unwrap(json.loads((SOURCE / 'release_archive.json').read_text(encoding='utf-8')))
pcb = list(records(snapshot['source']))
raw = base64.b64decode(archive['base64'])
with zipfile.ZipFile(io.BytesIO(raw)) as z:
    all_records = []
    for name in z.namelist():
        if name.endswith('.epru'):
            all_records.extend(records(z.read(name).decode('utf-8')))
print('Project documents:', len([1 for h, b in all_records if h['type'] == 'DOCHEAD']))
(OUT / 'provenance.json').write_text(json.dumps({
    'source': str(SOURCE), 'snapshot_date': '2026-09-19',
    'live_connection_verified': False,
    'reason': 'Existing local adapter cannot listen on 127.0.0.1:8765 (EACCES).',
    'pcb_source_sha256': hashlib.sha256(snapshot['source'].encode()).hexdigest(),
    'archive_sha256': hashlib.sha256(raw).hexdigest()
}, indent=2), encoding='utf-8')

# Resolve actual GPIOs from the overridden S3 symbol, then join PCB PAD_NET.
from collections import defaultdict
docs = defaultdict(list)
current = ''
for head, body in all_records:
    if head['type'] == 'DOCHEAD':
        current = body['uuid']
    docs[current].append((head, body))
attributes = defaultdict(dict)
for head, body in pcb:
    if head['type'] == 'ATTR':
        attributes[body['parentId']][body['key']] = body['value']
module_symbols = []
for document in docs.values():
    if document[0][1]['docType'] == 'SCH_PAGE':
        sch_attributes = defaultdict(dict)
        for head, body in document:
            if head['type'] == 'ATTR':
                sch_attributes[body['parentId']][body['key']] = body.get('value')
        module_symbols.extend(value['Symbol'] for value in sch_attributes.values() if value.get('Designator') == 'MOD1')
assert len(module_symbols) == 1, 'Expected one actual MOD1 schematic symbol'
pin_attributes = defaultdict(dict)
for head, body in docs[module_symbols[0]]:
    if head['type'] == 'ATTR' and body['key'] in ['Pin Name', 'Pin Number']:
        pin_attributes[body['parentId']][body['key']] = body['value']
pin_names = {value['Pin Number']: value['Pin Name'] for value in pin_attributes.values()}
connections = defaultdict(dict)
for head, body in pcb:
    if head['type'] == 'PAD_NET':
        _, parent, number, *_ = json.loads(head['id'])
        designator = attributes[parent]['Designator']
        if number in connections[designator]:
            assert connections[designator][number] == body['padNet']
        connections[designator][number] = body['padNet']
gpio_map = {}
for pad, net in connections['MOD1'].items():
    name = pin_names[pad]
    if net and name.startswith('GPIO'):
        gpio_map[net] = {'gpio': int(name[4:].split('/')[0]), 'header_pad': int(pad)}
parts = {}
for designator in ['U10', 'U9', 'LED3']:
    value = next(value for value in attributes.values() if value.get('Designator') == designator)
    meta = next(body for head, body in docs[value['Device']] if head['type'] == 'META')
    parts[designator] = {key: meta['attributes'][key] for key in ['Manufacturer Part', 'Supplier Part', 'Datasheet']}
manifest = {
    'snapshot_date': '2026-09-19', 'pcb_uuid': next(b['uuid'] for h,b in pcb if h['type'] == 'DOCHEAD'),
    'module': 'ESP32-S3 SuperMini (schematic override of inherited C3 device)',
    'gpio_map': gpio_map, 'parts': parts,
    'pad_nets': {key: connections[key] for key in ['MOD1','U10','U9','LED3','LED4','LED5','LED6','CARD1','U2','U5','U6','U7']}
}
(OUT / 'hardware-map.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
print('GPIO map:', gpio_map)
