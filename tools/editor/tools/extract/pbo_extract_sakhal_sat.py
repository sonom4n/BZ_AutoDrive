# Tiles satelitales de Sakhal (S_col_row_lco.paa) del worlds_sakhal_data.pbo del CLIENTE.
# OJO (aprendido en Livonia): hay DOS juegos con el mismo nombre -> layers/ (real) y usermap/
# (vacios). Aplanar con basename hacia que usermap pisara a layers -> filtrar por layers/.
import struct, os, re
from collections import Counter
SRC=r"C:\Program Files (x86)\Steam\steamapps\common\DayZ\sakhal\Addons\worlds_sakhal_data.pbo"
OUT=r"_scratch\sat_sakhal"
os.makedirs(OUT, exist_ok=True)
f=open(SRC,'rb')
def asciiz():
    b=bytearray()
    while True:
        c=f.read(1)
        if not c or c==b'\x00': break
        b+=c
    return b.decode('latin1')
ent=[]
while True:
    name=asciiz(); h=f.read(20)
    if len(h)<20: break
    pk,orig,res,ts,size=struct.unpack('<5I',h)
    if name=='' and pk==0x56657273:
        while True:
            k=asciiz()
            if k=='': break
            asciiz()
        continue
    if name=='' and pk==0 and size==0: break
    ent.append({'name':name,'size':size,'pk':pk})
off=f.tell()
for e in ent: e['off']=off; off+=e['size']
S=[e for e in ent if os.path.basename(e['name'].replace('\\','/')).lower().startswith('s_') and e['name'].lower().endswith('_lco.paa')]
print("entradas S_*_lco:", len(S), " carpetas:", dict(Counter(os.path.dirname(e['name'].replace(chr(92),'/')).lower() for e in S)))
got=0; skip=0
for e in S:
    path=e['name'].replace('\\','/').lower()
    if not path.startswith('layers/'): skip+=1; continue
    if e['size']==0: skip+=1; continue
    if e['pk']!=0: skip+=1; continue
    b=os.path.basename(path)
    f.seek(e['off']); open(os.path.join(OUT,b),'wb').write(f.read(e['size'])); got+=1
print(f"descartados (usermap/ o vacios): {skip}")
print(f"tiles extraidos: {got} -> {OUT}")
rc=[re.match(r's_(\d+)_(\d+)_lco\.paa', os.path.basename(p), re.I) for p in __import__('glob').glob(OUT+r"\s_*_lco.paa")]
rc=[(int(m.group(1)),int(m.group(2))) for m in rc if m]
if rc:
    cs=[a for a,b in rc]; rs=[b for a,b in rc]
    print(f"grilla: col {min(cs)}..{max(cs)} row {min(rs)}..{max(rs)} => {max(cs)-min(cs)+1} x {max(rs)-min(rs)+1}")
