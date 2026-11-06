// ============================================================================
//  BZCintaBinario.c - BZ_AutoDrive 1.2
//
//  LEER LA CINTA DE UN BINARIO. Medido en el motor, 124.258 filas: CSV 1.450 ms, columnas enteras 78 ms, valor por valor iguales.
//  El CSV sigue siendo la fuente: el binario lo genera tools\cinta_binaria.py y se puede borrar sin perder nada.
//
//  ⛔ UN BINARIO NUNCA PUEDE MENTIR SIN AVISO. Sin binario se carga el CSV, sin decir nada. Con un binario que no cierra
//  por cualquier motivo se descarta TODO, se avisa y se carga el CSV: quien llama tira esta cinta y arma otra.
//
//  El archivo, en el formato de FileSerializer (little endian):
//      int largo + "BZCINTA"
//      int version · int filas · int tamCsv · int huellaCsv · int tamBin · int huellaBin
//      14 columnas, cada una int cantidad + sus valores: s x z y h kmh fw thr brk str roll camina gear corte
//      int FIN
//
//  LA HUELLA DEL CSV es el serial (BZHuellaArchivo). La mide quien pide la carga y se compara contra la cabecera, asi que tambien
//  ve un CSV reconvertido o editado a mano sin tocar ningun otro archivo.
//
//  LA SUMA DEL BINARIO es entera: tamanio y h = h * 31 + int, sobre todo lo que sigue a la cabecera. Medido en el motor: leer los 7 MB y
//  recorrerlos cuesta 38 ms. Ve cualquier byte cambiado (31 es impar: un cambio nunca se anula solo), y tambien dos cambios
//  iguales en la misma posicion de bits, que un XOR deja pasar. La cabecera no entra porque guarda la suma; sus campos se comparan uno por uno.
// ============================================================================

// LA HUELLA DE UN ARCHIVO, en unidades: tamanio y XOR de un int cada HUELLA_PASO, leyendo de a TROZO_BYTES con ReadFile. La misma cuenta que
// huella() en tools\cinta_binaria.py. Un archivo que no abre da tamanio -1, que no coincide con ninguno.
// ⚠ Es una MUESTRA: ve lo que cambia el tamanio o cae en la muestra. Una edicion del mismo largo, 1 de cada 64.
class BZHuellaArchivo
{
    private static const int TROZO_BYTES = 4096;
    private static const int HUELLA_PASO = 64;     // en ints: 16 por trozo. Nada de esto se corta: un trozo son ~15 us
    private static const int TROZOS_POR_UNIDAD = 64;

    private FileHandle m_Fh;
    private bool m_Abierto;
    private int m_Tam;
    private int m_H;
    private int m_J;

    void Empezar(string path)
    {
        Cerrar();
        m_Tam = 0;
        m_H = 0;
        m_J = 0;
        m_Fh = OpenFile(path, FileMode.READ);
        if (m_Fh == 0)
        {
            m_Tam = -1;
            return;
        }
        m_Abierto = true;
    }

    int Tam()    { return m_Tam; }
    int Huella() { return m_H; }

    void Cerrar()
    {
        if (m_Abierto)
        {
            CloseFile(m_Fh);
            m_Abierto = false;
        }
    }

    // Una unidad de trabajo. true = termino.
    bool Unidad()
    {
        if (!m_Abierto) return true;
        int buf[1024];
        int t;
        for (t = 0; t < TROZOS_POR_UNIDAD; t++)
        {
            int leidos = ReadFile(m_Fh, buf, TROZO_BYTES);
            if (leidos <= 0)
            {
                Cerrar();
                return true;
            }
            m_Tam = m_Tam + leidos;
            int ints = leidos / 4;
            int k;
            for (k = 0; k < ints; k = k + HUELLA_PASO)
            {
                m_H = m_H ^ buf[k] ^ m_J;
                m_J = m_J + 1;
            }
        }
        return false;
    }
}

class BZCintaBinario
{
    private static const string MAGIA = "BZCINTA";
    private static const int VERSION = 3;
    private static const int FIN = 1313424962;     // "BFIN"
    private static const int TROZO_BYTES = 4096;
    private static const int SUMA_DESDE = 9;       // en ints: lo primero despues de la cabecera de 35 bytes
    private static const int TROZOS_POR_UNIDAD = 64;
    private static const int COLUMNAS = 14;

    private static const int E_CABECERA = 0;
    private static const int E_SUMA_BIN = 2;
    private static const int E_ABRIR = 3;
    private static const int E_COLUMNAS = 4;
    private static const int E_FIN = 5;

    private string m_PathBin;
    private int m_TamCsvHoy;
    private int m_HuellaCsvHoy;
    private BZCintaRunner m_Cinta;
    private int m_Decl;
    private int m_Etapa;
    private string m_Motivo;
    private ref FileSerializer m_Fs;
    private bool m_FsAbierto;
    private FileHandle m_Fh;
    private bool m_FhAbierto;
    private int m_Filas;
    private int m_TamCsv;
    private int m_HuellaCsv;
    private int m_TamBin;
    private int m_HuellaBin;
    private int m_Col;
    private int m_Tam;
    private int m_H;

    // El binario de una cinta: el mismo nombre con otra terminacion. "" si no termina como una cinta.
    static string RutaDe(string csv)
    {
        int n = csv.Length();
        if (n < 10) return "";
        if (csv.Substring(n - 10, 10) != "_cinta.csv") return "";
        return csv.Substring(0, n - 3) + "bin";
    }

    // tamCsvHoy y huellaCsvHoy: los del CSV de hoy (BZHuellaArchivo).
    void Empezar(string pathBin, BZCintaRunner cinta, int declaradas, int tamCsvHoy, int huellaCsvHoy)
    {
        m_PathBin = pathBin;
        m_TamCsvHoy = tamCsvHoy;
        m_HuellaCsvHoy = huellaCsvHoy;
        m_Cinta = cinta;
        m_Decl = declaradas;
        m_Etapa = E_CABECERA;
        m_Motivo = "";
        m_Col = 0;
    }

    bool Ok()          { return m_Etapa == E_FIN && m_Motivo == ""; }
    string Motivo()    { return m_Motivo; }
    int Filas()        { return m_Filas; }

    void Cancelar()
    {
        Cerrar();
        if (m_Etapa != E_FIN) Fallar("cancelada");
    }

    private void Cerrar()
    {
        if (m_FsAbierto) { m_Fs.Close(); m_FsAbierto = false; }
        if (m_FhAbierto) { CloseFile(m_Fh); m_FhAbierto = false; }
    }

    private void Fallar(string por)
    {
        Cerrar();
        m_Motivo = por;
        m_Etapa = E_FIN;
    }

    // Una unidad de trabajo. true = termino, bien o mal: lo dice Ok().
    bool Unidad()
    {
        if (m_Etapa == E_CABECERA) { Cabecera(); return m_Etapa == E_FIN; }
        if (m_Etapa == E_SUMA_BIN) { SumaDelBinario(); return m_Etapa == E_FIN; }
        if (m_Etapa == E_ABRIR) { Abrir(); return m_Etapa == E_FIN; }
        if (m_Etapa == E_COLUMNAS) { Columna(); return m_Etapa == E_FIN; }
        return true;
    }

    // Lee la cabecera y la revisa. true = cierra.
    private bool LeerCabecera()
    {
        string magia = "";
        int ver = -1;
        int filas = -1;
        int tamCsv = -1;
        int huellaCsv = 0;
        int tamBin = -1;
        int huellaBin = 0;
        bool leyo = m_Fs.Read(magia);
        leyo = m_Fs.Read(ver) && leyo;
        leyo = m_Fs.Read(filas) && leyo;
        leyo = m_Fs.Read(tamCsv) && leyo;
        leyo = m_Fs.Read(huellaCsv) && leyo;
        leyo = m_Fs.Read(tamBin) && leyo;
        leyo = m_Fs.Read(huellaBin) && leyo;
        if (!leyo) { Fallar("no se pudo leer la cabecera"); return false; }
        if (magia != MAGIA) { Fallar("no es un binario de cinta"); return false; }
        if (ver != VERSION) { Fallar("otra version del formato: " + ver.ToString()); return false; }
        if (filas < 1 || tamCsv < 1 || tamBin < 1) { Fallar("cabecera sin sentido"); return false; }
        if (m_Decl > 0 && filas != m_Decl) { Fallar("el binario tiene " + filas.ToString() + " filas y la ruta declara " + m_Decl.ToString()); return false; }
        if (m_Etapa != E_CABECERA && (filas != m_Filas || tamCsv != m_TamCsv || huellaCsv != m_HuellaCsv || tamBin != m_TamBin || huellaBin != m_HuellaBin))
        {
            Fallar("la cabecera cambio mientras se leia");
            return false;
        }
        m_Filas = filas;
        m_TamCsv = tamCsv;
        m_HuellaCsv = huellaCsv;
        m_TamBin = tamBin;
        m_HuellaBin = huellaBin;
        return true;
    }

    private void Cabecera()
    {
        m_Fs = new FileSerializer();
        if (!m_Fs.Open(m_PathBin, FileMode.READ)) { Fallar("no se pudo abrir"); return; }
        m_FsAbierto = true;
        if (!LeerCabecera()) return;
        if (m_TamCsv != m_TamCsvHoy || m_HuellaCsv != m_HuellaCsvHoy)
        {
            Fallar("el CSV no es del que salio este binario");
            return;
        }
        m_Fs.Close();
        m_FsAbierto = false;
        m_Tam = 0;
        m_H = 0;
        m_Etapa = E_SUMA_BIN;
    }

    private void SumaDelBinario()
    {
        if (!m_FhAbierto)
        {
            m_Fh = OpenFile(m_PathBin, FileMode.READ);
            if (m_Fh == 0) { Fallar("no se pudo abrir"); return; }
            m_FhAbierto = true;
        }
        int buf[1024];
        int t;
        for (t = 0; t < TROZOS_POR_UNIDAD; t++)
        {
            int leidos = ReadFile(m_Fh, buf, TROZO_BYTES);
            if (leidos <= 0)
            {
                CloseFile(m_Fh);
                m_FhAbierto = false;
                if (m_Tam != m_TamBin || m_H != m_HuellaBin)
                {
                    Fallar("el binario esta danado o cortado");
                    return;
                }
                m_Etapa = E_ABRIR;
                return;
            }
            int k = 0;
            if (m_Tam == 0) k = SUMA_DESDE;
            m_Tam = m_Tam + leidos;
            int ints = leidos / 4;
            int hS = m_H;
            while (k < ints)
            {
                hS = hS * 31 + buf[k];
                k = k + 1;
            }
            m_H = hS;
        }
    }

    private void Abrir()
    {
        m_Fs = new FileSerializer();
        if (!m_Fs.Open(m_PathBin, FileMode.READ)) { Fallar("no se pudo abrir"); return; }
        m_FsAbierto = true;
        if (!LeerCabecera()) return;
        m_Col = 0;
        m_Etapa = E_COLUMNAS;
    }

    // Una columna entera por unidad: la peor medida son 6,2 ms, menos que un lote de CSV.
    private void Columna()
    {
        if (m_Col >= COLUMNAS)
        {
            int fin = 0;
            bool leyoFin = m_Fs.Read(fin);
            Cerrar();
            if (!leyoFin || fin != FIN) { Fallar("falta el cierre del archivo"); return; }
            m_Etapa = E_FIN;
            return;
        }
        int cuantas = m_Cinta.LeerColumnaBin(m_Fs, m_Col);
        if (cuantas != m_Filas)
        {
            Fallar("la columna " + m_Col.ToString() + " tiene " + cuantas.ToString() + " filas y se esperaban " + m_Filas.ToString());
            return;
        }
        m_Col = m_Col + 1;
    }
}
