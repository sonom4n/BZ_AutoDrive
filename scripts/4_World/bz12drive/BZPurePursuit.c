// ============================================================================
//  BZPurePursuit.c - BZ_AutoDrive 1.2
//
//  Modulo de TRAYECTORIA (SEAM.md 4): produce un BZDriveGoal (velocidad, decel,
//  curvatura) y NO toca pedales -- eso lo traduce la DerivationEngine.
//
//  ESTADO PROPIO: NINGUNO. Es una funcion pura, y no por casualidad: es la pieza
//  que mato el sufrimiento de "cruza tramos / se va al descampado" justamente
//  porque es geometria sin memoria. En 1.2 eso queda garantizado por el tipo --
//  sin members no hay latch que se quede pegado.
//
//  NO re-escanea el leg: todo sale de la VENTANA del BZLegView.
// ============================================================================

class BZPurePursuit
{
    // Horizonte de anticipacion. Es un parametro de CONTROL (cuanto adelante mira),
    // no una constante per-vehiculo: el largo en metros sale de la velocidad.
    // ★★★ L4 PRECISION LATERAL: 1.2 -> 0.6. El 1.2 era la anticipacion de la
    // ERA DEL AIM A 2 HZ (con 0.5 s de retardo puro, mirar lejos era sobrevivir). Con
    // aim-a-40Hz la fase es invariante a la velocidad y 0.6 deja +58 grados de margen a
    // CUALQUIER velocidad (formula del margen, T=0.05). El costo del 1.2 estaba MEDIDO
    // fleet-wide (RPT 10-47): Agera 3.63 m afuera a 91 km/h con Ld 28-32 y COMANDA~0 --
    // la ley NO comandaba la correccion porque la supresion del lazo va como 1/Ld^2 y la
    // perturbacion como Ld^2 (Ld^4 total): a la mitad de Ld, x16 de supresion. En los
    // rapidos >60 en curva el Ld efectivo ya lo mandaban techo/estab (~20 m medido), asi
    // que el cierre del arco casi no se toca: esto muerde en recta y curva mansa, donde
    // vivia la deriva. El piso de estabilidad (PREVIEW_MIN_S=0.30, despejado del margen)
    // NO se toca. FALSADOR: hunting sostenido en recta rapida (periodo ~Ld/v) -> 0.8.
    private const float PREVIEW_S = 0.6;
    // ⚠⚠⚠ PISO DEL Ld POR TIEMPO. La oscilacion medida en no fue "el Ld quedo
    // corto": fue corto PARA ESA VELOCIDAD. 5.3 m a 100 km/h son 0.19 s de anticipacion, y
    // el lazo se puso a oscilar (12/34 cambios de signo del lateral) hasta que el corte
    // paso de 14.32 a 23.74 m -- PEOR que sin techo.
    //
    // El piso geometrico (2 x batalla) no sabe de velocidad, y por eso no podia frenar eso.
    // Este si: es medio PREVIEW_S, o sea la mitad de la anticipacion nominal. Por debajo de
    // ahi el horizonte deja de ser un horizonte.
    //
    // ★★★ Y ES LO QUE RESUELVE EL TRADE QUE EL TECHO SOLO NO PODIA: "Ld largo = corte,
    // Ld corto = oscilacion, no hay ventana". La salida no era elegir cual de los dos males:
    // era dejar de pedirle al horizonte que arregle un problema de VELOCIDAD. Con el piso
    // puesto, cuando la geometria no entra a esta velocidad el que cede es la velocidad.
    //
    // ⚠⚠⚠ EL VALOR NO ES "MEDIO PREVIEW": SALE DEL MARGEN DE FASE. Lo puse en 0.6 a ojo y
    // el modelo del lazo lo desmintio. Cerrando el lazo lateral en recta (angulo chico):
    //
    //     alpha ~ psi_e + y/Ld ;  kappa = 2 alpha / Ld ;  y' = v psi ;  psi' = v kappa
    //     =>  s^2 + (2v/Ld) s + 2 v^2/Ld^2 = 0
    //     =>  w = v * sqrt(2) / Ld       zeta = 1/sqrt(2) = 0.707  SIEMPRE
    //
    // O sea que la pura persecucion esta bien amortiguada POR CONSTRUCCION, a cualquier
    // velocidad y con cualquier Ld: no hay una ganancia mal puesta que buscar. Lo que la
    // vuelve inestable es el RETARDO -- el punto de mira se refresca en Plan (2 Hz), o sea
    // medio segundo de retardo puro, que se come `w*T` de fase.
    //
    //     margen(Ld) = 65 grados - degrees( v*sqrt(2)/Ld * 0.5 )
    //
    // Y el modelo PREDICE lo que ya se habia medido, sin haber sido armado con ese dato:
    //     Ld 21.7 (nominal)          margen  +31   estable
    //     Ld 16.1 (con techo)        margen  +20   marginal
    //     Ld 10.8 (este piso en 0.6) margen   -3   AL FILO   <- lo que yo habia elegido
    //     Ld  5.3 (eps_max=0.5)      margen  -73   INESTABLE <- la corrida que oscilo
    //
    // Asi que el piso se despeja de reservar 20 grados de margen:
    //     w_max = radians(65-20)/0.5 = 1.571      PREVIEW_MIN = sqrt(2)/w_max = 0.90 s
    //
    // ⚠⚠⚠ 0.90 -> 0.30 DESPUES DEL AIM-A-40Hz. Y NO al 0.045 que yo mismo despeje.
    //
    // El 0.045 salia de `sqrt(2)/w_max` con `w_max = radians(45)/T` y **T = 1/40 s**: o sea
    // suponiendo que el refresco del aim era EL UNICO retardo del lazo. El aim-a-40Hz lo
    // elimino -- y con eso el que manda pasa a ser el que nunca medimos: el actuador de
    // direccion. Despejar contra un retardo que ya no existe, ignorando el que queda, es
    // repetir el error de haber elegido 0.90 "porque era medio PREVIEW".
    //
    // ★ Y hay una razon dura ademas de la prudente: a 65 km/h, 0.045 s da Ld_min 0.81 m,
    // MUY por debajo del piso geometrico (2 x batalla = 5.24). No seria "un piso mas bajo":
    // seria BORRAR el piso de tiempo y quedarse solo con el geometrico. Y ahi, si el retardo
    // residual resultara ser 0.10 s, el Agera a 120 km/h se quedaria con 14 grados de margen.
    //
    // 0.30 destraba el techo de corte donde hace falta --a 65 km/h deja Ld_min 5.42 m, y el
    // techo pedia 5.8-- y conserva reserva PROPORCIONAL A LA VELOCIDAD justo donde la
    // exposicion es peor: a 120 km/h deja 10 m, no 5.2.
    //
    // ✖ El numero definitivo sale de MEDIR el retardo del actuador, y el instrumento ya
    // esta puesto: la linea de direccion imprime `COMANDA`, `COMANDA_prev` y `LOGRA`
    // (curvatura lograda, del yaw rate). Con esos tres se lee cuantos ticks tarda la rueda.
    private static const float PREVIEW_MIN_S = 0.30;
    // Piso del lookahead en wheelbases. El 2L era el epitafio de estabilidad de la ERA
    // DEL AIM A 2 HZ; el relevo L1 corrio a 1.2L en curva lenta durante tres
    // corridas SIN disparar su falsador de hunting -- esa evidencia + el margen de fase
    // invariante del aim-a-40Hz generalizan el 1.2 (: el bus en crucero quedaba
    // floor-bound en 2L=10.7 con pedido 8-10 y se cruzaba de carril, |lat| max 11.72).
    // FALSADOR: hunting sostenido en cualquier regimen -> volver ESTE numero a 1.5 y
    // re-medir (no re-agregar el relevo: un piso y una excepcion son dos numeros).
    // 1.2 -> 1.5: el falsador declarado DISPARO en el bus (volante 36%
    // reversals, zigzag en cruise, Ld clavado en 6.4 el 55%). 1.5 es el punto medio
    // medido entre el floor-bound (2.0, |lat| 11.7) y el hunting (1.2). Los autos
    // casi no lo sienten: sus pisos no muerden con preview 0.6.
    private static const float LD_WB_MULT = 1.5;
    private static const float LD_FLOOR_M = 3.0;
    // Presupuesto de CORTE geometrico, en metros. Ver TechoPorCorte: no es una perilla de
    // velocidad, es cuanto se acepta que la pura persecucion recorte una curva.
    // ✖✖ SE PROBO EN 1.65 m --el envelope de v1.1-- Y FUE NET NEGATIVO. NO VOLVER
    // sin resolver antes lo de abajo.
    //
    // Lo que hizo bien: el Ld subio de 21 a 36 m, tal como estaba diseñado.
    // Lo que NO hizo: str/kFF bajo de 9x a 6.9x, no al 1.5x que yo habia calculado.
    // Lo que ROMPIO: el Offroad paso de 1.1 a 6.0 m -- corto curva, porque 1.65 m es
    // demasiado presupuesto para sus radios.
    //
    // ⚠ MI CUENTA ESTABA MAL, Y ES ALGEBRA, NO RUIDO. Aplique 1/Ld^2 a TODA la correccion,
    // y solo el termino de cross-track escala asi:
    //     rumbo       2*sin(psi)/Ld        ->  1/Ld
    //     cross-track 2*(y/Ld)/Ld = 2y/Ld^2 ->  1/Ld^2
    // Con la mezcla 50/50 que yo mismo habia medido, subir el Ld 1.71x ablanda 2.17x, no
    // 2.94x. Medido por ellos: 1.82x. La mitad de RUMBO no la toca el lookahead.
    //
    // Si algun dia se quiere el Ld largo sin romper la flota, la via es compuertarlo por
    // velocidad con el piso de 60 km/h que ya esta medido (el Offroad topa en 46 y nunca lo
    // veria) -- pero el Ld largo NO alcanzo para los supercars, asi que primero hay que
    // entender la oscilacion de rumbo.
    private static const float CORTE_MAX_M   = 0.5;
    // ★★★ EL PRESUPUESTO DE CORTE A ALTA VELOCIDAD, Y POR QUE ES OTRO NUMERO.
    //
    // Medido y confirmado: el corte estrangula el Ld a ~21 m cuando el
    // diseño pide 53-63, y eso sube la frecuencia del lazo de 1.18 a 3.55 rad/s. Con el
    // retardo del actuador (0.50-0.67 s segun decreaseSpeed) el desfase pasa de 100 grados
    // y el MARGEN DE FASE SE VUELVE NEGATIVO -37 (GT63) / -71 (GT2RS). Por eso el rumbo
    // OSCILA +-1.5 grados en recta: el lazo caza. Confirmado con dato: al subir el Ld de 21
    // a 36 el |hErr| bajo de 1.70 a 1.19 grados.
    //
    // ⚠ NO HAY OBJETIVO DE MARGEN QUE INVENTAR. Con Ld = v*PREVIEW la frecuencia es
    // constante 1.18 rad/s y el margen ya sale positivo en los dos: +20 grados el GT2RS y
    // +31 el GT63. El arreglo no es elegir un margen -- es NO DEJAR que el corte empuje el
    // Ld por debajo del diseño.
    //
    // ★ Y el valor cae solo: con el Ld de diseño a 190 km/h sobre la curvatura mediana de
    // esta ruta (0.0033) el corte resultante es 1.66 m -- que es EXACTAMENTE el envelope
    // lateral medido de v1.1 (1.65). No es coincidencia: es lo que un lookahead bien
    // dimensionado produce en este camino a esta velocidad.
    //
    // ✖✖ SOLO ARRIBA DE 60 km/h, y esa compuerta no es nueva: es la misma que ya esta
    // medida y validada para el contravolante (cero eventos en ZiS5/Offroad por encima).
    // Sin compuerta esto rompe la flota -- medido: el Offroad paso de 1.1 a 6.0 m cortando
    // curva cuando se relajo el presupuesto a todas las velocidades.
    private static const float CORTE_ALTA_M  = 1.65;
    private static const float CORTE_ALTA_KMH = 60.0;
    // N2: lead del FF (el retardo del actuador, ~4 ticks de 40 Hz), banda del corrector
    // (bien amortiguado, asentamiento ~3 s: una mano, no un latigo) y piso de velocidad
    // para las ganancias 1/v (debajo mandan los modulos POSE igual).
    private static const float FFP_LEAD_S = 0.06;   // tau transporte + slew ~2-3 ticks; barrido 1-4 ticks = falsador
    // 1.4 salio del BANCO (sim a lazo cerrado, leg real del bus) y SOBREVIVIO al spec
    // del workflow del analista: re-barrido CON deadband y
    // lead 0.06 -- 0.8 da p95 0.20 m (falla tracking), 1.4 da max 0.32 / p95 0.08 /
    // 0.24 rev/s (los tres criterios). El 0.8 de los jueces asumia FF perfecto; en la
    // ruta real el FF de ventana deja residuo que la banda tiene que barrer.
    private static const float FFP_OMEGA = 1.4;
    private static const float FFP_ZETA = 0.9;
    private static const float FFP_VMIN_MS = 3.0;
    // semibrazo de la tangente de POSICIONES para y/psi (ver el bloque de la ley)
    private static const float FFP_TAN_M = 1.5;
    // deadband SUAVE (C1, sin fase) SOLO del corrector: mata el micro-volanteo de
    // recta sin ciclo limite. eps_y = piso de RUIDO (no el objetivo: 15cm fijaria un
    // error estacionario de 15cm), eps_psi = 0.3 grados.
    private static const float FFP_EPS_Y = 0.05;
    private static const float FFP_EPS_PSI = 0.00524;
    private static const int   CORTE_PASOS   = 16;
    // Por debajo de esta curvatura la traza es recta y la flecha no existe (R > 1000 m).
    // ⚠ ZONA MUERTA DE LA SONDA FF. Debajo de esta curvatura el camino ES una recta
    // (0.006 = R > 167 m) y el deficit que se lee ahi no es camino: es el PISO DE RUIDO
    // ANGULAR del muestreo, ~0.2 grados, dividido por la ventana. Por eso CRECE al acortar
    // la ventana --a 2 m predice 0.0017 y se midio 0.0019-- y por eso un promediador no lo
    // saca: no es ruido blanco tick a tick sino un sesgo correlacionado. Medido: la media
    // movil de 3 y 5 wp deja el ruido en 0.00188 y 0.00184 contra 0.00186 crudo: NO HACE NADA.
    //
    // La separacion es de AMPLITUD, no de frecuencia, y por eso la zona muerta es GRATIS:
    //      |ff| en RECTA  p99 = 0.0057        |ff| en CURVA  p10 = 0.0116
    // En u = 0.006 mata 137 de 137 ticks de recta (100%) y cuesta 0 de 86 de curva (0%).
    //
    // Y la magnitud cierra el circulo: 0.0019 sostenido sobre 20 m de recta da k*s^2/2 =
    // 0.38 m, y la mediana de recta del bus habia subido de 0.053 a 0.157 con la ventana
    // corta mientras la CURVA mejoraba (0.617 -> 0.510, pico 1.903 -> 1.488). El costo estaba
    // entero en la recta, que es justo donde no hay curvatura que anticipar.
    //
    // ✖ ESTO TRATA EL SINTOMA. La causa es el piso angular del muestreo del BZLegView. Es
    // correcto y suficiente para ventanas >= 2 m; si algun dia se quiere 1 m hay que ir al
    // MUESTREADOR, no agregar otra compuerta.
    private const float FF_ZONA_MUERTA = 0.006;

    // ⚠ Y LA SONDA SE CALLA EN EL APPROACH FINAL, donde manda el slam.
    //
    // Medido con ventana 2.0: en los ultimos 25 m el FF quedaba vivo (|ff| p50 0.0023) y el
    // endpoint se fue de 0.105 a 0.321 m. La zona muerta sola NO alcanza ahi: el approach no
    // es recto, esta APOYADO SOBRE EL UMBRAL (curvatura mediana 0.0046, con 22% de los ticks
    // del bus y 47% de los del sedan por ENCIMA de 0.006). Depender de un umbral justo donde
    // se lo roza es fragil.
    //
    // ✖ Intente decidir con un modelo (integrar k*ds*d) si la zona muerta sola bastaba, y
    // FALLO PARIDAD: predice 1.443 m donde se midieron 0.321, y 0.528 donde se midieron
    // 0.105 -- sobre-predice 4-5x porque no modela que el lazo corrige. NO SE SACA UNA GUARDA
    // CON UN MODELO QUE NO CIERRA: la compuerta queda.
    //
    // En batallas, como el resto de las longitudes de este modulo. Cuatro cubren la zona de
    // daño medida (~16 m = 3 batallas del bus) con margen, y quedan por FUERA de la ventana
    // de alineacion del slam (ALIGN_L_WB = 2) para que los dos no se peleen en el borde.
    private const float FF_OFF_WB = 4.0;

    // ⚠⚠ MARGEN DE AUTORIDAD RESERVADO PARA EL LAZO. No es "no pidas lo imposible": ESO SOLO
    // SERIA COSMETICO, porque `SteerCommandForCurvature` ya clampea el comando a +-1 y el
    // volante termina en el mismo lugar. Lo que esto compra es OTRA cosa, y esta medido:
    //
    //   MIENTRAS EL VOLANTE ESTA CONTRA EL FIN DE CARRERA, EL LAZO ESTA ABIERTO.
    //   Rachas consecutivas de |str| > 0.90 en el barrido, a 2 Hz:
    //       ventana 3.0 -> la mas larga 1 tick  (0.5 s)     pico 2.009
    //       ventana 2.0 -> la mas larga 8 ticks (4.2 s, ~23 m a 20 km/h)   pico 2.684
    //       ventana 2.5 -> rachas de 8 Y de 5                              pico 3.079
    //       base sin FF -> ninguna                                         pico 2.082
    //   El pico no sigue a la ventana ni al total de ticks saturados: sigue a la RACHA MAS
    //   LARGA. Veintitres metros a lazo abierto en una S de R~9 m es como se juntan 2.68 m.
    //
    // Reservando este margen el comando nunca consume TODA la autoridad, asi que el lazo
    // conserva con que modular. Se paga 5% de giro en una curva que ya venia justa -- y ese
    // 5% no se estaba usando igual: por encima del tope el comando extra no dobla nada.
    //
    // ⚠ RIESGO DECLARADO: si el pico NO baja, la lectura "racha larga -> lazo abierto" era
    // correlacion y no causa, y el clamp sale. Es falsable en una corrida.
    private static const float MARGEN_AUTORIDAD = 0.05;
    private float m_KMaxCmd;
    private static const float ALAT_DIR_PISO = 5.0;   // m/s2 -- ver el bloque del cap
    private bool  m_KClampMordio;
    // ⚠⚠ OVERRIDE DE BANCO. NO ES PARTE DE LA LEY Y NO SE USA EN NINGUNA CORRIDA NORMAL.
    //
    // Existe para UN experimento controlado: correr un vehiculo CORTO con la ventana de uno
    // LARGO y ver si se escapa igual. midio que el lateral del bus ES el corte
    // geometrico a lazo abierto entero (medido/teorico 1.02) mientras el sedan suprime el
    // 37-85% del mismo corte, y que la razon entre la perturbacion (Ld^2*k/8) y la ganancia
    // que la suprime (2/Ld^2) va como Ld^4: (10.70/5.86)^4 = 11.1x. El experimento falsa eso.
    //
    // ✖ NO ES UN ARREGLO. Baja el piso = subir la ganancia del lazo, que es exactamente lo
    // que `AcortarPorGiro` hizo y divergio los dos vehiculos (ver el epitafio al final). Esto
    // sube el piso de un vehiculo corto, que es el experimento inverso y solo sirve para MEDIR.
    //
    // Es static y se fija UNA sola vez al arrancar desde una bandera: no es estado que pueda
    // quedarse pegado entre legs, que es lo unico que este modulo promete no tener.
    private static float s_PisoLdForzadoM = 0;

    static void SetPisoLdForzado(float m) { s_PisoLdForzadoM = m; }

    // ⚠ LA VENTANA DEL FEEDFORWARD, PARAMETRIZABLE. Medido: con la ventana en una batalla la
    // sonda cierra el deficit del sedan (0.594 -> 0.945) pero al bus solo a medias
    // (0.586 -> 0.706), porque UNA BATALLA DEL BUS SON 5.35 m -- casi el horizonte entero
    // del sedan. La batalla entra tambien en el feedforward, asi que esta perilla es el
    // primer paso de la opcion 2 (desacoplar la ventana del FF de la de estabilidad) sin
    // costo extra. Ausente = una batalla.
    private static float s_VentanaFFM = 0;

    static void SetVentanaFF(float m) { s_VentanaFFM = m; }

    // ⚠⚠ TOPE DE VELOCIDAD DE BANCO. NO ES PARTE DE LA LEY.
    //
    // Es un TECHO, no un objetivo: si el perfil grabado pide menos, manda el perfil. Sirve
    // para aislar cualquier efecto que dependa de la velocidad, y nace de una pregunta
    // concreta: la reversa diverge por GEOMETRIA (un polo, independiente de la velocidad) o
    // por GANANCIA contra una perturbacion (a menos velocidad el lazo tiene mas tiempo y
    // mejora). Correrla a la mitad separa las dos sin tocar una linea de la ley.
    //
    // ⚠ Acota el objetivo del PURSUIT. En modo MANIOBRA manda el MC, que fija el suyo: para
    // que el tope valga en un tramo, ese tramo tiene que estar bajo pursuit.
    private static float s_TopeVelMs = 0;

    static void SetTopeVelKmh(float kmh) { s_TopeVelMs = kmh / 3.6; }

    // ★★★ N2: ley FF-PRIMARIA tras bandera de banco. Ver
    // LeyFFPrimario. A/B en la misma sesion; se fija SIEMPRE (tambien false), la regla
    // de las banderas de banco.
    private static bool s_FFPrimario = false;

    static void SetFFPrimario(bool on) { s_FFPrimario = on; }
    static bool FFPrimario() { return s_FFPrimario; }

    // ⚠ LOS DOS LIMITES DEL SEGUIDOR, EXPUESTOS PARA QUE EL ARBITRO PREGUNTE EN VEZ DE
    // DUPLICAR. El ruteo pursuit<->maniobra en reversa necesita saber que puede seguir este
    // modulo; copiarle las constantes al arbitro seria tener dos fuentes de verdad para la
    // misma magnitud, y la que se desactualiza es siempre la copia.
    //
    // El piso del lookahead: por debajo de 2 batallas el pure pursuit OSCILA (epitafio medido
    // de AcortarPorGiro, que divergio los dos vehiculos). Es tambien la longitud minima que
    // este modulo puede VER: una maniobra mas corta que esto le queda del otro lado del punto
    // de mira.
    // ⚠⚠⚠ FIX A -- EL PUNTO DE REFERENCIA EN REVERSA. Es el remolque empujado.
    //
    // Hacia adelante el eje directriz va DELANTE y el punto de mira TIRA del vehiculo hacia
    // la traza: la realimentacion tiene el signo que corresponde y el lazo es estable.
    // Hacia atras el eje directriz ARRASTRA, y la MISMA geometria EMPUJA al vehiculo afuera.
    // No es poca ganancia -- es el signo, y ninguna ganancia lo arregla.
    //
    // Medido, y por eso se descarto ganancia:
    //   - la correccion `COMANDA - PIDE_arco` oponia al lateral solo 6/13 y a veces SUMABA.
    //     En un lazo estable con poca ganancia la correccion SIEMPRE opone; que el signo sea
    //     casi azaroso dice que lo pone la geometria.
    //   - una curva MANSA (R~7.7 m), DENTRO de la autoridad (COMANDA -0.15 contra kmax 0.19),
    //     con CERO K-CLAMP, divergio 0 -> -2.05 m monotono. Un controlador sub-ganado sobre
    //     una perturbacion acotada da error ACOTADO, no una rampa.
    //   - y paso en DOS batallas muy distintas: ZiS5 (3.72) 2.05 m y Hatchback_02 (2.4) 1.60 m.
    //     Es la geometria del seguidor, no un vehiculo.
    //
    // ★ POR QUE UNA BATALLA, SIN SABER DONDE CAE EL ORIGEN DEL MODELO:
    // no sabemos si `st.pos` esta en el eje trasero, en el delantero o en el medio -- y no
    // hace falta. Hacia adelante el seguidor FUNCIONA con la referencia que tiene hoy; lo
    // unico que la reversa necesita es la MISMA relacion, espejada. Eso es un corrimiento
    // RELATIVO de una batalla hacia el eje que LIDERA (en reversa, el trasero). La batalla es
    // MEASURED en los siete vehiculos: no entra una constante nueva.
    //
    // ★ Y TRAE SU PROPIO TERMOMETRO: el cangrejeo. El ZiS5 arrastra -15 a -21 grados de
    // `hErr` en RECTA con lateral ~0 -- un equilibrio en el lugar equivocado. Con la
    // referencia bien puesta tiene que irse a ~0. Y el Hatchback, que HOY ya tiene cangrejeo
    // ~0, es el control: si el corrimiento se pasa de rosca, le mete cangrejeo donde no
    // habia. Los dos juntos acotan el corrimiento por arriba y por abajo.
    //
    // ✖ El forward no se toca: `isReverse` falso devuelve la posicion tal cual.
    private static vector PosReferencia(vector pos, float yaw, bool isReverse, BZVehicleManual m)
    {
        if (!isReverse) return pos;
        if (!m) return pos;
        if (m.wheelbaseM < 0.5) return pos;
        // heading h  =>  versor de avance del cuerpo = (sin h, cos h). El eje lider en
        // reversa es el TRASERO, asi que la referencia se corre HACIA ATRAS del cuerpo.
        float sx = Math.Sin(yaw);
        float cz = Math.Cos(yaw);
        vector r = pos;
        r[0] = pos[0] - sx * m.wheelbaseM;
        r[2] = pos[2] - cz * m.wheelbaseM;
        return r;
    }

    static float PisoLookaheadM(BZVehicleManual m)
    {
        if (!m) return LD_FLOOR_M;
        if (m.wheelbaseM < 0.5) return LD_FLOOR_M;
        return LD_WB_MULT * m.wheelbaseM;
    }

    // La curvatura mas alta que tiene sentido comandar: la que el vehiculo ejecuta, menos el
    // margen de autoridad que el lazo necesita para modular. Es EXACTAMENTE el techo del
    // clamp (ver MARGEN_AUTORIDAD), asi que el arbitro y el clamp no pueden discrepar.
    static float CurvaturaUtilMax(BZVehicleManual m, float kmh)
    {
        if (!m) return 0;
        return m.MaxCurvature(kmh) * (1.0 - MARGEN_AUTORIDAD);
    }

    float LastFFDeficit() { return m_FFDeficit; }

    bool FFCongelado() { return m_FFCongelado; }

    bool FFEnRecta() { return m_FFEnRecta; }

    bool FFEnFinal() { return m_FFEnFinal; }

    // static: el arbitro la necesita para medir la curvatura del camino en LA MISMA ESCALA
    // en la que el seguidor la ve. Si el criterio midiera a otra escala, podria mandar a
    // maniobra por un detalle que el pursuit ni siquiera percibe.
    static float VentanaFFM(BZVehicleManual m) { return VentanaLocalM(m); }
    private float m_LastLd;
    private float m_LdPedido;      // el que pide PREVIEW, antes de toda atadura
    private float m_LdTechoCorte;  // el techo por corte de curva
    private float m_LdPiso;        // el piso
    private float m_LdFwd;         // lo que queda de tramo por delante
    private float m_LdEstab;       // el piso por estabilidad (0 debajo de la compuerta)
    // Pose del vehiculo en el ultimo Plan: la referencia contra la que se mide cuanto
    // camino le comio al punto de mira. Ver RefrescarMiraA40.
    private vector m_PosPlan;
    private bool   m_PosPlanValida;
    private ref BZPathSample m_SMira40;
    private ref BZPathSample m_SFP0;
    private ref BZPathSample m_SFP1;
    private ref BZPathSample m_SFP2;
    private float m_FFPk40;
    private bool  m_FFPk40Valido;
    private float m_FFPCorr;
    private float m_FFPKff;
    // Presupuesto de RECUPERACION del corrector N2 (a_lat, m/s2): cubre la demanda y el
    // golpe MEDIDOS en los baches del bus (RPT 18-31: demandas 5.8-9.7, kicks hasta
    // 6.2) sin depender de a que velocidad pegue. Ver el bloque de cap por componente.
    private static const float CORR_ALAT_RECUP = 10.0;
    // ★★★ TRIM INTEGRAL del corrector.
    // Los offsets suaves de 0.8-1.4 m con volante quieto son el EQUILIBRIO del
    // corrector proporcional contra el error del FF (wp350: corrP -0.0084 cancelado
    // por FF~ +0.0090, lat -1.32 estable). Medido que el error del FF es IRREDUCIBLE
    // ~+-0.005 a paso 2.2 m (4 estimadores probados contra verdad-posiciones, ninguno
    // baja de 0.006; peralte refutado: la rueda del humano explica el 96-104% de la
    // kappa). Con dK fijo, y* = dK*v^2/w^2 no se arregla con lead (no hay fase) ni con
    // mas w (refutado en banco). El integrador lento lo come SIN tocar el transitorio:
    // ancho de banda ~0.05 Hz, inerte en recta (y~0 no acumula), anti-windup con el
    // clamp mordido, decae cuando la ley no corre (reversa/quieto), y vive DENTRO del
    // corrector (presupuesto por componente lo acota; el freeze de embrague anula su
    // aplicacion). Falsador: si aparece hunting lento en curvas largas -> bajar KI;
    // si los offsets persisten con trim saturado en MAX -> el error no es constante y
    // el frente es la MEDICION de y (loggear pos).
    // ⚠⚠⚠ EL KI VA EN UNIDADES DE A_LAT Y SE DIVIDE POR v^2. El gain de
    // LAZO del integrador es KI*v^2; con KI constante era 0.78 a 50 km/h (estable,
    // banco del 546) y 16.3 a 230 (inestable de libro): el trim flipeaba +-0.003
    // (mas que el presupuesto entero a esa v) y el lat crecia 0.04 -> 0.9 -> 2.2 ->
    // 4.9 -> trompo (el fly-off del Agera en wp807). 0.77 = 0.004*(50/3.6)^2: a 50
    // km/h la ley del 546 EXACTA; el gain de lazo queda invariante a toda velocidad
    // (mismo margen de estabilidad, ~3.5x sobre w^3). El tope-mitad-del-presupuesto
    // se probo de cinturon y NO gano su lugar (identico a ki/v^2 solo): no se shippea.
    private static const float FFP_TRIM_KI_ALAT = 0.77;
    private static const float FFP_TRIM_MAX = 0.010;
    private static const float FFP_TRIM_DECAY = 0.98;
    private float m_FFPTrim;
    private float m_FFPYdb;
    // El volante APLICADO (normalizado, el del tick anterior): el discriminante real
    // del margen ejecutable. Lo setea el Controller antes del refresco de 40 Hz.
    private float m_VolanteAplicado;
    private static const float AVANCE_MIN_M = 0.05;
    private static const float AVANCE_MAX_M = 40.0;
    // ⚠ Instrumento: los tres numeros con los que se arma la curvatura del volante. El
    // trace de obligo a deducirlos invirtiendo la formula, y de ahi salio que el
    // NIVEL de `str` no lo explica la geometria (kappa 0.0925 con el camino en 0.009, o sea
    // alpha ~48 grados en una recta: eso ya no es seguimiento, es deslizamiento). Impresos,
    // la proxima vez se lee en vez de deducirse.
    private float m_DiagAlpha;
    private float m_DiagDist;
    private float m_DiagDen;
    private float m_LastKArco;
    private float m_DistAlFin;
    private float m_ALatToma;
    private float m_ALatDir;   // p95: SOLO para el cap de direccion
    // ⚠⚠ INSTRUMENTO: quien pone el MINIMO del perfil, y por que.
    //
    // `ComputeSpeedMs` se queda con el MINIMO sobre la ventana. Cuando el objetivo sale 87
    // con 149 grabado EN EL MISMO LUGAR, hay UN waypoint concreto que puso ese minimo y UNA
    // razon concreta por la que su `vj` es bajo. Sin esto, "el objetivo es conservador" es
    // una descripcion; con esto es "lo pone el wp X por la razon Y", que es accionable.
    //
    // ★ Y las tres salidas llevan a arreglos COMPLETAMENTE distintos: si manda la CURVA hay
    // que tocar el modelo de curva; si manda el GRABADO de un waypoint lejano, la ventana;
    // y si el objetivo resulta ser lo grabado y el vehiculo igual va lento, entonces no es
    // el objetivo --es aceleracion-- y todo el frente del objetivo sobra.
    private string m_MinRazon;
    private int    m_MinWp;
    private float  m_MinDist;
    private float  m_MinRecKmh;
    private string m_RazonAct;
    private int    m_StopServidoWp;   // el STOP ya SERVIDO (Resume tras un hold) no pide 0; -1 = ninguno
    private float m_MordidaKmh;
    private bool  m_EnRampaFinal;
    private vector m_LastAim;
    private bool  m_AimValido;
    private float m_LastKLocal;
    // Arco corto sobre el que se mide la curvatura LOCAL. Dos wheelbases: la escala a la que
    // el vehiculo puede responder, y suficiente para que el ruido de a un waypoint promedie.
    // ⚠ ERA `K_LOCAL_M = 5.862`, UN NUMERO DE SEDAN EN LA CAPA UNIVERSAL.
    // 5.862 = LD_WB_MULT * 2.931 = dos batallas del CivilianSedan: la formula correcta,
    // congelada para UN vehiculo. Era el unico metro crudo del modulo -- todas las demas
    // longitudes de la capa de respuesta estan en batallas o son fracciones.
    //
    // ⚠⚠ Y NO ALCANZA CON PONERLE `2 * wheelbase`, que es lo obvio: eso lo hace COINCIDIR
    // EXACTAMENTE con el piso del Ld (LD_WB_MULT * wheelbase). Medido sobre las corridas
    // reales, el piso manda en el 45% de los ticks del bus y el 42% del sedan --incluida
    // la curva de wp 359 que es justo la que sub-comanda--. Ahi las dos ventanas serian la
    // MISMA y `kLocal - kArco` daria cero: la medida perderia todo su contenido justo
    // donde importa.
    //
    // La ventana local tiene que ser mas corta que el Ld POR CONSTRUCCION, si no no es
    // local. Una batalla es el limite geometrico que el resto del framework ya usa
    // (ENDPOINT_WB), y garantiza local < piso del Ld = dos batallas.
    //
    // ⚠ CAMBIA EL NUMERO QUE SE VENIA LEYENDO: en el sedan PIDE_local pasa de medirse
    // sobre 5.86 m a 2.93 m, asi que en curva va a salir MAS GRANDE que antes. No es una
    // regresion: antes estaba diluido. Hoy solo alimenta el log.
    // ⚠⚠⚠ ESTO REVIERTE LA DIRECCION DEL ARREGLO DE ARRIBA, Y LA RAZON IMPORTA.
    //
    // El bloque de arriba cuenta que `K_LOCAL_M = 5.862` salio porque era "un numero de
    // SEDAN en la capa universal" -- 2 x 2.931, la geometria de UN vehiculo promovida a
    // constante. Se cambio a batallas, correctamente, con el dato que habia entonces.
    //
    // Ahora vuelve a ser un ABSOLUTO, y no es el mismo error: la diferencia es la EVIDENCIA.
    // Aquel 5.862 salia de un vehiculo; este sale de un barrido en DOS vehiculos con
    // batallas muy distintas (5.35 y 2.93), y la hipotesis de fraccion quedo FALSADA:
    //
    //    optimo medido    bus 3.0 m (batalla 5.35 -> 0.56)   sedan 2.9-4.0 (batalla 2.93 -> 1.0)
    //
    // Si fuera fraccion de batalla, el optimo del sedan estaria en 0.56 x 2.93 = 1.64 m --
    // y ahi es justo donde el sedan esta PEOR (pico 2.35 a 1.5 m y 2.64 a 2.0 m, contra 1.27
    // en 2.93). Al reves de lo que esa hipotesis predice. El absoluto, en cambio, es
    // consistente en los dos.
    //
    // Y hay por que: las DOS cotas de esta ventana son universales. Abajo la pone el piso de
    // ruido angular de `BZRouteData.Head(i)`, que es la direccion del SEGMENTO sobre ~0.4 m
    // --propiedad del MUESTREADOR, no del vehiculo--. Arriba la pone la dilucion del arco,
    // que es geometria de la RUTA. Ninguna de las dos mira la batalla, asi que el optimo
    // entre ellas tampoco.
    //
    // ✔ PERO LA BATALLA QUEDA COMO TECHO, y eso preserva la invariante del bloque de arriba:
    // la ventana local tiene que ser mas corta que el Ld por construccion, si no deja de ser
    // local. Con piso de Ld en 2 batallas, un vehiculo de batalla < 1.5 m tendria el Ld por
    // DEBAJO de 3.0 y la "local" seria mas larga que el horizonte. Con el techo eso no puede
    // pasar, y ademas la forma reproduce EXACTAMENTE los dos optimos medidos:
    //
    //    bus   batalla 5.35 > 3.0  ->  3.00 m   (medido mejor: 3.0, pico 1.904)
    //    sedan batalla 2.93 < 3.0  ->  2.93 m   (medido mejor: 2.93, pico 1.272)
    //
    // ⚠ Y LO QUE SIGUE SIENDO PER-VEHICULO ES EL CLAMP de MaxCurvature (steerMax +
    // understeerK). Esa es la unica capa que adapta la ley a cada vehiculo, y es la que hay
    // que probar con uno nuevo -- no esta ventana.
    private static const float VENTANA_FF_M = 3.0;

    private static float VentanaLocalM(BZVehicleManual m)
    {
        // Banco: ver s_VentanaFFM. Ausente (0) = la constante universal, acotada por la batalla.
        if (s_VentanaFFM > 0.2) return s_VentanaFFM;
        if (!m || m.wheelbaseM < 0.5) return LD_FLOOR_M;
        if (m.wheelbaseM < VENTANA_FF_M) return m.wheelbaseM;
        return VENTANA_FF_M;
    }
    // Velocidad grabada por debajo de la cual el dato NO sirve como objetivo.
    private const float REC_USABLE_MS = 0.5;

    private ref BZPathSample m_S;
    private ref BZPathSample m_S0;
    private ref BZPathSample m_SArco;
    // ⚠ Scratch PROPIO del sondeo de corte. Reusar m_S0/m_SArco lo ataria a que nadie mas
    // los toque en el mismo tick, que es justo la clase de acoplamiento que ya nos mordio.
    private ref BZPathSample m_SCorteA;
    private ref BZPathSample m_SCorteB;
    // ⚠ ESTE MODULO SE DECLARA SIN ESTADO Y ESTA ES LA EXCEPCION, CON SU RAZON.
    //
    // La SONDA FF suma al comando el deficit que pure pursuit no anticipa:
    //
    //      k_cmd = k_pp + (PIDE_local - PIDE_arco)
    //
    // Se agrega SOLO EL DEFICIT, no la señal entera: pure pursuit NO vale cero sobre la
    // traza --su salida ya es ~PIDE_arco-- asi que sumarle PIDE_local encima daria
    // 1.59 * k_local, un sobre-comando del 59%. Medido: el deficit hoy es
    // |COMANDA|/|PIDE_local| = 0.586 (bus) y 0.594 (sedan), o sea la ley comanda el 59% de
    // lo que el camino pide localmente, IGUAL EN LOS DOS -- es una propiedad de la FORMA.
    //
    // ✔ En curva de radio CONSTANTE y en recta el termino vale EXACTAMENTE CERO, porque
    //   CurvaturaDelArco es dPsi/d y para curvatura constante eso da k para cualquier d.
    //   Es el invariante que asserta verify_kcurva.
    //
    // ✔ NO sube la ganancia del lazo: depende del CAMINO en la posicion del vehiculo, no
    //   del error del vehiculo. Por eso no reproduce `AcortarPorGiro` (ver el epitafio).
    //
    // ⚠ LA GUARDA DEL CURSOR ES LA CONDICION PARA QUE ESO VALGA. Si el cursor se congela
    //   --paso en wp 724-- `PIDE_local` se queda en un valor viejo y el feedforward lo
    //   mandaria al volante. Con la guarda, la sonda DEGRADA A PURE PURSUIT PURO en vez de
    //   amplificar un dato muerto.
    //
    //   Medido sobre las corridas del PBO 16:47, por banda de velocidad: arriba de 8 km/h
    //   el cursor NO se queda quieto NUNCA (0 de 312 ticks en el bus, 0 de 389 en el sedan).
    //   Abajo de 8 se queda quieto entre el 11% y el 57% de los ticks, legitimamente --el
    //   vehiculo casi no avanza-- y ahi apagar el feedforward es lo conservador y correcto.
    //   O sea que la guarda NO parpadea en el regimen donde la sonda tiene algo que hacer.
    private float m_FFDeficit;
    private int   m_WpPrevFF;
    private bool  m_FFCongelado;
    private bool  m_FFEnRecta;
    private bool  m_FFEnFinal;

    void BZPurePursuit()
    {
        m_S = new BZPathSample();
        m_S0 = new BZPathSample();
        m_SArco = new BZPathSample();
        m_SCorteA = new BZPathSample();
        m_SCorteB = new BZPathSample();
        m_SMira40 = new BZPathSample();
        m_SFP0 = new BZPathSample();
        m_SFP1 = new BZPathSample();
        m_SFP2 = new BZPathSample();
        m_WpPrevFF = -9999;
        m_StopServidoWp = -1;
    }

    void ComputeGoal(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind, BZDriveGoal g)
    {
        g.Reset();
        if (!leg) return;
        if (!leg.valid)
        {
            g.halt = true;   // sin contexto no se inventa una trayectoria
            return;
        }
        g.dir = 1;
        if (leg.isReverse) g.dir = -1;
        float decel = m.MaxBrakeDecel(surfKind);
        m_ALatToma = leg.ALatToma();
        m_ALatDir = leg.ALatDir();
        m_DistAlFin = leg.remainM;
        m_EnRampaFinal = DetectarRampaFinal(leg, m);
        // ⚠ LA MORDIDA ES DEL VEHICULO, NO DEL WAYPOINT. Se calculaba por muestra pasando
        // la velocidad GRABADA de esa muestra, asi que la relacion engranada salia distinta
        // en cada una y el objetivo bailaba: 11.48 en un wp y 9.52 --por debajo de la
        // mordida-- en el siguiente. Depende de la marcha que el vehiculo tiene PUESTA.
        // ...y la marcha PUESTA: ver el bloque en SpeedAtBiteKmh. Sin ella, en reversa
        // se devolvia la mordida de primera y el objetivo se iba 52% arriba.
        m_MordidaKmh = m.SpeedAtBiteKmh(st.chassisKmh, st.rpm, st.gear);
        // ⚠⚠ EN LA RAMPA FINAL LA MORDIDA ES LA DEL TRAMO, no la de la marcha PUESTA.
        // El detector (DetectarRampaFinal) pregunta por la marcha del tramo (1ra, o
        // reversa); el floor usaba esta de arriba -- y con una marcha residual engranada
        // en la aproximacion daba 19.8-34 km/h (RPT 09-09: "mordida a 34.07 [gear 6]")
        // contra 10.6 grabado: perseguir un imposible = los pasitos que el brake-limpio
        // no mato. Detector y floor ahora consultan LA MISMA mordida.
        if (m_EnRampaFinal)
        {
            int gTramoRF = 2;
            if (leg.isReverse) gTramoRF = 0;
            m_MordidaKmh = m.SpeedAtBiteKmh(0, 0, gTramoRF);
        }
        if (decel < 0.5) decel = 0.5;
        g.decelMs2 = decel;
        m_PosPlan = st.pos;
        m_PosPlanValida = true;
        g.curvature = ComputeCurvature(st, leg, m);
        g.curvatureFF = m_LastKArco;   // ver BZDriveGoal.curvatureFF
        g.speedMs = ComputeSpeedMs(leg, m, decel, surfKind);
        // Banco: ver s_TopeVelMs. Techo, no objetivo -- nunca sube lo que el perfil pide.
        if (s_TopeVelMs > 0.05 && g.speedMs > s_TopeVelMs) g.speedMs = s_TopeVelMs;
        if (leg.remainM <= 0.05 && leg.fwdM <= 0.05) g.halt = true;
    }

    // ======================= direccion =======================

    // Pure pursuit clasico: kappa = 2 sin(alpha) / Ld, con alpha = angulo entre el rumbo
    // de MARCHA y la recta al punto de lookahead. En reversa el rumbo de marcha es
    // yaw + PI (el auto va para atras), asi que la misma formula sirve sin casos especiales.
    private float ComputeCurvature(BZVehicleState st, BZLegView leg, BZVehicleManual m)
    {
        float ld = LookaheadM(st, leg, m);
        if (!leg.SampleAt(ld, m_S)) return 0;
        m_LastLd = ld;
        m_LastKArco = CurvaturaDelArco(leg, ld);
        m_LastKLocal = CurvaturaDelArco(leg, VentanaLocalM(m));
        // La guarda: el termino solo vive si el cursor se movio en este tick de Plan.
        // `!=` y no `>` a proposito -- al cambiar de leg nearWp se reinicia y eso NO es un
        // cursor congelado. Ver el bloque de arriba para la medicion que lo respalda.
        m_FFDeficit = 0;
        m_FFCongelado = (leg.nearWp == m_WpPrevFF);
        m_WpPrevFF = leg.nearWp;
        float kMayor = Math.AbsFloat(m_LastKArco);
        if (Math.AbsFloat(m_LastKLocal) > kMayor) kMayor = Math.AbsFloat(m_LastKLocal);
        m_FFEnRecta = (kMayor < FF_ZONA_MUERTA);
        // ⚠⚠ EN BATALLAS, **NO** EN VENTANAS DE FF. Lo escribi con VentanaLocalM(m) y era
        // al reves de lo que hace falta: con la ventana forzada a 2.0 la proteccion se
        // encogia de 21.4 m a 8 m, justo en la corrida que MAS ruido tiene. La compuerta
        // protege una zona del CAMINO y del vehiculo, no de la perilla que se esta barriendo.
        float wbOff = m.wheelbaseM;
        if (wbOff < 0.5) wbOff = LD_FLOOR_M;
        m_FFEnFinal = (leg.endIsStop && leg.remainM < FF_OFF_WB * wbOff);
        // ⚠⚠⚠ DOS COTAS, Y LA QUE MANDA ES LA MENOR.
        //
        // `MaxCurvature` mide cuanto pueden GIRAR LAS RUEDAS: `tan(steerMax)*k(v)/batalla`.
        // A alta velocidad ese no es el limite -- el limite lo pone el AGARRE:
        //
        //     kappa_agarre = aLat / v^2
        //
        // Medido: a 122 km/h el agarre permite kappa 0.0218 (volante
        // 0.089) y el controlador comandaba 0.42 en el onset y 0.96 en el pico: entre 5 y 11
        // veces mas. **Comandar mas curvatura de la que las gomas dan no dobla el auto: lo
        // hace DESLIZAR.** Y deslizar es el cruce que se midio -- el lazo `str sube -> alpha
        // crece -> str sube mas` arranca porque el PRIMER comando ya era imposible.
        //
        // ★ Y se auto-desactiva donde no hace falta: cae con v^2, asi que recien empieza a
        // atar cerca de los 40 km/h. La flota lenta y las maniobras quedan intactas POR
        // CONSTRUCCION, sin una compuerta que alguien tenga que acordarse de poner.
        //
        // ★ El numero NO es nuevo: es el mismo `aLatToma` que destrabo la velocidad del
        // Agera, o sea el agarre que ESA toma demostro. Mismo dato, los dos
        // lados del problema: cuanto se puede correr en curva, y cuanto se puede girar.
        //
        // ✖ Nunca AFLOJA la cota de hoy: se toma el minimo de las dos.
        m_KMaxCmd = m.MaxCurvature(st.chassisKmh) * (1.0 - MARGEN_AUTORIDAD);
        // ✖ Solo con agarre MEDIDO. Si la toma no lo trae (`m_ALatToma` 0), no se inventa
        // un numero: queda la cota de siempre. Misma asimetria que el resto -- lo medido
        // puede APRETAR, la falta de medicion no afloja ni inventa.
        float vMs = st.chassisKmh / 3.6;
        // ⚠ El cap de DIRECCION usa el p95, no el p90 del perfil de velocidad. Ver
        // BZRouteData.m_ALatDir: mismo dato, percentiles distintos, porque ser conservador
        // aca VETA el giro que la toma tiene, mientras que en la velocidad es seguro.
        // ★★★ EL CAP DE DIRECCION POR BANDA DE VELOCIDAD GRABADA (paso 2, ).
        // Medido: en el onset del GT2RS a 177 km/h COMANDA quedo CLAVADA en kmax con K-CLAMP.
        // El p95 escalar de la toma es 7.83 m/s2 (= el clamp) y la maniobra pedia 8.6: el
        // escalar corta exactamente el 5% superior, que es donde vive esa curva. El p95 de la
        // BANDA 160-180 es 11.38. Se evalua a la velocidad GRABADA del sample (no a la actual:
        // seria circular) y nunca baja del escalar. p95->max se descarto: el max sigue con
        // outliers (51.6 Agera, 34.1 Huracan).
        float aDir = m_ALatDir;
        if (m_S.recSpeedKmh > 1.0) aDir = leg.ALatDirEn(m_S.recSpeedKmh);
        if (vMs > 1.0 && aDir > 0.1)
        {
            // ⚠⚠⚠ PISO FISICO DE RECUPERACION. El p95 de la banda
            // grabada dice cuanto GIRO la toma a esa velocidad -- en el bus, la banda
            // 60-80 son tramos suaves y da 2.87 m/s2 -> kmax 0.0085 a 66 km/h (R=117 m).
            // Medido: en una RECTA (kappa grabada 0.0000) un golpe externo de 4.6 m/s2
            // (LOGRA +0.0136 con el volante en -0.003) tiro el bus a lat -2.19; el
            // corrector pidio -0.0272 y COMANDA/COMANDA40 quedaron CLAVADAS en -0.0086
            // dos ticks: autoridad < perturbacion = lazo abierto. El evento "wp1066
            // lat 3.10 @ 33" era lo mismo a otra velocidad (el cap cae con 1/v^2; por
            // eso la Toyota a 18-23 nunca lo toca). El cap sigue vetando al FF, pero
            // al CORRECTOR no se le puede negar el agarre fisico del vehiculo por lo
            // mansa que fue la toma: piso 5.0 m/s2 (0.51g, cubre el golpe medido y es
            // ~1/2 del que salvo al GT2RS: escalar 7.83 / banda 11.38 -- inerte alli).
            // ✖ SOLO dentro de esta rama: sin medicion (aDir 0) NO se inventa cap.
            if (aDir < ALAT_DIR_PISO) aDir = ALAT_DIR_PISO;
            float kAgarre = aDir / (vMs * vMs);
            if (kAgarre < m_KMaxCmd) m_KMaxCmd = kAgarre;
        }
        if (!m_FFCongelado && !m_FFEnRecta && !m_FFEnFinal)
        {
            m_FFDeficit = m_LastKLocal - m_LastKArco;
        }
        // ★★★ N2: con la bandera, la ley FF-primaria REEMPLAZA a la persecucion en
        // forward. m_LastKArco pasa a ser MI kFF -> el split de los dos relojes congela
        // (embrague) exactamente MI corrector, sin tocar a los escritores.
        if (s_FFPrimario && !leg.isReverse)
        {
            bool fpOk = false;
            float kN = LeyFFPrimario(leg, m, 0.0, st.pos, st.courseRad, st.chassisKmh, fpOk);
            if (fpOk)
            {
                m_LastKArco = m_FFPKff;
                m_FFDeficit = 0;
                m_LastAim = m_S.pos;
                m_AimValido = true;
                // kN ya viene acotado POR COMPONENTE desde la ley (FF->toma,
                // corrector->presupuesto, total->ruedas): re-acotar el total aqui
                // re-estrangularia al corrector con el techo del FF.
                return kN;
            }
        }
        // ✖✖ SEGUNDO ENTIERRO DEL SESGO DE SAGITA. La mira es el sample
        // PELADO. El epitafio de abajo ya habia probado que sobre el arco la pura
        // persecucion comanda EXACTO 1/R (sin sesgo estacionario que compensar) y la
        // flecha se borro... y resucito como 'MiraConSesgo'. Esta vez con la medicion
        // in-vivo (RPT 16-56-03, bus, 1739 ticks): 87% de los ticks de curva con lat
        // hacia AFUERA, |lat| ~ 1.7x|sesgo|, CMD/PIDE_local 0.43 con LOGRA/CMD 1.06.
        // Correr la mira hacia afuera le ROBA alpha al lazo y el equilibrio se paga
        // con el vehiculo afuera de la linea: y = dk*Ld^2/2. NO REINTRODUCIR sin
        // refutar las DOS mediciones (la geometrica del epitafio y esta).
        vector mira = m_S.pos;
        m_LastAim = mira;
        m_AimValido = true;
        // Fix A: en reversa la geometria se mide desde el eje lider. Ver PosReferencia.
        vector pref = PosReferencia(st.pos, st.yaw, leg.isReverse, m);
        float dx = mira[0] - pref[0];
        float dz = mira[2] - pref[2];
        float dist = Math.Sqrt(dx * dx + dz * dz);
        if (dist < 0.10) return 0;
        float refYaw = st.yaw;
        if (leg.isReverse) refYaw = refYaw + Math.PI;
        float alpha = BZMath.WrapPi(BZMath.HeadingXZ(dx, dz) - refYaw);
        // ⚠⚠⚠ EL DENOMINADOR ES LA ANTICIPACION, NO LO QUE QUEDA HASTA EL PUNTO.
        //
        // El punto de mira se fija en Plan (2 Hz) y esta curvatura se recalcula a 40 Hz. Con
        // `dist` en el denominador, entre refresco y refresco el vehiculo le come camino al
        // punto y LA GANANCIA SUBE SOLA, sin que nada la pida: a 65 km/h son 9 m por
        // intervalo, y con Ld 16.1 eso lleva `dist` de 16.1 a 7.1 -> x2.28.
        //
        // Medido:
        //     str -0.37 -> -0.76  =  x2.05     contra el x2.28 que predice el colapso.
        // El lateral se fue de 0.50 a -2.15 con el volante casi al tope, sin curva que lo
        // explicara. Y solo les pasaba a los rapidos porque despacio el vehiculo apenas se
        // acerca al punto entre planes (x1.2 en vez de x2.3).
        //
        // ★ NO se congela la correccion: `alpha` se sigue recalculando con la pose, que es
        // toda la razon de ser del refresco de 40 Hz. Lo unico que se saca es la rampa 1/d.
        //
        // ✖ Y si el punto quedo MAS LEJOS que el Ld, manda la distancia real: no se inventa
        // una cercania que no hay. Por eso es el MAYOR de los dos.
        float den = m_LastLd;
        if (dist > den) den = dist;
        if (den < 0.10) return 0;
        m_DiagAlpha = alpha;
        m_DiagDist = dist;
        m_DiagDen = den;
        return AcotarACapacidad(2.0 * Math.Sin(alpha) / den + m_FFDeficit);
    }

    // ✖ ACA VIVIA LA COMPENSACION DE FLECHA, Y SE BORRO PORQUE LA PREMISA ERA FALSA.
    //
    // Decia: "persiguiendo un punto a Ld de ARCO el vehiculo recorre la CUERDA y queda por
    // dentro Ld^2/(8R)". Es falso. Sobre un arco, con el vehiculo SOBRE la traza y alineado,
    // la cuerda al punto de mira vale d = 2R*sin(phi/2) y el angulo tangente-cuerda es
    // exactamente phi/2, asi que
    //          kappa = 2*sin(alpha)/d = 2*(d/2R)/d = 1/R
    // EXACTO, y para CUALQUIER Ld. Pure pursuit comanda justo la curvatura del camino: no
    // hay sesgo estacionario que compensar.
    //
    // Verificado sobre la geometria REAL de AERO (8 puntos x 2 horizontes): la razon
    // comandada/traza con el vehiculo perfecto da mediana 0.978.
    //
    // Lo unico que hacia la flecha era RESTAR curvatura -- corre la mira hacia afuera y baja
    // alpha. En wp953 de AERO eran 0.79 m sobre Ld 15.8 = ~25% menos de curvatura, y
    // justamente ahi el vehiculo ya venia cortando por adentro (lat +0.85 en un giro a
    // izquierda). O sea que empeoraba lo que decia arreglar.
    //
    // El sobre-vire de wp113 lo arreglo BORRAR el tope de Ld, no agregar la flecha: viajaron
    // en el mismo PBO y se le atribuyo el merito al pasajero.
    //
    // La curvatura POR ARCO se queda: no para corregir, para MEDIR. Es la unica lectura de
    // curvatura que no es ruido (la de a un waypoint alterna de signo casi en cada punto).
    // ⚠⚠ ESTA FUNCION NO RE-MUESTREABA, Y POR ESO PIDE_local NUNCA MIDIO LO LOCAL.
    //
    // Usaba `m_S`, que lo deja fijado el llamador EN EL LOOKAHEAD, y lo dividia por la
    // ventana que le pasaban. O sea que para cualquier ventana devolvia el MISMO giro
    // dividido por otro numero:
    //
    //      PIDE_local  ==  PIDE_arco * Ld / batalla        <- identidad, no correlacion
    //
    // Medido sobre las corridas: la razon da 1.0000 con p10 = p90 = 1.000 en el bus (n=125)
    // y 0.9997 en el sedan (n=157). No es una aproximacion: es la misma cuenta reescalada.
    //
    // El dano fue de DIAGNOSTICO, no de manejo -- `LastKLocal()` solo alimenta la linea de
    // log. Pero costo caro igual: concluyo "la ventana diluye la curva a la mitad"
    // (0.500 en el bus) cuando ese 0.500 era `batalla/Ld` = 5.35/10.70, una constante
    // geometrica que no dice nada del camino. **Un instrumento que devuelve una identidad
    // se lee como un hallazgo.**
    //
    // `PIDE_arco` nunca estuvo mal: ahi ld == el ld del llamador, asi que la muestra reusada
    // era la correcta por casualidad. Todo lo que se concluyo con PIDE_arco sigue en pie.
    // El techo, en un solo lugar: los dos caminos --Plan y el refresco de 40 Hz-- tienen que
    // pasar por aca o el clamp se aplica a medias. Es la misma leccion que la sonda: el
    // volante NO sale de `g.curvature`, sale de CurvaturaDesdePose.
    private float AcotarACapacidad(float k)
    {
        m_KClampMordio = false;
        if (m_KMaxCmd <= 0.001) return k;
        if (k > m_KMaxCmd)
        {
            m_KClampMordio = true;
            return m_KMaxCmd;
        }
        if (k < -m_KMaxCmd)
        {
            m_KClampMordio = true;
            return -m_KMaxCmd;
        }
        return k;
    }

    float KMaxCmd() { return m_KMaxCmd; }

    bool KClampMordio() { return m_KClampMordio; }

    private float CurvaturaDelArco(BZLegView leg, float ld)
    {
        if (ld < 0.5) return 0;
        if (!leg.SampleAt(0.0, m_S0)) return 0;
        if (!leg.SampleAt(ld, m_SArco)) return 0;
        return BZMath.WrapPi(m_SArco.headingRad - m_S0.headingRad) / ld;
    }

    // ⚠ EL PUNTO DE MIRA LO FIJA Plan(); ESTO SOLO LO RE-APUNTA CONTRA LA POSE DE AHORA.
    // El punto no se mueve entre ticks de Plan (el cursor avanza alla), asi que esto no
    // adelanta el horizonte ni cambia a donde va el vehiculo: corrige el ANGULO con el que
    // lo persigue, que es el termino que se desactualiza rapido.
    //
    // Cuanto se desactualiza, medido en AERO: entre dos updates de Plan el vehiculo recorre
    // 43% del radio en un apice (R~7.6 m), 23% en curva media, y 1% en recta. Por eso en
    // ★★★ N2 -- LA LEY FF-PRIMARIA. y y psi FRESCOS contra la traza en el arco del
    // vehiculo (la formula del cursor de LegMachine, interpolada), kFF = curvatura
    // GRABADA un lead adelante. Convenciones verificadas contra el fuente: curvatura
    // + = DERECHA (Atan2 horario), y + = IZQUIERDA (producto cruz del cursor),
    // psi = ref - yaw (a la izquierda del rumbo da +). Vehiculo a la izquierda o
    // apuntando a la izquierda => correccion + (derecha): k = kFF + KY*y + KPSI*psi,
    // KY = w^2/v^2, KPSI = 2*zeta*w/v (polos del modelo de camino en w, zeta).
    // SOLO FORWARD: la reversa conserva la persecucion (su marco de signos es otro y
    // su regimen lo dominan los modulos POSE).
    private float LeyFFPrimario(BZLegView leg, BZVehicleManual m, float avance, vector pos, float yaw, float chassisKmh, out bool valido)
    {
        valido = false;
        if (!leg.SampleAt(avance, m_SFP0)) return 0;
        // ⚠⚠⚠ y Y psi VAN CONTRA LA TANGENTE DE POSICIONES, NO CONTRA EL RUMBO GRABADO.
        // El yaw grabado (col15) difiere de la tangente del camino hasta 9 grados p90 en
        // curva (MEDIDO en la ruta del bus): es la DERIVA del humano -- su trompa no
        // apunta por donde va el auto. Referenciar psi a col15 inyecta ese beta como
        // correccion espuria via KPSI (el banco lo mostro: p95 se clavaba en 0.31-0.56 y
        // NO escalaba con omega, porque KPSI escala igual). La direccion sale de DOS
        // posiciones de la traza a +-FFP_TAN_M; el kFF SI queda en col15 (ventana), que
        // para curvatura es la fuente sin fantasmas (esa doctrina no cambia).
        float aT0 = avance - FFP_TAN_M;
        if (aT0 < 0) aT0 = 0;
        float aT1 = avance + FFP_TAN_M;
        if (leg.fwdM > 0.05 && aT1 > leg.fwdM) aT1 = leg.fwdM;
        if (aT1 - aT0 < 0.5) return 0;
        if (!leg.SampleAt(aT0, m_SFP1)) return 0;
        if (!leg.SampleAt(aT1, m_SFP2)) return 0;
        float hseg = BZMath.HeadingXZ(m_SFP2.pos[0] - m_SFP1.pos[0], m_SFP2.pos[2] - m_SFP1.pos[2]);
        float ux = Math.Sin(hseg);
        float uz = Math.Cos(hseg);
        // ★ EJE TRASERO: la referencia es un CUERPO, no un
        // punto. Vehiculo Y referencia se corren media batalla atras (d_r = wb/2 -- lo
        // que la REVERSA ya hace con GetReverseControlOffset): el off-tracking del tren
        // trasero (bus: 0.7-1.0 m en R15-25) queda absorbido porque el humano ya lo
        // grabo -- pos+rumbo fijan el rigido y el eje trasero se reconstruye exacto.
        float dr = 0;
        if (m && m.wheelbaseM > 0.5) dr = m.wheelbaseM * 0.5;
        float rvx = pos[0] - Math.Sin(yaw) * dr;
        float rvz = pos[2] - Math.Cos(yaw) * dr;
        float rrx = m_SFP0.pos[0] - Math.Sin(m_SFP0.headingRad) * dr;
        float rrz = m_SFP0.pos[2] - Math.Cos(m_SFP0.headingRad) * dr;
        float y = ux * (rvz - rrz) - uz * (rvx - rrx);
        // ⚠ psi contra el CURSO del vehiculo (el llamador pasa courseRad, no yaw): el
        // sideslip de Boris tampoco entra al corrector -- simetrico al fix del lado
        // referencia (tangente de posiciones vs col15).
        float psi = BZMath.WrapPi(hseg - yaw);
        float vMs = chassisKmh / 3.6;
        if (vMs < FFP_VMIN_MS) vMs = FFP_VMIN_MS;
        float lead = vMs * FFP_LEAD_S;
        // ⚠⚠ EL kFF NO SALE DEL CAMPO curvature DEL SAMPLE: ese es dh/ds POR WP
        // (ComputeCurvature de RouteData) y ALTERNA DE SIGNO 1.39/s en la ruta real del
        // bus -- MAS nervioso que el Boris que venimos a calmar (medido antes de escribir
        // esta ley; el epitafio de la flecha ya lo decia: "la de a un waypoint alterna de
        // signo casi en cada punto"). Se computa POR VENTANA DE ARCO, el estimador que el
        // motor ya considera confiable (el mismo patron que PIDE_local).
        float w = VentanaLocalM(m);
        if (w < 0.5) return 0;
        float a0 = avance + lead - w * 0.5;
        if (a0 < 0) a0 = 0;
        float a1 = a0 + w;
        // SampleAt clampea EN SILENCIO en los bordes: la guarda contra fwdM es explicita
        if (leg.fwdM > 0.05 && a1 > leg.fwdM)
        {
            a1 = leg.fwdM;
            a0 = a1 - w;
            if (a0 < 0) a0 = 0;
        }
        if (a1 - a0 < 0.5) return 0;
        if (!leg.SampleAt(a0, m_SFP1)) return 0;
        if (!leg.SampleAt(a1, m_SFP2)) return 0;
        m_FFPKff = BZMath.WrapPi(m_SFP2.headingRad - m_SFP1.headingRad) / (a1 - a0);
        float ky = (FFP_OMEGA * FFP_OMEGA) / (vMs * vMs);
        float kpsi = 2.0 * FFP_ZETA * FFP_OMEGA / vMs;
        // deadband C1: f(x) = x*|x|/(|x|+eps) -- suave en 0, asintota x-eps*signo. Va
        // SOLO en el corrector; el FF viaja integro.
        float ay = Math.AbsFloat(y);
        float ap = Math.AbsFloat(psi);
        m_FFPYdb = y * ay / (ay + FFP_EPS_Y);
        m_FFPCorr = ky * m_FFPYdb + kpsi * (psi * ap / (ap + FFP_EPS_PSI)) + m_FFPTrim;
        // ⚠⚠⚠ CAP POR HEADROOM DE RUEDA. El humano
        // tomo los giros trabados al 99% del LOCK: en el NUCLEO el corrector no tiene
        // margen y debe callarse (asi murio el thrash: corrP -0.115, str 0.96 -> -0.28).
        // ✖ PERO el margen medido por kFF (kRuedas - |kFF|) REGRESO LA SALIDA
        // (1.37 -> 2.29 m, sesgo a la derecha que el usuario vio): en la recuperacion el kFF
        // de la curva de salida sigue grande y el cap estrangulaba al corrector con el
        // volante REAL a 0.29-0.5 -- margen fisico de sobra. Y si el corrector OPONE
        // al FF, el clamp del total (542) ya acota solo: aquel cap simetrico era
        // perdida pura ahi. El discriminante es EL VOLANTE APLICADO:
        //     cap = kRuedas * (1 - |volante|)
        // nucleo (str 0.96) -> 0.004 en ambos sentidos (mudo, thrash muerto, no
        // suelta el giro); salida (str 0.3-0.5) -> 0.05-0.07 (recuperacion fuerte y
        // AUTO-LIMITADA: al corregir la rueda sube y el cap baja -- equilibrio suave
        // cerca del lock, sin slam); a velocidad (str chico) -> ~kRuedas > presupuesto
        // 10/v^2 (INERTE). Proporcional, sin flap binario (el falsador del relay:
        // el humano al lock deja str<1 por momentos -- el cap respira con el).
        float libreArr = 1.0 - Math.AbsFloat(m_VolanteAplicado);
        if (libreArr < 0) libreArr = 0;
        if (m)
        {
            float capLock = m.MaxCurvature(chassisKmh) * (1.0 - MARGEN_AUTORIDAD) * libreArr;
            if (m_FFPCorr > capLock) m_FFPCorr = capLock;
            if (m_FFPCorr < -capLock) m_FFPCorr = -capLock;
        }
        // ★★★ CAP POR COMPONENTE.
        // El clamp del TOTAL estrangulaba al corrector con el mismo techo que veta al
        // FF: con piso 5.0, en los baches a 47-62 km/h la demanda real fue a_lat
        // 5.8-9.7 (corrP 0.0307/0.0582/0.0257 con kmax clavado en 5/v^2) y el bus se
        // fue a 2.6-3.0 m; wp512 mismo lugar/velocidad dio 0.03 y 1.48 en dos runs
        // (MARGINAL). Cada componente lleva SU cap: el FF la capacidad de la toma
        // (sigue la linea grabada, no necesita mas que lo que la toma demostro); el
        // corrector su presupuesto de RECUPERACION (cubre demanda y golpe medidos); y
        // la RED DURA unica es la de ruedas. El GT2RS-class sigue protegido: FF atado
        // a su toma, corrector acotado a a_lat 10 (el runaway medido comandaba a_lat
        // de CIENTOS -- 0.42 a 122 km/h = 480). Los miembros quedan con los valores
        // APLICADOS: el split de embrague y la columna corrP siguen exactos.
        // Falsador (el B del 538 sigue armado): weaveo/deslizada en recuperacion ->
        // bajar el presupuesto o gatearlo por superficie, NO volver al clamp del total.
        m_KClampMordio = false;
        if (m_KMaxCmd > 0.001)
        {
            if (m_FFPKff > m_KMaxCmd)  { m_FFPKff = m_KMaxCmd;  m_KClampMordio = true; }
            if (m_FFPKff < -m_KMaxCmd) { m_FFPKff = -m_KMaxCmd; m_KClampMordio = true; }
        }
        float vRe = chassisKmh / 3.6;
        if (vRe > 1.0)
        {
            float capCorr = CORR_ALAT_RECUP / (vRe * vRe);
            if (m_FFPCorr > capCorr)  { m_FFPCorr = capCorr;  m_KClampMordio = true; }
            if (m_FFPCorr < -capCorr) { m_FFPCorr = -capCorr; m_KClampMordio = true; }
        }
        valido = true;
        float kTot = m_FFPKff + m_FFPCorr;
        if (m)
        {
            float kRuedas = m.MaxCurvature(chassisKmh) * (1.0 - MARGEN_AUTORIDAD);
            if (kRuedas > 0.001)
            {
                if (kTot > kRuedas)  { kTot = kRuedas;  m_KClampMordio = true; }
                if (kTot < -kRuedas) { kTot = -kRuedas; m_KClampMordio = true; }
            }
        }
        return kTot;
    }

    float LastFFPCorr() { return m_FFPCorr; }

    // Instrumento del falsador B: corrP amasa proporcional+trim y no los
    // separa; con esta columna el RPT decide directo si el trim clavo en su tope.
    float LastFFPTrim() { return m_FFPTrim; }

    void SetVolanteAplicado(float s) { m_VolanteAplicado = s; }

    // UN STOP SERVIDO NO PIDE 0. Lo marca el controlador al salir de un hold (ver
    // BZDriveController.MarcarStopServido) y lo olvida cuando el cursor lo pasa; -1 = ninguno.
    void SetStopServido(int wp) { m_StopServidoWp = wp; }

    // recta esto no puede cambiar nada y en el apice lo cambia todo.
    // ⚠⚠⚠ EL PUNTO DE MIRA AVANZA A 40 Hz. Esto CRUZA EL SEAM, con OK explicito del usuario.
    //
    // Hasta aca el aim se fijaba en Plan (2 Hz) y se consumia a 40. Entre refresco y
    // refresco el vehiculo le comia camino --a 65 km/h, 9 m-- y eso es medio segundo de
    // RETARDO PURO. Y el retardo, no la ganancia, es lo que se come el margen de fase:
    //
    //     margen(Ld) = 65 grados - degrees(v*sqrt(2)/Ld * T)
    //
    // Con T = 0.5 s todo lo rapido vivia clavado en +20 grados --el minimo que reserva el
    // piso--. Con T = 1/40 s pasa a ~+63, y deja de depender de la velocidad. La distancia
    // de asentamiento a 65 km/h cae de 87 m a 43 m.
    //
    // ✖ NO se recalcula el Ld: sigue saliendo del Plan, con su techo de corte y su piso.
    // Lo unico que se mueve es DONDE mira, no CUAN LEJOS.
    //
    // ✖ Y devuelve el sample PELADO, igual que el Plan: con avance 0 el aim es IDENTICO.
    //
    // Tres guardas, cada una con su mutante:
    //   - fin de ventana: ⚠ `SampleAt` NO falla afuera -- CLAMPEA y devuelve true igual.
    //     Un sample clampeado vuelve pegado al borde, mas cerca que Ld, y le aplicariamos
    //     el sesgo de un Ld que no corresponde. Por eso la guarda es EXPLICITA contra fwdM.
    //   - salto (respawn/teleport): mas de AVANCE_MAX_M no se extrapola.
    //   - quieto: por debajo de AVANCE_MIN_M el punto del Plan ya sirve.
    void RefrescarMiraA40(BZLegView leg, BZVehicleManual mnl, vector pos, float yaw, bool esReversa, float chassisKmh, float dt)
    {
        // N2: el flag de esta pasada se invalida SIEMPRE al entrar -- un k40 rancio de
        // otro tick seria el mismo bug del retardo que este refresco vino a matar.
        m_FFPk40Valido = false;
        if (!m_AimValido) return;
        if (!m_PosPlanValida) return;
        if (!leg) return;
        if (!leg.valid) return;
        if (m_LastLd < 0.5) return;
        float dx = pos[0] - m_PosPlan[0];
        float dz = pos[2] - m_PosPlan[2];
        float avance = Math.Sqrt(dx * dx + dz * dz);
        if (avance < AVANCE_MIN_M) return;
        if (avance > AVANCE_MAX_M) return;
        if (leg.fwdM > 0.05 && avance + m_LastLd > leg.fwdM) return;
        if (!leg.SampleAt(avance + m_LastLd, m_SMira40)) return;
        m_LastAim = m_SMira40.pos;   // sample PELADO: ver el segundo entierro del sesgo
        // ★★★ N2 a 40 Hz: misma ley, avance fresco. CurvaturaDesdePose la consume.
        if (s_FFPrimario && !esReversa)
        {
            bool fp40 = false;
            float k40 = LeyFFPrimario(leg, mnl, avance, pos, yaw, chassisKmh, fp40);
            if (fp40)
            {
                m_FFPk40 = k40;
                m_FFPk40Valido = true;
                // Integrador del TRIM: solo con la ley valida y SIN clamp mordido
                // (anti-windup: integrar contra un tope es acumular deuda falsa).
                // ⚠ El integrador respira por el MISMO headroom de rueda que el
                // corrector (; reemplaza el gate por VMIN del 547, que el
                // relay señalo como co-culpable de la recuperacion estrangulada):
                // donde el volante no puede actuar no se acumula (la basura del
                // thrash al lock, medida: +0.010 peleando -0.115); donde puede,
                // el trim ayuda a la recuperacion como en el build del 546.
                if (!m_KClampMordio && dt > 0.0001)
                {
                    float libreTrim = 1.0 - Math.AbsFloat(m_VolanteAplicado);
                    if (libreTrim < 0) libreTrim = 0;
                    // KI v-scheduleado como KY/KPSI (ver la constante): gain de lazo
                    // invariante -> estable a toda velocidad, identico a 50 km/h.
                    float vTr = chassisKmh / 3.6;
                    if (vTr < FFP_VMIN_MS) vTr = FFP_VMIN_MS;
                    m_FFPTrim = m_FFPTrim + (FFP_TRIM_KI_ALAT / (vTr * vTr)) * m_FFPYdb * dt * libreTrim;
                    m_FFPTrim = Math.Clamp(m_FFPTrim, -FFP_TRIM_MAX, FFP_TRIM_MAX);
                }
            }
            else
            {
                m_FFPTrim = m_FFPTrim * FFP_TRIM_DECAY;
            }
        }
        else
        {
            m_FFPTrim = m_FFPTrim * FFP_TRIM_DECAY;
        }
    }

    float CurvaturaDesdePose(vector pos, float yaw, bool isReverse, BZVehicleManual m)
    {
        if (!m_AimValido) return 0;
        // ★★★ N2: si la ley FF-primaria dio valida en el refresco de ESTE tick, manda
        // ella (mismo cap). Sin refresco valido (quieto/salto/fin de ventana) cae a la
        // persecucion de siempre -- fallback honesto, no un cero.
        if (s_FFPrimario && !isReverse && m_FFPk40Valido) return m_FFPk40;   // acotado por componente en la ley
        // Fix A: el refresco de 40 Hz es el que MANDA el volante -- si la referencia se
        // corrigiera solo en Plan, el arreglo no llegaria a la rueda (ver 13.39).
        vector pref = PosReferencia(pos, yaw, isReverse, m);
        float dx = m_LastAim[0] - pref[0];
        float dz = m_LastAim[2] - pref[2];
        float dist = Math.Sqrt(dx * dx + dz * dz);
        if (dist < 0.10) return 0;
        float refYaw = yaw;
        if (isReverse) refYaw = refYaw + Math.PI;
        float alpha = BZMath.WrapPi(BZMath.HeadingXZ(dx, dz) - refYaw);
        // ⚠⚠ EL TERMINO TIENE QUE ESTAR TAMBIEN ACA, Y ESTE ES EL QUE MANDA.
        // A 40 Hz el volante NO sale de `g.curvature` (2 Hz) sino de este re-apuntado:
        // `RefrescarVolante` pisa m_TargetSteer en cada Sample por encima de creepKmh.
        // Ponerlo solo en ComputeCurvature habria dejado la sonda sin efecto sobre la rueda
        // en casi toda la corrida, y el resultado se habria leido como "el FF no sirve".
        return AcotarACapacidad(2.0 * Math.Sin(alpha) / dist + m_FFDeficit);
    }

    // Para que la regla de gas del controlador use la MISMA nocion de rampa final que el
    // planificador: si el objetivo dice "sosténé la mordida", el gas tiene que sostenerla.
    bool EnRampaFinal()
    {
        return m_EnRampaFinal;
    }

    float MordidaKmh()
    {
        return m_MordidaKmh;
    }

    float LastKArco()
    {
        return m_LastKArco;
    }

    // La curvatura del camino DONDE ESTA EL VEHICULO, sobre un arco corto. Es la referencia
    // bien planteada de "cuanto hay que girar AHORA": la del arco entero del horizonte es un
    // PROMEDIO, y en una esquina corta con horizonte largo mezcla la curva con la recta que
    // sigue. Comparar el comando contra ese promedio da una razon que cambia de significado
    // con Ld -- que es el error que tenia el reporte anterior.
    float LastKLocal()
    {
        return m_LastKLocal;
    }


    // ⚠ SESGO DE MIRA HACIA AFUERA. Corre el punto de mira hacia el exterior del giro.
    //
    // HISTORIA, porque el nombre viejo mentia y costo dos reportes:
    // lo puse como "compensacion de flecha", diciendo que pure pursuit recorre
    //               la CUERDA y queda por dentro Ld^2/(8R).
    // falsé esa premisa: sobre un arco, con el vehiculo sobre la traza y
    //               alineado, kappa = 2 sin(alpha)/d = 1/R EXACTO para cualquier Ld
    //               (verificado, 1.0000 en 9 casos). No habia sesgo que compensar, asi que
    //               lo borre.
    // el A/B del motor lo desmintio: sin esto, TODAS las zonas curvas de AERO
    //               empeoraron (0.357->0.476, 0.329->0.588, 0.411->0.589) y las rectas no se
    //               movieron. Mi propia falsacion escrita decia "si suben, la repongo".
    //
    // ✖ NO TENGO EL MECANISMO. La geometria sobre las 168 curvas reales de AERO dice que
    // esto EMPEORA el comando (mediana COMANDA/k_local 1.002 sin esto, 0.778 con esto). El
    // motor dice que mejora el seguimiento. Se queda porque el motor midio el resultado y la
    // geometria solo modela una parte -- pose, dinamica y modelo de direccion quedan afuera.
    // Vuelve con su nombre honesto: es un sesgo EMPIRICO, no una compensacion derivada.

    string DiagVolante()
    {
        string s = "alpha " + (m_DiagAlpha * Math.RAD2DEG).ToString() + " deg";
        s = s + " · d " + m_DiagDist.ToString();
        s = s + " · den " + m_DiagDen.ToString();
        s = s + " · Ld " + m_LastLd.ToString();
        s = s + " · ffDef " + m_FFDeficit.ToString();
        return s;
    }

    string DiagObjetivo(float objetivoKmh, float recAquiKmh)
    {
        string s = "[BZDrive] OBJETIVO " + objetivoKmh.ToString();
        s = s + " · lo pone wp" + m_MinWp.ToString();
        s = s + " a " + m_MinDist.ToString() + " m";
        s = s + " (grabado ahi " + m_MinRecKmh.ToString();
        s = s + " · razon " + m_MinRazon + ")";
        s = s + " · aqui grabado " + recAquiKmh.ToString();
        return s;
    }

    // lo mismo en corto, para embeber en otra linea sin repetir el prefijo "[BZDrive] OBJETIVO".
    string ObjetivoCorto(float objetivoKmh)
    {
        string s = objetivoKmh.ToString() + " km/h por wp" + m_MinWp.ToString() + " (" + m_MinRazon + ")";
        return s;
    }

    // Las tres ataduras del Ld, para que el log diga CUAL mordio y no haya que deducirlo.
    string DiagLookahead()
    {
        string s = " · LdPide " + m_LdPedido.ToString();
        s = s + " · LdCorte " + m_LdTechoCorte.ToString();
        s = s + " · LdPiso " + m_LdPiso.ToString();
        s = s + " · LdFwd " + m_LdFwd.ToString();
        s = s + " · LdEstab " + m_LdEstab.ToString();
        return s;
    }

    float LastLookaheadM()
    {
        return m_LastLd;
    }

    // ✖ ACORTAR EL Ld POR GIRO: PROBADO Y FALSADO. NO REINTENTAR SIN LEER ESTO.
    //
    // El diagnostico era correcto y sigue en pie: con Ld mayor que la extension de la curva,
    // el acorde promedia curva con recta y el modulo sub-comanda (bus 55%, sedan 67% de la
    // curvatura real). El REMEDIO fue el equivocado.
    //
    // Se implemento AcortarPorGiro: si el giro de la ventana pasaba 20 grados (umbral sacado
    // del histograma real, p90 = 16.9), se reescalaba ld continuo con piso en una batalla. Se
    // cuidaron las dos causas del intento anterior --curvatura del ARCO en vez de por waypoint,
    // reescalado CONTINUO en vez de escalon-- y las dos se verificaron en el espejo.
    //
    // MEDIDO EN LOS DOS VEHICULOS, y rompe a los dos:
    //     bus   max lateral 8.09 m   (base 2.16)   ACORTADO disparo 104 veces
    //     sedan max lateral 18.35 m  (base ~0.3)   ACORTADO disparo 113 veces
    // El sedan, CON steerTransfer medido, empeoro MAS que el bus: no era el transfer.
    //
    // POR QUE: un Ld corto sube la GANANCIA del lazo, y pure pursuit se vuelve inestable --
    // con escalon o sin el. La divergencia fue una espiral lenta, no un flip tick-a-tick, asi
    // que un chequeo de oscilacion por cambio de signo NO la detecta. Y se realimenta: apenas
    // el lateral crece, la geometria mete mas ticks en la cola del histograma y la regla actua
    // mas (104-113 disparos contra el 8% que predecia el histograma en manejo sano).
    //
    // LA LECCION: el sub-comando es lo que pure pursuit ES, no un defecto suyo -- sigue un
    // punto a distancia Ld y en una curva mas cerrada que Ld necesariamente corta. Bajarle el
    // Ld cambia un error ACOTADO (ir un poco ancho) por uno DIVERGENTE (irse 18 m). Lo que
    // falta no es menos ventana: es un termino de ALIMENTACION DIRECTA de la curvatura del
    // camino, que sume lo que la ventana diluye SIN tocar la ganancia de realimentacion.
    //
    // Ld adaptativo: lo que el vehiculo recorre en PREVIEW_S, con piso geometrico.
    // Acotado por fwdM -- si la ventana se acorto cerca del final del leg, el modulo lo
    // SABE y no extrapola a ciegas.
    // ⚠⚠⚠ EL TECHO DEL Ld POR CORTE DE CURVA. Es el route-breaker de la tanda de 12.
    //
    // El corte geometrico de la pura persecucion es  eps ~ Ld^2 * k / 8.  Con Ld = v*PREVIEW
    // el corte crece con v^2, asi que la velocidad SOSTENIDA alta lo compone curva a curva.
    // Medido en la tanda: Agera 14.32 m fuera de linea a 77-104 km/h sostenidos, y se fue
    // por la tangente 1962 m; r32 7.76 m a 68 km/h. En NINGUNO habia K-CLAMP ni pursuit-off:
    // el volante no saturaba. No era falta de autoridad -- el punto de mira apuntaba FUERA
    // de la curva porque el Ld se habia hecho enorme.
    //
    // ★ SE INVIERTE LA MISMA FORMULA, no se agrega una perilla:
    //       eps = Ld^2 * k / 8   =>   Ld <= sqrt(8 * eps_max / k)
    // El unico numero nuevo es eps_max: CUANTO CORTE SE ACEPTA, en metros. Es una magnitud
    // que se puede discutir mirando una traza, no una ganancia que se tuning-ea a ciegas.
    //
    // ★★★ Y ES DE LA CURVA, NO DE LA VELOCIDAD -- que es lo que el dato pedia: el r32 fallo
    // a 68 km/h y el Gemera aguanto a 98. Un umbral de velocidad no puede explicar las dos
    // cosas; un techo que depende de k, si. En recta y en curva mansa (R=200 a 82 km/h) el
    // techo ni ata: el Ld sigue siendo el de siempre.
    //
    // ✖ Y NUNCA BAJA DEL PISO GEOMETRICO: el clamp del piso va DESPUES del techo. Acortar el
    // Ld sube la ganancia del lazo, y el piso (2 x batalla) es la estabilidad que ya
    // teniamos medida. Si ni con el Ld en el piso el corte entra en el presupuesto, el
    // problema ya no es el horizonte sino la VELOCIDAD DE LLEGADA -- y eso es del perfil,
    // no de aca. En los cuatro casos de la tanda el techo quedo por encima del piso.
    //
    // ✖ Se toma la PEOR curvatura de la ventana, no el promedio: una curva apretada metida
    // en medio de una recta se diluye en el promedio y es exactamente la que corta.
    private float CurvaturaPeorEnVentana(BZLegView leg, BZVehicleManual m, float ldM)
    {
        float baseM = VentanaFFM(m);
        if (baseM < 0.5) return 0;
        if (ldM < baseM) return 0;
        float peor = 0;
        float d0 = 0;
        int n = 0;
        while (n < CORTE_PASOS)
        {
            if (d0 + baseM > ldM) break;
            n++;
            if (leg.SampleAt(d0, m_SCorteA) && leg.SampleAt(d0 + baseM, m_SCorteB))
            {
                float dk = BZMath.WrapPi(m_SCorteB.headingRad - m_SCorteA.headingRad);
                float k = Math.AbsFloat(dk) / baseM;
                if (k > peor) peor = k;
            }
            d0 = d0 + baseM;
        }
        return peor;
    }

    // El Ld minimo que hace falta para que el lazo no cace, acotado por lo que v1.1 tolera
    // de corte. Devuelve 0 debajo de la compuerta de velocidad: la flota no lo ve nunca.
    private float PisoPorEstabilidad(BZLegView leg, BZVehicleManual m, float kmh, float ldPedido)
    {
        if (kmh < CORTE_ALTA_KMH) return 0;
        float k = CurvaturaPeorEnVentana(leg, m, ldPedido);
        if (k < 0.0005) return ldPedido;             // recta: el diseño entero, sin corte que acotar
        float porCorte = Math.Sqrt(8.0 * CORTE_ALTA_M / k);
        if (porCorte > ldPedido) return ldPedido;    // nunca mas largo que el diseño
        return porCorte;
    }

    private float TechoPorCorte(BZLegView leg, BZVehicleManual m, float ldPedido)
    {
        float k = CurvaturaPeorEnVentana(leg, m, ldPedido);
        if (k < 0.0005) return ldPedido;             // recta: no hay corte que acotar
        return Math.Sqrt(8.0 * CORTE_MAX_M / k);
    }

    // ★★★ QUIEN ATA EL Ld. Medido en el RPT del 09:06: a >140 km/h el Ld pedido por PREVIEW
    // es 53.3 m, el techo por corte da 35.0, y el Ld REAL es 20.5 -- o sea que ni siquiera
    // es el corte el que manda. Y la correccion de pure pursuit escala con 1/Ld^2, asi que
    // un Ld al 39% del diseño hace el lazo 6.6x mas caliente de lo diseñado. Eso explica
    // solo el exceso de correccion sobre la cinematica que medimos (5-9x).
    // Sin saber CUAL de las tres ataduras muerde, cualquier arreglo es a ciegas.
    private float LookaheadM(BZVehicleState st, BZLegView leg, BZVehicleManual m)
    {
        // st.kmh es el VELOCIMETRO (la rueda) y miente con el patinaje; el resto del
        // controlador usa la velocidad real del chasis. El horizonte tambien.
        float ld = (st.chassisKmh / 3.6) * PREVIEW_S;
        float floorM = PisoLookaheadM(m);
        // ⚠ El piso de ESTABILIDAD, que depende de la velocidad. Se toma el MAYOR de los
        // dos: el geometrico manda despacio (donde v*0.6 es casi nada) y este manda rapido
        // (donde dos batallas son un pañuelo). Ver PREVIEW_MIN_S.
        float pisoTiempoM = (st.chassisKmh / 3.6) * PREVIEW_MIN_S;
        if (pisoTiempoM > floorM) floorM = pisoTiempoM;
        // Banco: ver s_PisoLdForzadoM. Apagado (0) en toda corrida normal.
        if (s_PisoLdForzadoM > 0.5) floorM = s_PisoLdForzadoM;
        // ⚠ EL ORDEN IMPORTA: techo primero, piso despues. Al reves, el techo podria dejar
        // el Ld por debajo del piso y nadie lo levantaria.
        float techo = TechoPorCorte(leg, m, ld);
        // El piso por ESTABILIDAD: a alta velocidad el corte no puede empujar el Ld por
        // debajo del diseño, salvo que hacerlo cortara mas de lo que v1.1 tolera.
        float pisoEstab = PisoPorEstabilidad(leg, m, st.chassisKmh, ld);
        if (pisoEstab > floorM) floorM = pisoEstab;
        // (el relevo L1 en curva lenta vivio aca y fue ABSORBIDO por el piso global 1.2
        // borrado entero, no gateado)
        m_LdEstab = pisoEstab;
        m_LdPedido = ld;
        m_LdTechoCorte = techo;
        m_LdPiso = floorM;
        m_LdFwd = leg.fwdM;
        if (ld > techo) ld = techo;
        if (ld < floorM) ld = floorM;
        if (leg.fwdM > 0.05 && ld > leg.fwdM) ld = leg.fwdM;
        return ld;
    }

    // ======================= velocidad =======================

    // "EL OJO": mira adelante en la ventana y toma la velocidad que AHORA permite frenar
    // hasta cada objetivo que viene:  v = sqrt(vj^2 + 2 a d).  El MINIMO manda -> Boris se
    // ANTICIPA al tramo lento en vez de reaccionar tarde.
    private float ComputeSpeedMs(BZLegView leg, BZVehicleManual m, float decel, int surfKind)
    {
        int i;
        float best = -1.0;
        float d;
        float vj;
        float lim;
        for (i = leg.nearWp; i <= leg.winHi; i++)
        {
            if (!leg.Sample(i, m_S)) break;
            d = leg.ArcFromCursor(i);
            if (d < 0) d = 0;
            vj = TargetAt(m_S, m, decel, surfKind);
            lim = Math.Sqrt(vj * vj + 2.0 * decel * d);
            if (best < 0 || lim < best)
            {
                best = lim;
                // ⚠ Se guarda la razon de ESTE waypoint, no la del ultimo recorrido: la
                // pregunta es quien puso el MINIMO. Hay un mutante para cada confusion.
                m_MinRazon = m_RazonAct;
                m_MinWp = i;
                m_MinDist = d;
                m_MinRecKmh = m_S.recSpeedKmh;
            }
        }
        if (best < 0) return 0;
        return best;
    }

    // Velocidad objetivo EN un waypoint. Prioridad:
    //   1. la GRABADA, si es usable (el humano ya resolvio ese tramo)
    //   2. si no sirve y el tramo es TIGHT (precision declarada por el autor) -> el creep
    //      del vehiculo, o sea lo que hace SOLO en ralenti. Reemplaza el "parking = 3.0 km/h"
    //      horneado de v1.1 por un hecho del vehiculo.
    //   3. si no sirve y no es tight -> el limite por CURVATURA y agarre del piso.
    // Un stop grabado sigue siendo 0: eso es dato, no falta de dato.
    private float TargetAt(BZPathSample s, BZVehicleManual m, float decel, int surfKind)
    {
        float vj = s.recSpeedKmh / 3.6;
        if (vj < REC_USABLE_MS)
        {
            if (s.isStop && s.wp != m_StopServidoWp)
            {
                m_RazonAct = "STOP";
                return 0;
            }
            if (s.isStop)
            {
                // UN STOP SERVIDO NO PIDE 0. Medido en la linea de buses (RPT 21/09 13:20, 44
                // salidas de hold con el cursor EN el wp isStop): el objetivo era 0 en todas -- el return de arriba --
                // y el vehiculo salia igual en 42 por un accidente: con objetivo 0 la derivacion (BZDerivationEngine.
                // Pedals) pide un gas minimo para cancelar la resistencia al rodaje, el piso de arranque lo sube a
                // 0,35 y la rampa lo hace rodar hasta que el cursor pasa el wp. En el unico stop en bajada fuerte
                // de las dos rutas (wp 13157, -9,7 %) ese mismo termino pide FRENO: 2 de 2 clavados para siempre
                // con AR=0 (la cinta no posee y no hay gas aplicado: ninguna firma del AR da verde). Servido el
                // stop, este wp deja de opinar: vale el limite por curvatura y el minimo de la ventana lo ponen los
                // wps siguientes, que son los grabados de la salida. Mediana del primer gas tras el hold: 3,0 s.
                m_RazonAct = "STOP-SERVIDO";
                return CurvatureLimitMs(s, decel);
            }
            if (s.tight)
            {
                m_RazonAct = "CREEP-CERRADO";
                return m.creepKmh / 3.6;
            }
            m_RazonAct = "CURVA-SIN-GRABADO";
            return CurvatureLimitMs(s, decel);
        }
        m_RazonAct = "GRABADO";
        float v = PisoDeMordida(s, m, vj, decel);
        if (v > vj + 0.01) m_RazonAct = "MORDIDA";
        // ⚠⚠⚠ LA CURVA ES UN TECHO, SIEMPRE. Hasta aca, con velocidad grabada utilizable
        // se devolvia lo grabado SIN pasarlo nunca por el limite de curvatura: lo grabado
        // era un PERMISO, no un objetivo. Y un permiso no vale si el que grabo iba en otro
        // vehiculo, o si el vehiculo llega mas rapido de lo que el humano llego.
        //
        // Medido: a 77-104 km/h sostenidos el Agera se iba por la tangente
        // ~2 km. Acortarle el horizonte no alcanzo --y de hecho lo empeoro-- porque el
        // problema no era desde donde miraba: era a que velocidad llegaba.
        //
        // ★ LA ANTICIPACION SALE GRATIS, sin mecanismo nuevo: ComputeSpeedMs ya pliega cada
        // waypoint de la ventana por sqrt(vj^2 + 2*a*d) y se queda con el MINIMO. Con el
        // techo puesto en TargetAt, una curva cerrada 40 m adelante baja la velocidad de
        // AHORA, y cuanto mas lejos este, menos frena. Es `predictTurn` cayendo solo de una
        // maquinaria que ya estaba.
        //
        // ✖ Es un TECHO, no un reemplazo: en recta CurvatureLimitMs devuelve 999 y no toca
        // nada, y si lo grabado ya es mas lento que la curva, manda lo grabado. Nunca acelera.
        // ⚠ LA TOMA SOLO PUEDE SUBIR EL PRESUPUESTO, NUNCA BAJARLO.
        // Sin esta asimetria el cambio le bajaria la velocidad en curva a media flota: el
        // ZiS5 mide 4.88 contra un presupuesto de 9.81. Con ella, los ocho vehiculos que
        // miden por debajo quedan exactamente como hoy, y se destraban solo los que la toma
        // demostro que aguantan mas (Agera 24.67, Suzuki 25.02).
        float aLat = decel;
        // ✖ ACA IBA EL PRESUPUESTO POR BANDA DE VELOCIDAD Y NO SE APLICO, porque la
        // justificacion se cayo al verificarla. Yo habia inferido que el cap permitia
        // 4.8 m/s2 donde el humano uso 7.1, dividiendo el OBJETIVO por la curvatura LOCAL.
        // Pero el objetivo es el MINIMO plegado sobre una ventana de anticipacion: lo fija
        // una curva ADELANTE, no la de abajo del auto. El motor imprime su presupuesto real
        // y para esta toma es 24.99 m/s2 -- cinco veces mas permisivo que mi inferencia.
        // El cap no estaba apretando. La maquinaria de bandas quedo en BZRouteData, medida y
        // con espejo, por si algun dia hace falta; lo que NO quedo es usarla sin causa.
        float aToma = m_ALatToma;
        if (aToma > aLat) aLat = aToma;
        float vCurva = CurvatureLimitMs(s, aLat);
        if (v > vCurva)
        {
            v = vCurva;
            m_RazonAct = "CURVA";
        }
        return v;
    }

    // ⚠ NO PEDIR VELOCIDADES QUE EL VEHICULO NO PUEDE SOSTENER.
    //
    // Por debajo de la velocidad de mordida el motor deja de transmitir: no tracciona (el
    // bog de re-arranque) y el volante deja de ser efectivo (deriva). Medido en AERO: la
    // toma grabada pide 5-7 km/h en los ultimos 11 m --el humano lo hace con embrague-- y
    // Boris cruzaba la mordida (10.5 km/h en 1ra), se apagaba a 0.26 km/h y derivaba 1.5 m
    // de costado. Los dos sintomas salian del MISMO regimen sub-mordida.
    //
    // Asi que se sostiene la mordida y se frena firme al final, desviandose a proposito de
    // lo grabado en ese tramo. Y la distancia a la que se deja de sostener NO es una
    // constante: es donde la propia curva de frenado del planificador vale la mordida,
    //          d* = vMordida^2 / (2 * decel)
    // con el MISMO `decel` medido que el planificador ya usa. En d* las dos valen lo mismo,
    // asi que el piso y la curva se encuentran sin escalon.
    //
    // ✖ Y SOLO EN LA RAMPA FINAL, no en cualquier tramo lento.
    // Una esquina lenta a mitad de ruta tambien pide menos que la mordida, y ahi subir el
    // objetivo seria manejar mas rapido que el grabado sin que nadie lo haya pedido. Lo que
    // el usuario autorizo es desviarse del grabado EN LA APROXIMACION a una parada.
    //
    // El limite por curvatura no sirve de acotado --a R=5 m permite 23 km/h, muy por encima
    // de la mordida, asi que nunca mordería--. El acotado sale del DATO: la rampa terminal
    // es la corrida CONTIGUA de waypoints sub-mordida que llega hasta la parada. Se calcula
    // una vez por tick barriendo la ventana desde el final hacia atras. En AERO son los
    // ultimos ~11 m (el humano freno suave, mucho mas suave de lo que el freno permite, y
    // por eso quedan 11 m por debajo de la mordida).
    private float PisoDeMordida(BZPathSample s, BZVehicleManual m, float vj, float decel)
    {
        if (s.isStop) return vj;
        if (!m_EnRampaFinal) return vj;            // no es la llegada: manda lo grabado
        if (m_MordidaKmh <= 0.5) return vj;
        float vMord = m_MordidaKmh / 3.6;
        if (vj >= vMord) return vj;
        // El MISMO criterio que en el sosten (ver BZDriveController.PisoDeMordidaAplica):
        // si lo grabado no llega ni al borde de la banda que el piso defiende, el humano no
        // estaba sobre el filo -- estaba manejando deliberadamente por debajo, y levantarle
        // el objetivo a la mordida lo lleva a 2.7x la velocidad que quiso.
        if (!BZDriveController.PisoDeMordidaAplica(s.recSpeedKmh, m_MordidaKmh)) return vj;
        if (decel <= 0.01) return vj;
        float dEstrella = vMord * vMord / (2.0 * decel);
        if (m_DistAlFin <= dEstrella) return vj;   // ya toca frenar: manda la curva
        return vMord;
    }

    // La rampa terminal: la corrida CONTIGUA de waypoints por debajo de la mordida que
    // termina en la parada del leg. Si el vehiculo esta adentro de ella, esta llegando; si
    // no, un waypoint lento suelto es geometria o criterio del que grabo, y se respeta.
    private bool DetectarRampaFinal(BZLegView leg, BZVehicleManual m)
    {
        if (!leg.valid) return false;
        // ⚠⚠ LOS INTERCAMBIOS SON ENDPOINTS (doctrina 2026-07-26) -- y esta compuerta no
        // lo sabia: exigia endIsStop, asi que la aproximacion a un CUSP corria sin la
        // proteccion de mordida. Medido (RPT 11-46, Gemera wp154-167): 14 wp a 4 km/h con
        // gas 0.00 y obj 19 -- lo grabado baja hacia el cusp (los pisos anti-bog se
        // apartan, correcto) y NADIE evitaba hundirse bajo la mordida. El "un solo
        // impulso" de 1.1 es exactamente esta rampa, armada tambien aca.
        if (!leg.endIsStop && !leg.endIsCusp) return false;
        // ⚠ Con (0, 0) esto cae SIEMPRE al respaldo, que es justo el que suponia primera:
        // la rampa final de un tramo de reversa se media contra 15.87 en vez de 10.44, y
        // por lo tanto contra un umbral que casi ningun waypoint podia superar. Se pregunta
        // por la marcha del TRAMO, que es la que el vehiculo va a tener puesta al recorrerlo.
        int gearTramo = 2;                       // 2 = PRIMERA
        if (leg.isReverse) gearTramo = 0;        // 0 = REVERSA
        float mordidaKmh = m.SpeedAtBiteKmh(0, 0, gearTramo);
        if (mordidaKmh <= 0.5) return false;
        int i;
        for (i = leg.winHi; i >= leg.nearWp; i--)
        {
            if (!leg.Sample(i, m_S0)) return false;
            if (m_S0.isStop) continue;
            if (m_S0.recSpeedKmh >= mordidaKmh) return false;   // la corrida se corto antes
        }
        return true;
    }

    // Techo por fisica lateral: v = sqrt(a_lat / |kappa|). Se usa la decel maxima del piso
    // como proxy del agarre disponible (mismo mu).
    private float CurvatureLimitMs(BZPathSample s, float aLat)
    {
        float k = s.curvature;
        if (k < 0) k = -k;
        if (k < 0.001) return 999.0;
        return Math.Sqrt(aLat / k);
    }
}
