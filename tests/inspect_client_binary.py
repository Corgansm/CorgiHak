"""Read-only PE/signature audit. Never loads or executes the inspected DLL."""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

binary = Path(sys.argv[1])
out = Path(sys.argv[2])
data = binary.read_bytes()
u16 = lambda p: struct.unpack_from('<H', data, p)[0]
u32 = lambda p: struct.unpack_from('<I', data, p)[0]
pe = u32(0x3c)
assert data[:2] == b'MZ' and data[pe:pe+4] == b'PE\0\0'
opt = pe + 24
assert u16(opt) == 0x10b, 'Expected PE32'
base = u32(opt + 28)
sections = []
for i in range(u16(pe + 6)):
    p = opt + u16(pe + 20) + 40*i
    sections.append(dict(name=data[p:p+8].rstrip(b'\0').decode(),
        rva=u32(p+12), virtual_size=u32(p+8), raw=u32(p+20), size=u32(p+16), flags=u32(p+36)))
results = []
root = Path(__file__).resolve().parents[1]
for source in (root/'base').rglob('*.cpp'):
    if 'dependencies' in source.parts:
        continue
    text = source.read_text(errors='replace')
    for m in re.finditer(r'MEM::FindPattern\(\s*CLIENT_DLL\s*,\s*_\(\s*"([0-9A-Fa-f? ]+)"', text):
        tokens = m[1].split()
        pattern = re.compile(b''.join(b'.' if '?' in t else re.escape(bytes([int(t,16)])) for t in tokens), re.DOTALL)
        matches = []
        for section in sections:
            for hit in pattern.finditer(data[section['raw']:section['raw']+section['size']]):
                rva = section['rva'] + hit.start()
                matches.append(dict(rva=hex(rva), va=hex(base+rva), section=section['name']))
        line = text.count('\n',0,m.start())+1
        results.append(dict(source=str(source.relative_to(root)), line=line,
            context=text[text.rfind('\n',0,m.start())+1:text.find('\n',m.start())].strip(),
            pattern=m[1], matches=matches))
report = dict(path=str(binary), sha256=hashlib.sha256(data).hexdigest(), size=len(data),
    machine=hex(u16(pe+4)), image_base=hex(base), sections=sections, signatures=results)
out.mkdir(parents=True, exist_ok=True)
(out/'client-signatures.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:report[k] for k in ['path','sha256','size','machine','image_base']},indent=2))
print('Signatures:',len(results),'unique:',sum(len(x['matches'])==1 for x in results),
    'missing:',sum(not x['matches'] for x in results),'ambiguous:',sum(len(x['matches'])>1 for x in results))
for result in results:
    if any(name in result['context'] for name in ['oUpdateAnimationState','oCreateAnimationState','oResetAnimationState','ClampBonesInBBox','SetupVelocityReturn']):
        print(result['context'],result['matches'])
