// ============================================================================
//  BZClienteFiltro - la orientacion de un vehiculo del framework, suavizada en el cliente de quien lo mira.
//
//  Quien no maneja un vehiculo lo recibe por red (el cliente es Proxy), y la orientacion llega en escalones de unos 0,44
//  grados aunque en el server se mueva suave: se ve como un temblor constante, sobre todo parado arriba o cerca. Este filtro
//  corre solo en el cliente y solo mueve lo que se dibuja: ni posicion, ni velocidad, ni fisica, ni red. El manejo no cambia.
//
//  QUIEN: el server marca los vehiculos que maneja el framework (m_BZFiltroMarca, una NetSync). Con la opcion del admin
//  apagada no marca ninguno; un vehiculo sin marca no gasta nada. Asi la opcion llega a todos los jugadores, tambien a los que
//  entran despues, sin un mensaje aparte.
//
//  LA CUENTA: e = motor - y;  v' = e / tau^2;  y' = v + 2 e / tau. Criticamente amortiguado, con un polo doble en -1/tau: en un
//  giro parejo no se atrasa. No se integra paso a paso, que diverge con tau chico y un cuadro lento: se usa la solucion
//  exacta del tramo con el motor fijo, estable con cualquier dt (a = dt / tau):
//    e1 = exp(-a) * ((1 - a) * e - dt * v)
//    v1 = exp(-a) * (dt / tau^2 * e + (1 + a) * v)
//
//  EL GANCHO ES EOnPostFrame: "al final de cada cuadro o cuando la entidad se mueve durante el cuadro". Mover la entidad adentro
//  puede volver a dispararlo: por eso la guarda de reentrada.
//
//  LO QUE SE LEE PUEDE SER LO QUE ESCRIBIMOS NOSOTROS: si el motor no reescribe entre mensajes, GetOrientation devuelve la
//  ultima escritura, y el filtro se perseguiria a si mismo y perderia el salto que llego. Por eso se guarda lo leido justo
//  despues de escribir: si en la proxima lectura sigue igual, el motor no toco nada.
// ============================================================================
class BZClienteFiltroEstado {
    bool   m_Dentro;         // guarda de reentrada
    bool   m_Escribio;       // la ultima pose del vehiculo es nuestra
    vector m_TrasEscribir;   // lo que GetOrientation devolvio justo despues de escribir
    bool   m_TieneFiltro;
    vector m_Filtro;
    vector m_V;              // la velocidad de giro del filtro, en grados/s
    bool   m_EnVuelta;       // paso el tope y esta volviendo
    bool   m_TieneMotor;
    vector m_Motor;          // la ultima orientacion que puso el motor
    int    m_UltMs;          // el reloj del juego (entero: no pierde resolucion en una sesion larga)
    int    m_Corrige;        // 0 / 1: lo que paso en la ultima pasada
    int    m_ProxLogMs;
}

class BZClienteFiltro {
    private static const float TAU_S = 0.20;
    private static const float TOPE_DEG = 3.0;          // mas que esto contra el motor: se apaga la velocidad y vuelve solo
    private static const float TOPE_GOLPE_DEG = 15.0;   // mas que esto es un teleport: vuelve de golpe
    private static const float VUELTA_TAU_S = 0.10;
    private static const float IGUAL_DEG = 0.0001;      // lo que se escribio y se volvio a leer sin que nadie lo tocara
    private static const float DT_MAX_S = 0.25;
    // Hasta aca se filtra. El escalon de 0,44 grados en la punta de un bus (5 m del centro) mueve 3,8 cm: a 150 m, con 1080p y
    // el campo de vision normal, son 0,2 pixeles, y 0,6 con la mira al maximo. Mas lejos no se ve y no vale la cuenta.
    private static const float DISTANCIA_M = 150.0;
    private static const int   LOG_CADA_MS = 5000;

    private static bool s_Listo;
    private static bool s_EsCliente;
    static bool s_Detalle;      // lineas de diagnostico: apagadas
    static int  s_Graduales;    // veces que paso el tope y volvio solo
    static int  s_Golpes;       // veces que volvio de golpe

    // -180..180: el camino corto entre dos angulos
    static float Dif(float a, float b) {
        float dD = a - b;
        while (dD > 180) dD = dD - 360;
        while (dD < -180) dD = dD + 360;
        return dD;
    }

    private static bool Iguales(vector a, vector b) {
        if (Math.AbsFloat(a[0] - b[0]) > IGUAL_DEG) return false;
        if (Math.AbsFloat(a[1] - b[1]) > IGUAL_DEG) return false;
        return Math.AbsFloat(a[2] - b[2]) <= IGUAL_DEG;
    }

    private static void Avisar(BZClienteFiltroEstado st, string linea) {
        if (!s_Detalle) return;
        int ahoraA = GetGame().GetTime();
        if (ahoraA < st.m_ProxLogMs) return;
        st.m_ProxLogMs = ahoraA + LOG_CADA_MS;
        Print("[BZFiltro] " + linea);
    }

    // lo escrito se devuelve al motor y el filtro arranca de cero
    private static void Soltar(CarScript c, BZClienteFiltroEstado st) {
        if (st.m_Escribio && st.m_TieneMotor) c.SetOrientation(st.m_Motor);
        st.m_Escribio = false;
        st.m_TieneFiltro = false;
        st.m_EnVuelta = false;
        st.m_Corrige = 0;
    }

    // Un paso de la solucion exacta, por angulo, con el error por el camino corto. false = volvio de golpe.
    private static bool Paso(CarScript c, BZClienteFiltroEstado st, float dtG) {
        float aG = dtG / TAU_S;
        float kG = Math.Pow(Math.EULER, -aG);
        float kvG = dtG / (TAU_S * TAU_S);
        vector yG = st.m_Filtro;
        vector vG = st.m_V;
        float mayorG = 0;
        for (int iG = 0; iG < 3; iG++) {
            float eG = Dif(st.m_Motor[iG], st.m_Filtro[iG]);
            float vAng = st.m_V[iG];
            float eNuevo = kG * ((1.0 - aG) * eG - dtG * vAng);
            vG[iG] = kG * (kvG * eG + (1.0 + aG) * vAng);
            yG[iG] = st.m_Motor[iG] - eNuevo;
            if (Math.AbsFloat(eNuevo) > mayorG) mayorG = Math.AbsFloat(eNuevo);
        }
        if (mayorG > TOPE_GOLPE_DEG) {
            s_Golpes = s_Golpes + 1;
            Soltar(c, st);
            Avisar(st, "mas de 15 grados contra el motor: vuelve de golpe");
            return false;
        }
        if (mayorG > TOPE_DEG) {
            if (!st.m_EnVuelta) {
                st.m_EnVuelta = true;
                s_Graduales = s_Graduales + 1;
                Avisar(st, "mas de 3 grados contra el motor: vuelve gradual");
            }
            float kR = Math.Pow(Math.EULER, -dtG / VUELTA_TAU_S);
            for (int iR = 0; iR < 3; iR++) {
                vG[iR] = vG[iR] * kR;
            }
        }
        else {
            st.m_EnVuelta = false;
        }
        st.m_Filtro = yG;
        st.m_V = vG;
        return true;
    }

    private static void Aplicar(CarScript c, BZClienteFiltroEstado st, float maxD2) {
        // solo un Proxy: el que maneja simula su propio vehiculo, y el server no pasa de la primera linea de Cuadro
        if (!c.IsProxy()) {
            Soltar(c, st);
            return;
        }
        // lo que vino del motor, y no lo nuestro
        vector leida = c.GetOrientation();
        if (!st.m_Escribio || !Iguales(leida, st.m_TrasEscribir)) {
            st.m_Motor = leida;
            st.m_TieneMotor = true;
        }
        // lejos no se ve
        if (vector.DistanceSq(c.GetPosition(), GetGame().GetCurrentCameraPosition()) > maxD2) {
            Soltar(c, st);
            return;
        }
        // si el cuerpo se activa, el cliente lo esta simulando: no se le mueve nada encima
        if (dBodyIsSet(c) && dBodyIsActive(c)) {
            Soltar(c, st);
            Avisar(st, "cuerpo activo en el Proxy: no corrige");
            return;
        }
        int ahoraF = GetGame().GetTime();
        float dtF = (ahoraF - st.m_UltMs) / 1000.0;
        st.m_UltMs = ahoraF;
        if (dtF < 0) dtF = 0;
        if (dtF > DT_MAX_S) dtF = DT_MAX_S;
        if (!st.m_TieneFiltro) {
            st.m_Filtro = st.m_Motor;
            st.m_V = "0 0 0";
            st.m_TieneFiltro = true;
        }
        if (!Paso(c, st, dtF)) return;
        c.SetOrientation(st.m_Filtro);
        st.m_TrasEscribir = c.GetOrientation();
        st.m_Escribio = true;
        st.m_Corrige = 1;
    }

    // en EOnPostFrame, solo para un vehiculo con marca o con estado
    static void Cuadro(CarScript c, bool marca) {
        if (!s_Listo) {
            s_Listo = true;
            s_EsCliente = GetGame().IsClient();
        }
        if (!s_EsCliente) return;
        BZClienteFiltroEstado st = c.BZFiltroEstado();
        if (st.m_Dentro) return;
        st.m_Dentro = true;
        if (marca) {
            Aplicar(c, st, DISTANCIA_M * DISTANCIA_M);
            st.m_Dentro = false;
            return;
        }
        // perdio la marca (se retiro el runner, un jugador tomo el volante): devuelve la pose y deja de gastar
        Soltar(c, st);
        st.m_Dentro = false;
        c.BZFiltroOlvidar();
    }
}
