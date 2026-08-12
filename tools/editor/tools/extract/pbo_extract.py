import struct, sys, os, re
PBO=r"C:\Program Files (x86)\Steam\steamapps\common\DayZ\Addons\worlds_chernarusplus_data.pbo"
OUT=sys.argv[1] if len(sys.argv)>1 else r"_scratch\sat"
PAT=sys.argv[2] if len(sys.argv)>2 else "s_"   # substring (lower) para filtrar nombres
os.makedirs(OUT, exist_ok=True)
f=open(PBO,'rb')
def z():
    b=bytearray()
    while True:
        c=f.read(1)
        if c in (b'\x00',b''): break
        b+=c
    return b.decode('latin1')
entries=[]; first=True
while True:
    name=z(); hdr=f.read(20)
    if len(hdr)<20: break
    packing,orig,res,ts,dsize=struct.unpack('<IIIII',hdr)
    if first and packing==0x56657273:
        while True:
            k=z()
            if k=='': break
            z()
        first=False; continue
    first=False
    if name=='' and packing==0 and orig==0 and dsize==0: break
    entries.append([name,packing,orig,dsize])
datastart=f.tell(); off=datastart
for e in entries: e.append(off); off+=e[3]
cnt=0
for name,pk,orig,ds,o in entries:
    base=name.split('\\')[-1].lower()
    if PAT not in base: continue
    if pk!=0:  # solo uncompressed (S_ lo son)
        print("SKIP compressed", name); continue
    f.seek(o); data=f.read(ds)
    with open(os.path.join(OUT, name.split('\\')[-1]),'wb') as g: g.write(data)
    cnt+=1
print(f"extraidos {cnt} a {OUT}")
