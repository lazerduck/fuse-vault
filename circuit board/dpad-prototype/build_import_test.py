"""Build a document-layer-only EasyEDA import test from a saved single-snapshot project.

Requires Python 3 and cryptography. Never writes the source database.
"""
import base64
import gzip
import hashlib
import json
import math
from pathlib import Path
import sqlite3
import uuid
import zipfile

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'dpad-prototype.eprj2'
TEMPLATE = ROOT / 'dpad-prototype.epro2'
OUTPUT = ROOT / 'dpad-roundtrip-test.epro2'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse(line):
    head, sep, body = line.rstrip('\r\n').partition('||')
    try:
        return json.loads(head), json.loads(body[:-1])
    except (ValueError, TypeError):
        return None, None


def record(head, body):
    return json.dumps(head, separators=(',', ':')) + '||' + json.dumps(body, separators=(',', ':')) + '|\n'


before = digest(SOURCE)
with sqlite3.connect(SOURCE.as_uri() + '?mode=ro', uri=True) as db:
    tables = [r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'project_history_%'")]
    assert len(tables) == 1, 'Inspect history before handling a different project structure.'
    table = tables[0]
    assert all(c.isalnum() or c == '_' for c in table)
    histories = db.execute(f'SELECT uuid,key FROM "{table}"').fetchall()
    assert len(histories) == 1, 'This builder only supports the inspected full snapshot.'
    history_id, key = histories[0]
    rows = db.execute('SELECT dataStr FROM history_data WHERE history_uuid=?', (history_id,)).fetchall()
    assert len(rows) == 1
    plain = AESGCM(bytes.fromhex(key)).decrypt(bytes.fromhex(history_id), base64.b64decode(rows[0][0]), None)
    source = gzip.decompress(plain).decode('utf-8')

lines = source.splitlines(keepends=True)
# Local edit metadata is not included in exported project archives.
lines = [l for l in lines if not l.startswith('{"type":"EDIT_HEAD"}')]
heads = [(i, *parse(l)) for i, l in enumerate(lines) if l.startswith('{"type":"DOCHEAD"')]
pcbs = [(i, h, d) for i, h, d in heads if d and d.get('docType') == 'PCB']
assert len(pcbs) == 1
start, header, doc = pcbs[0]
end = next((i for i, _, _ in heads if i > start), len(lines))
max_ticket = max(parse(l)[0].get('ticket', 0) for l in lines[start:end] if parse(l)[0])
added = []


def add(kind, body):
    ticket = max_ticket + len(added) + 1
    rid = uuid.uuid5(uuid.NAMESPACE_URL, f'fuse-vault/dpad-fit-test/{len(added)}').hex[:16]
    added.append(record({'type': kind, 'ticket': ticket, 'id': rid, 'firstTicket': ticket}, body))


def mil(mm):
    return round(mm / 0.0254, 5)


def outline(points, width=0.15):
    coords = [mil(v) for p in points for v in p]
    path = coords[:2] + ['L'] + coords[2:] + coords[:2]
    add('POLY', {'groupId': 0, 'netName': '', 'layerId': 13, 'width': mil(width),
                 'path': path, 'locked': False, 'zIndex': -1, 'polyType': 'NORMAL'})


cx, cy = 61.1, -11.8
arm, half = 2.75, 8
cross = [(-arm,half),(arm,half),(arm,arm),(half,arm),(half,-arm),(arm,-arm),
         (arm,-half),(-arm,-half),(-arm,-arm),(-half,-arm),(-half,arm),(-arm,arm)]
outline([(cx+x,cy+y) for x,y in cross])
# Four candidate switch land reserves; these are graphics, not footprints.
for dx, dy in [(0,5.5),(5.5,0),(0,-5.5),(-5.5,0)]:
    w,h = (5,3.2) if dx == 0 else (3.2,5)
    x,y = cx+dx,cy+dy
    outline([(x-w/2,y-h/2),(x+w/2,y-h/2),(x+w/2,y+h/2),(x-w/2,y+h/2)],0.1)
outline([(49.149+2.2*math.cos(i*math.tau/48),-6.2+2.2*math.sin(i*math.tau/48)) for i in range(48)])
add('STRING', {'groupId':0,'layerId':13,'x':mil(40),'y':mil(3),
              'text':'D-PAD FIT TEST: 16 mm + SELECT','fontFamily':'default','fontSize':mil(1),
              'strokeWidth':mil(0.12),'bold':False,'italic':False,'origin':'LEFT_BOTTOM',
              'angle':0,'reverse':False,'expansion':0,'mirror':False,'locked':False,'zIndex':-1})
new_head = dict(header, ticket=max_ticket+len(added))
output_lines = lines[:start] + [record(new_head,doc)] + lines[start+1:end] + added + lines[end:]
# Removing the seven new graphics and restoring the document ticket must exactly
# recover all original exported bytes, including every electrical record.
restored = output_lines[:start] + [lines[start]] + output_lines[start+1:end] + output_lines[end+len(added):]
assert restored == lines
with zipfile.ZipFile(TEMPLATE) as z:
    metadata = json.loads(z.read('project2.json'))
metadata['title'] = 'dpad-roundtrip-test'
with zipfile.ZipFile(OUTPUT, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('project2.json', json.dumps(metadata, ensure_ascii=False))
    z.writestr('dpad-roundtrip-test.epru', ''.join(output_lines))
with zipfile.ZipFile(OUTPUT) as z:
    assert z.testzip() is None
    assert z.read('dpad-roundtrip-test.epru').decode() == ''.join(output_lines)
assert digest(SOURCE) == before
report = {'source':SOURCE.name,'source_sha256':before,'output':OUTPUT.name,
          'output_sha256':digest(OUTPUT),'pcb_uuid':doc['uuid'],
          'document_count':len(heads),'new_document_layer_records':len(added),
          'existing_records_preserved':True,'source_database_unchanged':True,
          'easyeda_import_verified':False,
          'note':'Full PCB recovered from native snapshot; schematic-only export supplies project metadata.'}
(ROOT/'import-test-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
