# Extraer los tiles SATELITALES de Livonia (S_col_row_lco.paa) del worlds_enoch_data.pbo del CLIENTE.
# (el PBO del server no trae texturas: no renderiza)
import struct, os, re
SRC=r"C:\Program Files (x86)\Steam\steamapps\common\DayZ\Addons\worlds_enoch_data.pbo"
OUT=r"_scratch\sat_enoch"
os.makedirs(OUT, exist_ok=True)
f=open(SRC,'rb')
def asciiz():
    b=bytearray()
    while True:
        c=f.read(1)
        if not c or c==b'\x00': break
        b+=c
    return b.decode('latin1')
entries=[]
while True:
    name=asciiz(); hdr=f.read(20)
    if len(hdr)<20: break
    packing,orig,res,ts,size=struct.unpack('<5I', hdr)
    if name=='' and packing==0x56657273:
        while True:
            k=asciiz()
            if k=='': break
            asciiz()
        continue
    if name=='' and packing==0 and size==0: break
    entries.append({'name':name,'packing':packing,'orig':orig,'size':size})
off=f.tell()
for e in entries: e['off']=off; off+=e['size']
print(f"entradas: {len(entries)}")
# inventario por tipo de layer
from collections import Counter
c=Counter()
for e in entries:
    b=os.path.basename(e['name'].replace('\\','/')).lower()
    if   b.startswith('s_') and b.endswith('_lco.paa'): c['S_*_lco (satelital)']+=1
    elif b.startswith('m_'): c['M_* (mascara superficie)']+=1
    elif b.endswith('.paa'): c['otros .paa']+=1
    else: c['otros']+=1
for k,v in c.most_common(): print(f"   {k:<28} {v}")
# extraer los S_
got=0; packs=Counter(); skip=0
for e in entries:
    path=e['name'].replace('\\','/').lower()
    b=os.path.basename(path)
    if not (b.startswith('s_') and b.endswith('_lco.paa')): continue
    # OJO: hay DOS juegos con el MISMO nombre -> layers/ (el satelital real, ~128KB) y
    # usermap/ (vacios, 0 KB). Aplanar con basename hacia que usermap PISARA a layers.
    if not path.startswith('layers/'): skip+=1; continue
    if e['size']==0: skip+=1; continue
    packs[e['packing']]+=1
    if e['packing']!=0: continue          # los S_ son pack=0 (uncompressed) -> corte por offset
    f.seek(e['off']); open(os.path.join(OUT,b),'wb').write(f.read(e['size'])); got+=1
print(f"descartados (usermap/ o vacios): {skip}")
print(f"\npacking de los S_: {dict(packs)}  (0 = sin comprimir)")
print(f"tiles extraidos: {got} -> {OUT}")
# grilla: S_<col>_<row>_lco
import glob
rc=[re.match(r'S_(\d+)_(\d+)_lco\.paa', os.path.basename(p), re.I) for p in glob.glob(OUT+r"\S_*_lco.paa")]
rc=[(int(m.group(1)), int(m.group(2))) for m in rc if m]
if rc:
    cs=[a for a,b in rc]; rs=[b for a,b in rc]
    print(f"grilla: col {min(cs)}..{max(cs)}  row {min(rs)}..{max(rs)}   => {max(cs)-min(cs)+1} x {max(rs)-min(rs)+1}")
