// ★★★ AUTORECOVERY v2 · PARTE 1: RE-ARRANQUE EN EL LUGAR PARA TRABAS NO FISICAS.
//
// Diseño del usuario: "yo no quiero un teleport, sino que se quede ahi y que la toma retome desde
// el punto donde quedo" · "cuando el stuck no es fisico, re-arrancarlo desde ahi" · "eso tapa el
// error visual y por AR sabriamos si se trabo". Sin teleport, sin impulso, sin velocidad inyectada.
// La traba FISICA se CLASIFICA y se REGISTRA; NO se actua (el usuario la esta pensando).
//
// UN SOLO LUGAR. La unica firma NO-FISICA medida hasta hoy es la de las paradas intermedias
// la cinta posee a un vehiculo QUIETO en CRUISE y la fila grabada donde quedo
// no es de salida (`v grabada > 2` O `freno > 0,5`). La detecta el controller (Sample) y ACA se
// decide si se actua, cuantas veces, y se deja el registro. La accion es la liberacion validada
// de (soltar el latch de posesion + re-armado por POSICION); el LAUNCH arranca solo.
// ✖ NO se generaliza a "lo aplicado no pide avanzar" (gas ~0): el banco de mostro que
//   "sin mirar el freno" rompe un relanzamiento que anda -- en las filas de la meseta (v grabada
//   ~0, freno suelto) el vehiculo sale solo. Una firma nueva entra cuando se MIDE, no antes.
// ✖ NO se re-ancla el cursor al soltar: el re-armado por posicion descansa en que la odometria
// absorbe lo recorrido en la primera consulta; el rescate de la cinta sigue
//   disponible solo si el cursor se desincroniza.
//
// PRECEDENCIA DE LO FISICO. Si la cinta aplicaba gas a fondo (>= 0,9) o hubo un CHOQUE reciente
// (caida >= 20 km/h en 0,5 s), no es un cerebro trabado: es un vehiculo que no puede. Medido sobre
// las 10 liberaciones reales de los logs de hoy: las 9 de paradas llegan con freno 1 y gas 0 a
// menos de 20 km/h; la de PAZ (contra el cartel) tenia gas 1,0 y un choque de -44 km/h 4 s antes.
// Sin la liberacion el desenlace de PAZ es el mismo: el CLAVADO mira el gas PEDIDO y abandona el leg.
//
// DEFAULTS = COMPORTAMIENTO VALIDADO. WaitS 0,5 (el reposo de LOG_COLA_QUIETO_S: "arrancar apenas se
// detiene es lo correcto", -- la espera la pone el EVENTO). CooldownS 1,0: las dos
// liberaciones de s 7227 (DEMOBUS) estan a 1,5 y 1,7 s -- con 5 s esa parada cambiaba. MaxAttempts 3
// POR EPISODIO (mismo lugar, radio EPISODIO_M): por RUTA, DEMOBUS (9) se clavaba en la 4ta parada.
class BZAutoRecovery12
{
    private const float THR_FISICO = 0.9;
    private const float CHOQUE_KMH = 20.0;
    private const int   CHOQUE_FILAS = 20;          // 0,5 s a 40 Hz
    // 60 s, no 10: PAZ empujo el cartel 14 s despues del choque; la vigencia la corta tambien
    // la distancia (EPISODIO_M). En las corridas de paradas no hubo ningun choque: no cambia nada ahi.
    private const float CHOQUE_VIGENCIA_S = 60.0;
    private const float EPISODIO_M = 8.0;           // DEMOBUS: 7227,2 -> 7235,8 son dos paradas (8,6 m)
    private const float EXITO_M = 1.0;
    private const float EXITO_KMH = 2.0;            // = CINTA_RODANDO_KMH
    private const float RESULTADO_TOPE_S = 15.0;
    private static const string CSV_PATH = "$profile:BZ_AutoDrive\\AR_log.csv";

    private bool   m_LogEn = true;
    private float  m_WaitS = 0.5;
    private int    m_MaxInt = 3;
    private float  m_CoolS = 1.0;
    private bool   m_FisEn = true;     // activado por defecto (el servicio manda el valor real)
    private string m_Ruta = "";
    private string m_Veh = "";

    private float  m_T;
    // POR QUE NO. Cada `return false` de DebeReArrancar deja aca su motivo y el instante; el
    // diagnostico del controlador lo publica. Sin esto, un AR que decide que no es indistinguible de un AR que
    // no se llamo -- y eso costo cuatro episodios diagnosticados a ciegas en dos noches.
    private string m_PorQueNo = "nunca se evaluo";
    private float  m_PorQueNoT = -999;
    private ref array<float> m_Hist = new array<float>();
    private int    m_HistIdx;
    private float  m_TChoque = -1000.0;
    private vector m_PosChoque;

    private bool   m_EpActivo;
    private vector m_EpPos;
    private int    m_EpIntentos;
    private bool   m_EpAvisado;
    private float  m_TUltIntento = -1000.0;

    private bool   m_Pend;
    private float  m_PendT0;
    private vector m_PendPos;
    private int    m_PendWp;
    private float  m_PendS;
    private string m_PendEvid;
    private int    m_PendN;
    private string m_PendTipo = "";     // NO-FISICO (re-arranque) o MANUAL (re-ubicacion)
    private string m_PendAccion = "";

    // los AHOGOS de C1 tambien van al FIN y al CSV (tipo AHOGO), para el mapa de fallas.
    private bool   m_AhogoPend;
    private int    m_AhogoWp;
    private float  m_AhogoS;
    private string m_AhogoEvid;
    private string m_AhogoAccion;
    private int    m_NAhogo;
    private string m_ListaAhogo = "";
    private int    m_NMan;

    private bool   m_FisActivo;
    private vector m_FisPos;
    // F1/F3/G1: ESCALERA FISICA por lugar (radio EPISODIO_M). Paso 1 maniobra (solo con ruedas en el
    // piso), 2 re-ubicar en el ultimo bien pisado, 3.. zonas seguras +2, +3, ... hasta m_FisLadderM. El controlador
    // pide el paso con TomarPasoFisico (Plan, 2 Hz) y lo ejecuta; el servicio mueve. Agotada -> flujo de siempre.
    private float  m_FisRevM = 3.0;
    private float  m_FisRevKmh = 5.0;
    private float  m_FisLadderM = 6.0;
    private float  m_FisPendOrigen = -1;   // -1 = el controlador elige el origen (bien pisado atras) · >= 0 = punto trabado
    // geometria de las zonas. m_LargoM = largo de la caja de colision (lo entrega el controlador); la primera
    // zona es largo/2 + ZONA_MARGEN_M; los rechazos barren +1 m sin consumir escalon, hasta m_FisSweepM.
    private const float ZONA_MARGEN_M = 1.5;
    private const float ZONA_PASO_M = 1.0;
    private float  m_LargoM;
    private float  m_FisSweepM = 10.0;
    private float  m_FisBarridoM;
    private int    m_FisPaso;
    private bool   m_FisAgotado;
    private int    m_FisPend;          // 0 nada · 1 maniobra · 2 re-ubicar (offset m_FisPendOff)
    private float  m_FisPendOff;
    private int    m_FisWp;
    private float  m_FisS;
    private float  m_FisS0;            // 2: el PRIMER punto trabado del episodio: de ahi salen las zonas +2, +3...
    private string m_FisEvid = "";
    private int    m_NFisAcc;
    private string m_ListaFisAcc = "";

    private int    m_NLog;
    private int    m_NFis;
    private string m_ListaLog = "";
    private string m_ListaFis = "";

    // La config la entrega el servicio: bz12drive no lee config de ruta.
    void Configurar(bool logEn, float waitS, int maxInt, float coolS, bool fisEn, float fisRevM, float fisRevKmh, float fisLadderM, float fisSweepM, string ruta, string veh)
    {
        m_FisSweepM = fisSweepM;
        if (m_FisSweepM < 0) m_FisSweepM = 0;
        m_FisRevM = fisRevM;
        if (m_FisRevM < 0.5) m_FisRevM = 0.5;
        m_FisRevKmh = fisRevKmh;
        if (m_FisRevKmh < 1.0) m_FisRevKmh = 1.0;
        m_FisLadderM = fisLadderM;
        if (m_FisLadderM < 0) m_FisLadderM = 0;
        m_LogEn = logEn;
        m_WaitS = waitS;
        if (m_WaitS < 0.1) m_WaitS = 0.1;
        m_MaxInt = maxInt;
        m_CoolS = coolS;
        if (m_CoolS < 0) m_CoolS = 0;
        m_FisEn = fisEn;
        m_Ruta = ruta;
        m_Veh = veh;
        Reiniciar();
        string cfg = "[C12] AutoRecovery " + m_Ruta + " · Logical.Enabled " + m_LogEn.ToString();
        cfg = cfg + " · WaitS " + m_WaitS.ToString() + " · MaxAttempts " + m_MaxInt.ToString();
        cfg = cfg + " · CooldownS " + m_CoolS.ToString() + " · Physical.Enabled " + m_FisEn.ToString();
        if (m_FisEn) cfg = cfg + " · ReverseM " + m_FisRevM.ToString() + " · ReverseMaxKmh " + m_FisRevKmh.ToString() + " · LadderMaxM " + m_FisLadderM.ToString() + " · SweepMaxM " + m_FisSweepM.ToString();
        Print(cfg);
    }

    void Reiniciar()
    {
        m_T = 0;
        m_Hist.Clear();
        m_HistIdx = 0;
        m_TChoque = -1000.0;
        m_EpActivo = false;
        m_EpIntentos = 0;
        m_EpAvisado = false;
        m_TUltIntento = -1000.0;
        m_Pend = false;
        m_FisActivo = false;
        m_FisPaso = 0;
        m_FisAgotado = false;
        m_FisPend = 0;
        m_NFisAcc = 0;
        m_ListaFisAcc = "";
        m_NLog = 0;
        m_NFis = 0;
        m_ListaLog = "";
        m_ListaFis = "";
        m_AhogoPend = false;
        m_NAhogo = 0;
        m_ListaAhogo = "";
        m_NMan = 0;
    }

    // los self-tests (fixtures, sin ruta) imprimian lineas [AR] sin nombre y las escribian en
    // AR_log.csv (5 filas del 14/09 19:57 UTC). El registro es de corridas reales, no de fixtures.
    private bool Silencio()
    {
        if (BZDriveController.s_Quiet) return true;
        if (m_Ruta == "") return true;
        return false;
    }

    float WaitS()
    {
        return m_WaitS;
    }

    // el largo del casco (caja de colision) para la geometria de las zonas seguras. 0 = sin dato.
    void SetLargo(float lenM)
    {
        m_LargoM = lenM;
    }

    // primera zona segura: la primera pose en la que el paragolpes trasero pasa el punto trabado
    private float ZonaBase()
    {
        if (m_LargoM < 2.0) return 2.0;
        return m_LargoM * 0.5 + ZONA_MARGEN_M;
    }

    // 40 Hz, todos los ticks: reloj, detector de choque, fin de episodio y resultado pendiente.
    // kmhSentido: velocidad CON SIGNO en el sentido del leg (la calcula el controller; + = avanza por la ruta).
    void Tick(BZVehicleState st, float dt, float kmhSentido)
    {
        m_T = m_T + dt;
        float kmhAhora = Math.AbsFloat(st.chassisKmh);
        if (m_Hist.Count() < CHOQUE_FILAS) m_Hist.Insert(kmhAhora);
        else m_Hist.Set(m_HistIdx, kmhAhora);
        m_HistIdx = m_HistIdx + 1;
        if (m_HistIdx >= CHOQUE_FILAS) m_HistIdx = 0;
        float maxHist = 0;
        for (int hi = 0; hi < m_Hist.Count(); hi++)
        {
            if (m_Hist.Get(hi) > maxHist) maxHist = m_Hist.Get(hi);
        }
        if (maxHist >= CHOQUE_KMH && maxHist - kmhAhora >= CHOQUE_KMH && m_T - m_TChoque > 1.0)
        {
            m_TChoque = m_T;
            m_PosChoque = st.pos;
        }
        if (m_EpActivo && Dist2D(st.pos, m_EpPos) >= EPISODIO_M)
        {
            m_EpActivo = false;
            m_EpIntentos = 0;
            m_EpAvisado = false;
        }
        if (m_FisActivo && Dist2D(st.pos, m_FisPos) >= EPISODIO_M)
        {
            // G3: la escalera se reinicia cuando el vehiculo retoma el manejo con avance REAL (sale del radio)
            m_FisActivo = false;
            m_FisPaso = 0;
            m_FisAgotado = false;
            m_FisPend = 0;
        }
        if (m_Pend)
        {
            float tPend = m_T - m_PendT0;
            // era `kmhAhora >= EXITO_KMH` con la MAGNITUD, y rodar para atras contaba como avance.
            // Medido (DEMOBUS_SUBIDA 19:23:58): el bus ahogado caia de s 13,43 a 12,40 (pz -1,8 m) a 2,3 km/h HACIA
            // ATRAS y la linea dijo "avanza a 2.26 km/h". Ahora el metro tiene que ser en el sentido del leg.
            if (Dist2D(st.pos, m_PendPos) >= EXITO_M && kmhSentido >= EXITO_KMH)
            {
                string exito = "avanza a " + kmhSentido.ToString() + " km/h en " + tPend.ToString() + " s";
                CerrarPendiente(exito);
            }
            else if (tPend > RESULTADO_TOPE_S)
            {
                string tipoVencido = m_PendTipo;
                if (m_PendTipo == "NO-FISICO") CerrarPendiente("falla: no avanzo 1 m en 15 s");
                else CerrarPendiente("sigue quieto: no avanzo 1 m en 15 s");
                // 4 (#4): un escalon vencido SUBE SOLO al siguiente; no espera a que un detector lo vea.
                // Medido: "+3 sigue quieto 15 s" y 19 s despues STUCK con +4, +5 y +6 sin usar.
                if (tipoVencido == "FISICO" && FisicoEnCurso() && m_FisPend == 0)
                {
                    m_FisPaso = m_FisPaso + 1;
                    DecidirEscalon();
                }
            }
        }
    }

    // La FIRMA NO-FISICA ya se cumple (el controller solo llama en ese caso). true = actuar ahora.
    private void NoPorque(string motivo)
    {
        m_PorQueNo = motivo;
        m_PorQueNoT = m_T;
    }

    // Lo que el AR decidio la ultima vez que lo consultaron, y hace cuanto. Si la firma del controlador no dio
    // verde, el AR no se entera siquiera: por eso el texto distingue "no se evaluo AHORA" de un motivo propio.
    string PorQueNo()
    {
        if (m_PorQueNoT < -900) return "AR: nunca se evaluo en esta corrida (su firma no dio verde nunca)";
        float hace = m_T - m_PorQueNoT;
        string ult = m_PorQueNo;
        if (ult == "") ult = "actuo";
        if (hace > 1.0) return "AR: NO SE EVALUO ahora (su firma no da verde) · ultima decision hace " + hace.ToString() + " s: " + ult;
        if (m_PorQueNo == "") return "AR: actuo";
        return "AR: dijo que NO -> " + m_PorQueNo;
    }

    bool DebeReArrancar(BZVehicleState st, int wp, float s, float thrAplicado, string evid)
    {
        bool fisGas = false;
        if (thrAplicado >= THR_FISICO) fisGas = true;
        bool choque = ChoqueReciente(st.pos);
        if (fisGas || choque)
        {
            string porFis = "derivado a FISICO por choque reciente";
            if (fisGas) porFis = "derivado a FISICO por gas aplicado";
            string evFis = porFis + " · " + evid;   // el motivo real viaja con la evidencia (el aviso BLOQUEADO lo lee)
            if (fisGas) evFis = evFis + " · gas aplicado " + thrAplicado.ToString() + " (>= 0.9)";
            evFis = evFis + EvidenciaChoque(st.pos);
            Fisico(st.pos, wp, s, evFis, RuedasEnElPiso(st));
            NoPorque(porFis);
            return false;
        }
        if (!m_EpActivo)
        {
            m_EpActivo = true;
            m_EpPos = st.pos;
            m_EpIntentos = 0;
            m_EpAvisado = false;
        }
        if (!m_LogEn)
        {
            if (!m_EpAvisado)
            {
                m_EpAvisado = true;
                if (!Silencio()) Print(Cabeza(wp, s) + " · NO-FISICO (" + evid + ") · AutoRecovery.Logical desactivado: sin accion");
                Csv(wp, s, "NO-FISICO", evid, "ninguna", "Logical desactivado");
            }
            NoPorque("AutoRecovery.Logical desactivado");
            return false;
        }
        if (m_MaxInt > 0 && m_EpIntentos >= m_MaxInt)
        {
            if (!m_EpAvisado)
            {
                m_EpAvisado = true;
                string agot = Cabeza(wp, s) + " · NO-FISICO (" + evid + ") · agotado: " + m_EpIntentos.ToString();
                agot = agot + " intentos en este lugar -> flujo de siempre";
                if (!Silencio()) Print(agot);
                Csv(wp, s, "NO-FISICO", evid, "ninguna", "agotado");
            }
            NoPorque("agotado: " + m_EpIntentos.ToString() + " de " + m_MaxInt.ToString() + " intentos en este lugar");
            return false;
        }
        if (m_T - m_TUltIntento < m_CoolS)
        {
            NoPorque("en cooldown: faltan " + (m_CoolS - (m_T - m_TUltIntento)).ToString() + " s de " + m_CoolS.ToString());
            return false;
        }
        if (m_Pend) CerrarPendiente("falla: se volvio a trabar antes de avanzar 1 m");
        m_EpIntentos = m_EpIntentos + 1;
        m_TUltIntento = m_T;
        m_NLog = m_NLog + 1;
        if (m_ListaLog != "") m_ListaLog = m_ListaLog + ", ";
        m_ListaLog = m_ListaLog + "wp " + wp.ToString() + " no-fisico";
        m_Pend = true;
        m_PendT0 = m_T;
        m_PendPos = st.pos;
        m_PendWp = wp;
        m_PendS = s;
        m_PendEvid = evid;
        m_PendN = m_EpIntentos;
        m_PendTipo = "NO-FISICO";
        m_PendAccion = "re-arranque #" + m_EpIntentos.ToString();
        m_PorQueNo = "";
        m_PorQueNoT = m_T;
        return true;
    }

    // ★ 791 AR MANUAL: el operador re-ubico el vehiculo (bandera / boton). Cuenta como AR (G2: "AR
    // invisible" no existe) con tipo MANUAL; el resultado se mide como los demas: 1 m en el sentido del leg a >= 2
    // km/h desde la pose NUEVA (por eso recibe la posicion, no st.pos, que es de antes de mover).
    void Manual(vector posNueva, int wp, float s, string evid, string accion)
    {
        if (m_Pend) CerrarPendiente("falla: el operador aplico AR manual");
        m_NLog = m_NLog + 1;
        m_NMan = m_NMan + 1;
        if (m_ListaLog != "") m_ListaLog = m_ListaLog + ", ";
        m_ListaLog = m_ListaLog + "wp " + wp.ToString() + " manual";
        m_EpActivo = false;
        m_FisActivo = false;
        m_FisPaso = 0;
        m_FisAgotado = false;
        m_FisPend = 0;
        AbrirPendiente(posNueva, wp, s, evid, accion + " #" + m_NMan.ToString(), "MANUAL");
    }

    private void AbrirPendiente(vector posNueva, int wp, float s, string evid, string accion, string tipo)
    {
        m_Pend = true;
        m_PendT0 = m_T;
        m_PendPos = posNueva;
        m_PendWp = wp;
        m_PendS = s;
        m_PendEvid = evid;
        m_PendN = m_NMan;
        m_PendTipo = tipo;
        m_PendAccion = accion;
        if (!Silencio()) Print(Cabeza(wp, s) + " · " + tipo + " (" + evid + ") · accion " + accion + " · resultado pendiente");
    }

    // un AHOGO detectado por C1 (accion = "primera + hillstart #n" o "sin accion"); el resultado llega
    // con ResultadoAhogo. Al FIN como `AHOGO xN (wp ...)`; al CSV con tipo AHOGO.
    void Ahogo(int wp, float s, string evid, string accion)
    {
        if (m_AhogoPend) ResultadoAhogo("sin resultado: nuevo ahogo antes de cerrar");
        m_NAhogo = m_NAhogo + 1;
        if (m_ListaAhogo != "") m_ListaAhogo = m_ListaAhogo + ", ";
        m_ListaAhogo = m_ListaAhogo + "wp " + wp.ToString();
        m_AhogoPend = true;
        m_AhogoWp = wp;
        m_AhogoS = s;
        m_AhogoEvid = evid;
        m_AhogoAccion = accion;
    }

    void ResultadoAhogo(string resultado)
    {
        if (!m_AhogoPend) return;
        m_AhogoPend = false;
        Csv(m_AhogoWp, m_AhogoS, "AHOGO", m_AhogoEvid, m_AhogoAccion, resultado);
    }

    static bool RuedasEnElPiso(BZVehicleState st)
    {
        if (st.wheelCount <= 0) return true;
        return st.wheelsOnGround >= st.wheelCount;
    }

    // TRABA FISICA (sin movimiento: choque lateral F1, hundido/volcado F3, gas a fondo sin avanzar).
    // Con Physical.Enabled false: una linea por episodio y se registra (comportamiento de hoy). Con true: decide el
    // ESCALON de la escalera y lo deja pendiente para el controlador (TomarPasoFisico). El usuario:
    // primero salir MANEJANDO (reversa y re-encarar), si no puede re-ubicar en el mismo punto bien orientado, y si sigue
    // trabado zonas seguras cada vez mas lejos (G1). Hundido/volcado (ruedas < todas) salta la maniobra (F3).
    void Fisico(vector pos, int wp, float s, string evid, bool ruedasOk)
    {
        if (!m_FisActivo)
        {
            m_FisActivo = true;
            m_FisPos = pos;
            m_FisS0 = s;
            m_FisPaso = 0;
            m_FisBarridoM = 0;
            m_FisAgotado = false;
            m_NFis = m_NFis + 1;
            if (m_ListaFis != "") m_ListaFis = m_ListaFis + ", ";
            m_ListaFis = m_ListaFis + "wp " + wp.ToString();
            if (!m_FisEn)
            {
                if (!Silencio()) Print(Cabeza(wp, s) + " · FISICO (" + evid + ") · sin accion (Physical.Enabled false)");
                Csv(wp, s, "FISICO", evid, "ninguna", "Physical desactivado");
                return;
            }
        }
        if (!m_FisEn) return;
        if (m_FisAgotado) return;
        if (m_FisPend != 0) return;   // ya hay un escalon esperando al controlador
        if (m_Pend) CerrarPendiente("falla: sigue trabado (traba fisica)");
        m_FisWp = wp;
        m_FisS = s;
        m_FisEvid = evid;
        m_FisPaso = m_FisPaso + 1;
        if (m_FisPaso == 1 && !ruedasOk) m_FisPaso = 2;   // F3: sin ruedas en el piso no hay maniobra que valga
        DecidirEscalon();
    }

    private void DecidirEscalon()
    {
        string accE = "";
        if (m_FisPaso == 1)
        {
            m_FisPend = 1;
            m_FisPendOff = 0;
            m_FisPendOrigen = -1;
            accE = "maniobra: reversa " + m_FisRevM.ToString() + " m y re-encarar manejando";
        }
        else if (m_FisPaso == 2)
        {
            // 2: el bien pisado queda pegado al choque; el controlador retrocede una batalla
            m_FisPend = 2;
            m_FisPendOff = 0;
            m_FisPendOrigen = -1;
            accE = "re-ubicar en el ultimo bien pisado (una batalla atras), bien orientado";
        }
        else
        {
            // zona k (0, 1, 2...) = base + k·1 m + barrido; base = largo/2 + 1,5 = la primera pose en la
            // que el paragolpes trasero pasa el punto trabado. Medido (PAZ 7,66 m): +2..+6 caian MONTADAS (0-1 ruedas,
            // 0,63-0,83 m sobre el piso): ninguna zona sacaba el obstaculo de debajo del cuerpo. El tope LadderMaxM
            // se mide desde la primera zona; el barrido de rechazos (§3) tiene el suyo (SweepMaxM).
            float zonaE = (m_FisPaso - 3) * ZONA_PASO_M;
            if (zonaE > m_FisLadderM)
            {
                m_FisAgotado = true;
                m_FisPend = 0;
                int nEscE = m_FisPaso - 1;
                if (!Silencio()) Print(Cabeza(m_FisWp, m_FisS) + " · FISICO (" + m_FisEvid + ") · escalera AGOTADA tras " + nEscE.ToString() + " escalones (LadderMaxM " + m_FisLadderM.ToString() + " desde la primera zona, barrido " + m_FisBarridoM.ToString() + " m) -> flujo de siempre");
                Csv(m_FisWp, m_FisS, "FISICO", m_FisEvid, "ninguna", "escalera agotada");
                return;
            }
            float offE = ZonaBase() + zonaE + m_FisBarridoM;
            m_FisPend = 2;
            m_FisPendOff = offE;
            m_FisPendOrigen = m_FisS0;
            accE = "zona segura +" + offE.ToString() + " m desde el primer punto trabado (s " + m_FisS0.ToString() + "; primera zona largo/2+1,5 = " + ZonaBase().ToString() + ", barrido " + m_FisBarridoM.ToString() + ")";
        }
        if (!Silencio()) Print(Cabeza(m_FisWp, m_FisS) + " · FISICO (" + m_FisEvid + ") · escalon " + m_FisPaso.ToString() + ": " + accE);
    }

    // El controlador lo consulta en Plan: 0 nada · 1 maniobra · 2 re-ubicar (offM = metros adelante del bien pisado).
    int TomarPasoFisico(out float offM, out float sOrigenM)
    {
        int pasoT = m_FisPend;
        offM = m_FisPendOff;
        sOrigenM = m_FisPendOrigen;
        m_FisPend = 0;
        return pasoT;
    }

    // El servicio no pudo re-ubicar (destino ocupado, tramo de reversa...): se registra y sigue el proximo escalon.
    // un destino RECHAZADO (ocupado, montado, sin reversa) NO consume presupuesto de escalera: la
    // maniobra y el +0 pasan al siguiente tipo; una zona rechazada se BARRE +1 m (misma zona) hasta SweepMaxM.
    // Medido: dos corridas agotadas "sin haber probado ninguna pose usable" (1 sin reversa, 1 ocupado, 4 montado).
    void SaltarEscalon(string motivo)
    {
        if (!m_FisActivo || !m_FisEn || m_FisAgotado) return;
        if (m_Pend && m_PendTipo == "FISICO") CerrarPendiente("falla: " + motivo);
        if (!Silencio()) Print(Cabeza(m_FisWp, m_FisS) + " · FISICO · escalon " + m_FisPaso.ToString() + " rechazado: " + motivo + " -> siguiente (no consume escalon)");
        Csv(m_FisWp, m_FisS, "FISICO", m_FisEvid, "escalon " + m_FisPaso.ToString(), "rechazado: " + motivo);
        if (m_FisPaso < 3)
        {
            m_FisPaso = m_FisPaso + 1;
            DecidirEscalon();
            return;
        }
        m_FisBarridoM = m_FisBarridoM + ZONA_PASO_M;
        if (m_FisBarridoM > m_FisSweepM)
        {
            m_FisAgotado = true;
            m_FisPend = 0;
            if (!Silencio()) Print(Cabeza(m_FisWp, m_FisS) + " · FISICO (" + m_FisEvid + ") · BARRIDO agotado: " + m_FisBarridoM.ToString() + " m de destinos rechazados (SweepMaxM " + m_FisSweepM.ToString() + ") -> flujo de siempre");
            Csv(m_FisWp, m_FisS, "FISICO", m_FisEvid, "ninguna", "barrido agotado");
            return;
        }
        DecidirEscalon();
    }

    // Accion fisica APLICADA (maniobra iniciada o vehiculo ya re-ubicado): abre el resultado pendiente desde la pose
    // nueva, como el AR manual. Cuenta en el FIN como AR-FISICO.
    void AccionFisicaAplicada(vector posNueva, int wp, float s, string accion)
    {
        m_NFisAcc = m_NFisAcc + 1;
        if (m_ListaFisAcc != "") m_ListaFisAcc = m_ListaFisAcc + ", ";
        m_ListaFisAcc = m_ListaFisAcc + "wp " + wp.ToString() + " " + accion;
        AbrirPendiente(posNueva, wp, s, m_FisEvid, accion + " #" + m_FisPaso.ToString(), "FISICO");
    }

    // G1: mientras la escalera tenga escalones, el leg NO se abandona (el AR nunca corta la toma).
    bool FisicoEnCurso()
    {
        return m_FisActivo && m_FisEn && !m_FisAgotado;
    }

    // el arbitro y la traba por progreso preguntan si TODAVIA hay escalera que abrir antes de declarar STUCK.
    bool FisicoAgotado()
    {
        return m_FisActivo && m_FisAgotado;
    }

    bool FisicoHabilitado()
    {
        return m_FisEn;
    }

    // la evidencia y el wp con que se abrio la escalera. Los lee el aviso BLOQUEADO del controlador, que
    // ahora sale de la TRANSICION (cerrada -> abierta) y no de quien la abre: asi el camino DebeReArrancar -> Fisico
    // (derivado a FISICO por gas aplicado / choque reciente), que abria en silencio, queda cubierto por construccion.
    string FisicoEvidencia()
    {
        return m_FisEvid;
    }

    int FisicoWp()
    {
        return m_FisWp;
    }

    // 4 (#1): el re-arranque logico AGOTADO en este lugar. Un CLAVADO despues de eso es una traba FISICA
    // aunque la cinta ahi frene (PAZ s 739,8: `cinta v 26 freno 1`, tres re-arranques, agotado, abandono).
    bool LogicoAgotadoAca(vector pos)
    {
        if (!m_EpActivo || m_MaxInt <= 0) return false;
        if (m_EpIntentos < m_MaxInt) return false;
        return Dist2D(pos, m_EpPos) < EPISODIO_M;
    }

    string EvidenciaChoque(vector pos)
    {
        if (!ChoqueReciente(pos)) return "";
        float hace = m_T - m_TChoque;
        return " · choque hace " + hace.ToString() + " s";
    }

    // El FIN lo declara: una ruta que llego por AR no es un COMPLETE limpio.
    // LAS CUENTAS, PUBLICAS. El heartbeat mostraba `AR=` con el contador de TELEPORTS de
    // v1.1, que en modo 12 no corre nunca: marcaba 0 mientras este AR hacia 29 maniobras. Un instrumento que
    // no dice la verdad cuesta horas -- aca hizo que los dos hilos afirmaran lo contrario de lo que pasaba.
    int NLogicos()
    {
        return m_NLog;
    }

    int NFisicos()
    {
        return m_NFis + m_NFisAcc;
    }

    // las intervenciones fisicas CON accion (maniobra, re-ubicacion): las que invalidan un COMPLETE. Un FISICO declarado
    // sin accion (Physical.Enabled false) no movio nada.
    int NFisicosAcc()
    {
        return m_NFisAcc;
    }

    string ListaFisAcc()
    {
        return m_ListaFisAcc;
    }

    string ResumenFin()
    {
        string res = "";
        if (m_NLog > 0) res = res + " · AR x" + m_NLog.ToString() + " (" + m_ListaLog + ")";
        if (m_NFis > 0 && !m_FisEn) res = res + " · AR-FISICO x" + m_NFis.ToString() + " sin accion (" + m_ListaFis + ")";
        if (m_NFisAcc > 0) res = res + " · AR-FISICO x" + m_NFisAcc.ToString() + " (" + m_ListaFisAcc + ")";
        if (m_NAhogo > 0) res = res + " · AHOGO x" + m_NAhogo.ToString() + " (" + m_ListaAhogo + ")";
        return res;
    }

    void Cerrar()
    {
        if (m_Pend) CerrarPendiente("sin resultado: termino la ruta");
        if (m_AhogoPend) ResultadoAhogo("sin resultado: termino la ruta");
    }

    // si C1 toma el mando con un re-arranque pendiente, ese re-arranque NO saco al vehiculo (se ahogo) y
    // lo que venga despues es de C1. Sin esto el exito del hillstart se acreditaba al AR.
    void CerrarPorAhogo()
    {
        if (m_Pend) CerrarPendiente("falla: se ahogo el motor -> toma el mando C1 (primera + hillstart)");
    }

    // Lo usa el detector de AHOGO (C1) para no llamarle ahogo a un vehiculo empujando contra lo que choco.
    bool HuboChoqueAca(vector pos)
    {
        return ChoqueReciente(pos);
    }

    private bool ChoqueReciente(vector pos)
    {
        if (m_T - m_TChoque > CHOQUE_VIGENCIA_S) return false;
        if (Dist2D(pos, m_PosChoque) >= EPISODIO_M) return false;
        return true;
    }

    private void CerrarPendiente(string resultado)
    {
        m_Pend = false;
        string lin = Cabeza(m_PendWp, m_PendS) + " · " + m_PendTipo + " (" + m_PendEvid + ")";
        lin = lin + " · accion " + m_PendAccion + " · resultado " + resultado;
        if (!Silencio()) Print(lin);
        Csv(m_PendWp, m_PendS, m_PendTipo, m_PendEvid, m_PendAccion, resultado);
    }

    private string Cabeza(int wp, float s)
    {
        return "[AR] " + m_Ruta + " wp " + wp.ToString() + " s " + s.ToString();
    }

    private float Dist2D(vector a, vector b)
    {
        float dx = a[0] - b[0];
        float dz = a[2] - b[2];
        return Math.Sqrt(dx * dx + dz * dz);
    }

    private string Pad2(int n)
    {
        if (n < 10) return "0" + n.ToString();
        return n.ToString();
    }

    // Acumulado para el mapa de fallas. Append; cabecera si el archivo es nuevo.
    private void Csv(int wp, float s, string tipo, string evid, string accion, string resultado)
    {
        if (Silencio()) return;
        bool nuevo = !FileExist(CSV_PATH);
        FileHandle fh = OpenFile(CSV_PATH, FileMode.APPEND);
        if (fh == 0) return;
        if (nuevo) FPrintln(fh, "fecha_utc,ruta,vehiculo,wp,s,tipo,evidencia,accion,resultado");
        int yy = 0;
        int mo = 0;
        int dd = 0;
        int hh = 0;
        int mi = 0;
        int ss = 0;
        GetYearMonthDayUTC(yy, mo, dd);
        GetHourMinuteSecondUTC(hh, mi, ss);
        string fecha = yy.ToString() + "-" + Pad2(mo) + "-" + Pad2(dd);
        fecha = fecha + " " + Pad2(hh) + ":" + Pad2(mi) + ":" + Pad2(ss);
        string evCsv = evid;
        evCsv.Replace(",", ";");
        string resCsv = resultado;
        resCsv.Replace(",", ";");
        string fila = fecha + "," + m_Ruta + "," + m_Veh + "," + wp.ToString() + "," + s.ToString();
        fila = fila + "," + tipo + "," + evCsv + "," + accion + "," + resCsv;
        FPrintln(fh, fila);
        CloseFile(fh);
    }
}
