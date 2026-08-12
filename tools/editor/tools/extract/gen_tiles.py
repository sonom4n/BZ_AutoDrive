import os, re
from PIL import Image
PNG=r"_scratch\sat\png"
OUT=r"_scratch\sat_tiles"
os.makedirs(OUT, exist_ok=True)
rx=re.compile(r"S_(\d+)_(\d+)")
n=0; sz=None
for fn in os.listdir(PNG):
    m=rx.match(fn)
    if not m: continue
    a=int(m.group(1)); b=int(m.group(2))
    im=Image.open(os.path.join(PNG,fn)).convert("RGB")
    if sz is None: sz=im.size; print("tile px", sz)
    im.save(os.path.join(OUT, f"t_{a}_{b}.jpg"), quality=85, optimize=True)
    n+=1
print("tiles jpg escritos:", n, "->", OUT)
import glob
tot=sum(os.path.getsize(p) for p in glob.glob(OUT+r"\*.jpg"))
print("total MB", round(tot/1024/1024,1))
