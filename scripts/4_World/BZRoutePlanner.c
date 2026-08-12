// ============================================================================
//  BZRoutePlanner - EL PLANIFICADOR DE PRE-VUELO (usuario 2026-07-06)
// ============================================================================
//  "Boris lee la ruta EN VIVO, por eso llega tarde. Hay que darle la HOJA DE RUTA
//   antes de subirse, que pegue una ojeada y ejecute con la data incorporada,
//   como los aviones (el plan de vuelo antes de despegar)."
//
//  Este pass corre UNA VEZ al cargar la ruta, ANTES de que Boris maneje. Barre la
//  ruta ENTERA, encuentra cada curva por su curvatura, y COMPUTA (no aprende) la
//  hoja de vuelo -> siembra el PRIOR en los arrays del learner:
//    - CUT PRIOR (geometria): el controlador corta hacia adentro de forma PREDECIBLE
//      (~ curvatura). Pre-distorsiona la linea hacia afuera por ese corte esperado.
//    - SPEED PRIOR: entrar mas lento a la curva (margen + autoridad de Stanley).
//
//  NO tiene que ser perfecto: es el PISO. El learner (el debrief post-vuelo) refina
//  el residuo en un par de tomas. Pero Boris ya se sube CON un plan -> arranca cerca
//  de la respuesta -> el learner no diverge (el reactivo divergia por arrancar de 0).
//
//  Predictivo por construccion: mira la curva COMPLETA antes de llegar -> pone la
//  correccion donde NACE el error (la entrada), no donde se ve (el apice).
//
//  Es "el motor calcula" hecho arquitectura: el porque geometrico/fisico, no ensayo.
// ============================================================================

class BZRoutePlanner {
    // --- calibracion del CUT del controlador (propiedad del path-following, medida una vez) ---
    // EX05: curva kappa ~0.107 rad/+-6wp -> corte medido ~1.6m  => ~15 m por rad.
    // Escala GRANDE: spacing 0.11m/wp -> +-40wp = 4.4m. La curvatura es 2da derivada = ruido a escala chica;
    // solo a gran escala emerge la curva REAL (EX05: ruido <0.07, curva real 0.15-0.9 @ apice wp210).
    static const float CUT_PER_CURV = 0.0;    // GEOMETRIA APAGADA (usuario: "verde fusionado con violeta" = el corredor
                                              // ES la toma, sin linea falsa). El corte se ataca con VELOCIDAD, no deformando el objetivo.
    static const float CURV_MIN     = 0.15;   // |kappa| (+-40wp) menor = recta/ruido -> sin plan
    static const int   CURV_WIN     = 40;     // ventana +-N wp GRANDE (rechaza el jitter de la grabacion 10Hz)
    static const float LAG_DECAY    = 0.985;  // RETARDO: el cut de Boris laggea la curvatura (se abre SALIENDO).
                                              // Arrastra la pre-distorsion downstream con este decaimiento -> cubre la salida.
    // --- prior de velocidad en curva ---
    static const float SPD_PER_CURV = 0.0;    // GATEO INVENTADO APAGADO (usuario: "mi velocidad, mis volanteos"). Se usa la
                                              // velocidad GRABADA + el plant-FF computa la direccion (Boris ES capaz: lo probo en reverse).
    static const float SPD_CAP_FRAC = 0.5;
    static const float SPD_LAG_DECAY = 0.97;  // LAG DE SALIDA suave: mantiene el gateo un toque en la salida sin crawlear el tramo recto
    static const float MAX_SHIFT    = 3.0;    // clamp del cut prior (m) - la curva esta lejos del spawn, warp suave OK

    // Computa el plan y lo SIEMBRA en outShift/outSpdDelta (arrays del learner, por referencia).
    // Se llama SOLO en la 1er corrida (sin archivo aprendido) -> el prior; despues el learner refina.
    static void SeedPlan(array<ref BZWaypoint> wps, array<float> outShift, array<float> outSpdDelta) {
        int n = wps.Count();
        int curves = 0; float maxCut = 0; float maxDv = 0;
        int i;
        for (i = 0; i < n; i++) {
            // curvatura FIRMADA local desde la geometria pristina (cambio de heading sobre la ventana)
            int a = i - CURV_WIN; if (a < 0) a = 0;
            int b = i + CURV_WIN; if (b >= n) b = n - 1;
            vector pa = wps[a].GetVector();
            vector pm = wps[i].GetVector();
            vector pb = wps[b].GetVector();
            float vax = pm[0] - pa[0]; float vaz = pm[2] - pa[2];
            float vbx = pb[0] - pm[0]; float vbz = pb[2] - pm[2];
            if (vax * vax + vaz * vaz < 1.0 || vbx * vbx + vbz * vbz < 1.0) continue;   // ventana grande: span<1m degenerado
            float h1 = Math.Atan2(vax, vaz);
            float h2 = Math.Atan2(vbx, vbz);
            float k = h2 - h1;                                   // curvatura firmada (rad sobre la ventana)
            while (k > Math.PI)  k = k - 2 * Math.PI;
            while (k < -Math.PI) k = k + 2 * Math.PI;
            if (Math.AbsFloat(k) < CURV_MIN) continue;           // recta -> sin plan (el learner la deja quieta)

            // CUT PRIOR: el controlador corta ~ CUT_PER_CURV*k hacia adentro. Sembramos el shift OPUESTO
            // (misma convencion que el reference-shaping del learner: shift = -corte_esperado). El learner
            // refina el signo/magnitud por sus pasadas; si el corte EMPEORA en la 1er vuelta -> invertir CUT_PER_CURV.
            // SIGNO CORREGIDO (2026-07-06, del grafico curva90): Boris CORTA hacia adentro; hay que pre-distorsionar
            // el corredor hacia AFUERA (no adentro) para que su corte caiga sobre la toma. El grafico mostro el plan
            // del lado equivocado (adentro, empeorando). Pegado a la toma, del lado de afuera.
            float cut = CUT_PER_CURV * k;                        // firmado
            float s = cut;
            if (s > MAX_SHIFT)  s = MAX_SHIFT;
            if (s < -MAX_SHIFT) s = -MAX_SHIFT;
            outShift.Set(i, s);

            // SPEED PRIOR: reduccion proporcional a la curvatura, con anticipacion (el humano frena ANTES).
            // La aplicamos con LEAD hacia atras: los wp de la ENTRADA heredan la reduccion de la curva.
            float dv = SPD_PER_CURV * Math.AbsFloat(k);
            float cap = wps[i].targetSpeed * SPD_CAP_FRAC;
            if (dv > cap && cap > 0) dv = cap;
            // sembrar en i y en la entrada (CURV_WIN wp antes): frenar con tiempo
            int e;
            for (e = a; e <= i; e++) { if (dv > outSpdDelta[e]) outSpdDelta.Set(e, dv); }

            curves++;
            if (Math.AbsFloat(cut) > maxCut) maxCut = Math.AbsFloat(cut);
            if (dv > maxDv) maxDv = dv;
        }
        // RETARDO downstream: el cut de Boris LAGGEA la curvatura (peaks en la SALIDA, no en el apice: EX05 apice
        // wp207 pero residuo wp292). Arrastramos el shift hacia adelante con decaimiento -> pre-distorsiona la
        // recuperacion de la salida donde Boris esta mas abierto. Solo forward (la entrada queda limpia = correcto).
        float carry = 0;
        int c2;
        for (c2 = 0; c2 < n; c2++) {
            carry = carry * LAG_DECAY;
            if (Math.AbsFloat(outShift[c2]) > Math.AbsFloat(carry)) carry = outShift[c2];
            outShift.Set(c2, carry);
        }
        // LAG DE VELOCIDAD EN LA SALIDA (a fondo, usuario 2026-07-06): arrastra la reduccion de velocidad
        // downstream con decaimiento -> Boris sigue gateando hasta que se enderezo, recien ahi acelera.
        // Ataca el residuo de salida (se abria porque aceleraba doblando todavia -> understeer).
        float carryS = 0;
        int cs;
        for (cs = 0; cs < n; cs++) {
            carryS = carryS * SPD_LAG_DECAY;
            if (outSpdDelta[cs] > carryS) carryS = outSpdDelta[cs];
            outSpdDelta.Set(cs, carryS);
        }
        BZBusLog.Info("[PLAN] hoja de vuelo computada: " + curves + " wp en curva | cut-prior max=" + maxCut + "m | freno-prior max=-" + maxDv + " km/h + lag de salida");
    }
}
