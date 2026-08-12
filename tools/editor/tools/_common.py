# Rutas PORTABLES para las herramientas del editor.
#
# El editor y sus datos se descargan en cualquier server, así que NADA de rutas absolutas.
# Todo se resuelve en este orden: argumento --xxx  ->  variable de entorno  ->  búsqueda automática.
import os
import sys

EDITOR_FILE = "trajectory_editor.html"          # marca la raíz del paquete
SCAN_DIRNAME = "BZ_AutoDrive_PathLogger"        # donde el mod deja los CSV del scan


def _arg(nombre):
    """--nombre valor  |  --nombre=valor   (y lo saca de sys.argv)"""
    pre = "--" + nombre
    for i, a in enumerate(sys.argv):
        if a == pre and i + 1 < len(sys.argv):
            v = sys.argv[i + 1]; del sys.argv[i:i + 2]; return v
        if a.startswith(pre + "="):
            v = a.split("=", 1)[1]; del sys.argv[i]; return v
    return None


def find_root(desde=None):
    """Carpeta del editor (la que tiene trajectory_editor.html). Sube hasta encontrarla."""
    v = _arg("root") or os.environ.get("BZ_EDITOR_ROOT")
    if v:
        if not os.path.exists(os.path.join(v, EDITOR_FILE)):
            sys.exit(f"--root={v} no contiene {EDITOR_FILE}")
        return os.path.abspath(v)
    d = os.path.abspath(desde or os.path.dirname(os.path.abspath(__file__)))
    for _ in range(6):                                   # tools/mapgen/x.py -> ... -> raíz
        if os.path.exists(os.path.join(d, EDITOR_FILE)):
            return d
        p = os.path.dirname(d)
        if p == d:
            break
        d = p
    if os.path.exists(os.path.join(os.getcwd(), EDITOR_FILE)):
        return os.getcwd()
    sys.exit(f"No encontré {EDITOR_FILE}. Corré el script dentro del paquete del editor, "
             f"o pasá --root C:\\ruta\\al\\editor")


def find_scan(world=None):
    """Carpeta con los CSV del scan del mod (<world>_height.csv, <world>_objects.csv, ...).

    Orden: --scan  ->  BZ_SCAN  ->  <raíz>/scan  ->  profiles\\BZ_AutoDrive_PathLogger de los
    servers que haya al lado del paquete o en las rutas típicas de DayZ Server.
    """
    def sirve(d):
        if not d or not os.path.isdir(d):
            return False
        if not world:
            return True
        return any(f.startswith(world + "_") and f.endswith(".csv") for f in os.listdir(d))

    v = _arg("scan") or os.environ.get("BZ_SCAN")
    if v:
        if not os.path.isdir(v):
            sys.exit(f"--scan={v} no existe")
        return v

    root = find_root()
    cands = [os.path.join(root, "scan")]                       # lo recomendado al publicar
    base = os.path.dirname(root)
    for d in (root, base):                                     # servers al lado del paquete
        try:
            for n in os.listdir(d):
                p = os.path.join(d, n, "profiles", SCAN_DIRNAME)
                if os.path.isdir(p):
                    cands.append(p)
        except OSError:
            pass
    for d in (r"C:\DayZServer2", r"C:\DayZServer", r"C:\Program Files (x86)\Steam\steamapps\common\DayZServer"):
        cands.append(os.path.join(d, "profiles", SCAN_DIRNAME))

    for c in cands:
        if sirve(c):
            return c
    sys.exit("No encontré los CSV del scan.\n"
             "  Copiá la carpeta 'profiles\\BZ_AutoDrive_PathLogger' de tu server a "
             f"'{os.path.join(find_root(), 'scan')}',\n"
             "  o pasá --scan C:\\ruta\\a\\BZ_AutoDrive_PathLogger")


def find_dayz():
    """Carpeta de instalación del DayZ (cliente) para extraer los tiles del satelital."""
    v = _arg("dayz") or os.environ.get("BZ_DAYZ")
    if v:
        return v
    cands = [r"C:\Program Files (x86)\Steam\steamapps\common\DayZ",
             r"D:\Steam\steamapps\common\DayZ",
             r"E:\Steam\steamapps\common\DayZ"]
    try:                                                        # Steam suele estar en el registro
        import winreg
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam")
        steam = winreg.QueryValueEx(k, "SteamPath")[0].replace("/", "\\")
        cands.insert(0, os.path.join(steam, "steamapps", "common", "DayZ"))
        lib = os.path.join(steam, "steamapps", "libraryfolders.vdf")
        if os.path.exists(lib):                                 # y puede tener otras bibliotecas
            import re
            for m in re.finditer(r'"path"\s*"([^"]+)"', open(lib, encoding="utf-8", errors="replace").read()):
                cands.append(os.path.join(m.group(1).replace("\\\\", "\\"), "steamapps", "common", "DayZ"))
    except Exception:
        pass
    for c in cands:
        if os.path.isdir(c):
            return c
    sys.exit("No encontré la instalación de DayZ. Pasá --dayz \"C:\\ruta\\a\\DayZ\"")


# tamaño del mapa: se lee del propio scan (<world>_meta.csv). Nada hardcodeado por mapa.
def world_size(scan, world, default=15360.0):
    p = os.path.join(scan, f"{world}_meta.csv")
    try:
        with open(p) as f:
            f.readline()
            return float(f.readline().strip().split(",")[1])
    except Exception:
        return default


def sfx_of(world):
    """Sufijo de los archivos por mapa: Chernarus sin sufijo, el resto '.<world>'."""
    return "" if world == "chernarusplus" else f".{world}"
