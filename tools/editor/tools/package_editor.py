# Arma el PAQUETE PUBLICABLE del editor dentro del mod.
#   python tools\package_editor.py [destino]      (o  set BZ_PUBLISH_DEST=...)
#
# Destino (cascada, ver _resolve_dest): argumento -> BZ_PUBLISH_DEST -> source del mod de Sonom4n
# SI existe en esta maquina -> fallback local <raiz>\_dist\editor. Asi el script NUNCA clava
# una ruta ajena: si alguien lo corre en SU PC, cae al _dist local en vez de fallar.
# En la maquina del autor termina en el SOURCE del mod y se bundlea dentro del @folder al empaquetar:
#     <su server>\@BZ_AutoDrive\tools\editor\trajectory_editor.html
#
# QUE SE PUBLICA Y QUE NO (reglas de uso de contenido de Bohemia):
#   SI  - editor, scripts, y DATOS DERIVADOS (coordenadas medidas: red vial, agua, edificios...)
#   SI  - relief_tiles: hillshade de NUESTRA medicion (SurfaceY in-game)
#   NO  - sat_tiles: son archivos EXTRAIDOS del juego. No se redistribuyen.
#   NO  - cover_tiles: derivan del satelital => cada usuario las genera de SU copia
#         (tools\extract\ + tools\mapgen\build_map_tiles.py). El editor funciona sin ellas.
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import find_root   # noqa: E402

ROOT = find_root(os.path.dirname(os.path.abspath(__file__)))

# --- destino: PORTABLE, nunca clava una ruta que en otra maquina no existe ---
# El script viaja dentro del paquete, asi que si alguien lo corre en SU PC no puede
# terminar apuntando (ni CREANDO) la ruta de autoria de Sonom4n. Cascada:
#   1) argumento posicional (python package_editor.py D:\ruta)
#   2) variable de entorno BZ_PUBLISH_DEST
#   3) el source del mod de Sonom4n, SOLO si existe en esta maquina (su conveniencia)
#   4) fallback siempre-escribible: <raiz>\_dist\editor (al lado del editor)
AUTORIA = r"E:\BRIGADA Z PVE SERVER\MOD-SCIPTS\BZ_AutoDrive\tools\editor"
def _resolve_dest():
    for a in sys.argv[1:]:
        if not a.startswith("-"):
            return a
    v = os.environ.get("BZ_PUBLISH_DEST")
    if v:
        return v
    if os.path.isdir(os.path.dirname(os.path.dirname(AUTORIA))):   # ...\BZ_AutoDrive existe?
        return AUTORIA
    return os.path.join(ROOT, "_dist", "editor")
DEST = _resolve_dest()

# --- que copiar ---
HTML = ["trajectory_editor.html", "trajectory_editor_livonia.html", "trajectory_editor_sakhal.html"]
# datos DERIVADOS por mapa (medicion propia) — sin sufijo = Chernarus
DATA_PREFIX = ("polylines2", "water", "rail", "bridges", "buildings",
               "roads_manual", "roads_deleted", "huellas", "obstacles", "road_pieces",
               "heightmap")   # SurfaceY medido in-game (fase 3 BZMapExtract) = medicion propia -> se publica
TILE_DIRS_OK = ("relief_tiles",)          # medicion propia -> se publica
TILE_DIRS_NO = ("sat_tiles", "cover_tiles")  # del juego / derivadas del sat -> NO
SCRIPTS = ["_common.py", "package_editor.py"]
SCRIPT_DIRS = ["mapgen", "extract"]

def mb(p):
    if os.path.isfile(p):
        return os.path.getsize(p) / 1e6
    t = 0
    for r, _, fs in os.walk(p):
        for f in fs:
            t += os.path.getsize(os.path.join(r, f))
    return t / 1e6

os.makedirs(DEST, exist_ok=True)
total = 0.0
copiados = []

for h in HTML:
    s = os.path.join(ROOT, h)
    if os.path.exists(s):
        shutil.copy2(s, DEST); total += mb(s); copiados.append(h)

for f in sorted(os.listdir(ROOT)):
    if f.endswith(".js") and f.split(".")[0] in DATA_PREFIX:
        s = os.path.join(ROOT, f)
        shutil.copy2(s, DEST); total += mb(s); copiados.append(f)

for d in sorted(os.listdir(ROOT)):
    if os.path.isdir(os.path.join(ROOT, d)) and d.split(".")[0] in TILE_DIRS_OK:
        s = os.path.join(ROOT, d); t = os.path.join(DEST, d)
        shutil.rmtree(t, ignore_errors=True); shutil.copytree(s, t)
        total += mb(s); copiados.append(d + "\\")

tdst = os.path.join(DEST, "tools")
os.makedirs(tdst, exist_ok=True)
for f in SCRIPTS:
    s = os.path.join(ROOT, "tools", f)
    if os.path.exists(s):
        shutil.copy2(s, tdst); total += mb(s)
for d in SCRIPT_DIRS:
    s = os.path.join(ROOT, "tools", d)
    if os.path.isdir(s):
        t = os.path.join(tdst, d)
        shutil.rmtree(t, ignore_errors=True)
        shutil.copytree(s, t, ignore=shutil.ignore_patterns("__pycache__"))
        total += mb(s); copiados.append("tools\\" + d + "\\")

print(f"PAQUETE -> {DEST}")
if DEST == os.path.join(ROOT, "_dist", "editor"):
    print("   (no encontre el source del mod -> use el fallback local _dist\\editor.")
    print("    para otro destino: python package_editor.py D:\\ruta   o   set BZ_PUBLISH_DEST=D:\\ruta)")
for c in copiados:
    print(f"   {c}")
print(f"   TOTAL {total:,.0f} MB")
omit = [d for d in os.listdir(ROOT)
        if os.path.isdir(os.path.join(ROOT, d)) and d.split(".")[0] in TILE_DIRS_NO]
if omit:
    print("\nNO se publican (el usuario las genera de SU copia del juego):")
    for d in omit:
        print(f"   {d}\\  ({mb(os.path.join(ROOT,d)):,.0f} MB)")
    print("   -> tools\\extract\\  y  tools\\mapgen\\build_map_tiles.py")
print("\nAhora: empaqueta el @folder con tu pipeline (bundlea tools\\ dentro del @folder)")
