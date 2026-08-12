import struct, os, time
t0=time.time()
PBO=r"C:\Program Files (x86)\Steam\steamapps\common\DayZ\Addons\worlds_chernarusplus.pbo"
OUT=r"_scratch\chernarus.wrp"
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
off=f.tell()
for e in entries:
    e.append(off); off+=e[3]
wrp=[e for e in entries if e[0].lower().endswith('.wrp')][0]
name,pk,orig,ds,o=wrp
print("wrp",name,"offset",o,"size",ds)
if not os.path.exists(OUT) or os.path.getsize(OUT)!=ds:
    f.seek(o);
    with open(OUT,'wb') as g:
        left=ds
        while left>0:
            chunk=f.read(min(8*1024*1024,left)); g.write(chunk); left-=len(chunk)
    print(f"[{time.time()-t0:.0f}s] extraido a {OUT}")
else:
    print("ya extraido")

# --- header ---
w=open(OUT,'rb'); head=w.read(128)
print("magic",head[:4],"ver",struct.unpack('<I',head[4:8])[0])
print("head hex:", head[:64].hex())
# scan strings de nombres viales y su offset
import re, mmap
w.seek(0); mm=mmap.mmap(w.fileno(),0,access=mmap.ACCESS_READ)
size=mm.size()
for tok in (b'asf1', b'kr_x', b'path_dirt', b'asf2', b'grav'):
    i=mm.find(tok); n=0; positions=[]
    while i!=-1 and n<3:
        positions.append(i); n+=1; i=mm.find(tok,i+1)
    # contar total
    cnt=0; i=mm.find(tok)
    while i!=-1: cnt+=1; i=mm.find(tok,i+1)
    print(f"  '{tok.decode()}': total={cnt} primeras_pos={positions}")
print(f"[{time.time()-t0:.0f}s] size={size}")
