# -*- coding: utf-8 -*-
"""
envelope_emit.py -- emite `envelope_<clase>.csv` DESDE UNA TOMA (frame CSV).

Es el gemelo OFFLINE de lo que `BZEnvelopeStore.Save()` escribe online. Rompe el huevo y
la gallina: hoy el envelope se aprende MANEJANDO, asi que un vehiculo nuevo se clava en su
primer run por falta de envelope (Willys_MB: legs 0 y 2 abandonados, 174 m del endpoint).
Derivandolo en la conversion, la grabacion ya trae su propio mapa y el vehiculo la
reproduce de una.

★ MAPA SINCERO DE LA TOMA, no del vehiculo (decision del usuario).
  No se busca el maximo de lo que el vehiculo da: se describe LO QUE LA TOMA HIZO. Si la
  toma solo uso el balde 0 de volante, el balde 0 es TODO lo que hace falta para
  reproducirla; los otros nueve no son un hueco. Los bins que la toma no toco quedan
  VACIOS a proposito -- el motor cae a su fallback, que es la respuesta honesta a
  "aca no hay dato".

⚠ NO se emite `ratio`. El propio motor documenta que el ratio por marcha NO CONVERGE
  (3.94 a 54.75 dentro de la misma marcha) y lo descarta con un gate estricto. Emitirlo
  desde la toma seria inventar precision que el dato no tiene. Creep y launch se miden
  DIRECTO, que es justamente lo que saco del medio a esa cadena.

Uso:
    python envelope_emit.py <frame.csv> --clase Willys_MB --wb 2.292 --rueda 0.30 [--out DIR]
    python envelope_emit.py <frame.csv> [--out DIR]            # clase y wheelbase del header apareado (asi lo llama el wizard)
    python envelope_emit.py --dir <carpeta> --clase X [--out DIR] [--pisar]
        TODAS las tomas de la clase X que haya en <carpeta> (por el header apareado; solo las que nombro el recorder,
        frame_<n>-<hhmmss>_X: un frame_MERGE*/SEQ* es esas tomas otra vez y se saltea por nombre). Para RECONVERTIR una clase
        entera desde sus grabaciones sin pasarlas una por una. Si ya existe envelope_X.csv en DIR se FUSIONA como
        siempre (misma firma) -> para regenerar SOLO desde las tomas, --pisar.
        --pisar sobre un envelope existente dice QUE PIERDE (lo que las tomas no reponen: el lhist, sobre todo),
        y CONSERVA `lcal` (el piso de despegue medido en calibracion): lo calibrado no sale de las tomas.
    python envelope_emit.py --fusionar a.csv b.csv [...] --out DIR [--clase C] [--forzar]
"""
import io
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from envelope_desde_toma import (leer, exigir_filas, FALTA, T, STE, FWD, CHAS, KMH, THR, BRK, HB, GEAR, HEAD, PITCH, RPM,
                                 K_MIN_MS, STX_EPS_CMD, STX_EPS_ANG, STX_ESTABLE_TICKS,
                                 ST_BUCKETS, C_MAX_THROTTLE, C_MAX_BRAKE, C_MAX_HB,
                                 C_MAX_STEER, C_MIN_MS, C_MIN_DECEL, C_MAX_DECEL,
                                 BINW, NBINS)

# ---- grillas, copiadas de BZEnvelopeStore. Si alguna cambia alla, cambia aca. ----
STX_BUCKETS = 10
ANBINS, ABIN = 48, 0.25            # aceleracion 0..12 m/s2
SNBINS, SBIN = 90, 0.0174532925    # angulo de rueda 0..90 grados de a 1
CNBINS, CBIN = 80, 0.25            # creep 0..20 km/h
LNBINS, LBIN = 50, 0.02            # launch 0..1 de gas
A_MIN_THR = 0.8                    # solo cuenta lo que se logra A FONDO
# ⚠ EL LAUNCH ES UN LATCH, NO UNA COMPARACION ENTRE DOS TICKS. Lo tenia mal y por eso daba
# `launch 0(0)` en las cuatro tomas RUS -- se leyo como "las tomas no lo capturaron" cuando
# el hueco era del emisor. El motor (BZEnvelopeRecorder.TryLaunch) arma el latch en CUALQUIER
# tick por debajo de L_STOPPED_MS y dispara en el PRIMERO que lo supera, siempre que todavia
# no venga lanzado. Exigir el salto entre dos ticks consecutivos casi nunca se cumple.
L_STOPPED_MS = 0.05
L_MOVING_MS = 0.4
L_MIN_THR = 0.01     # sin gas no hay despegue; espejo de BZEnvelopeRecorder.L_MIN_THR
# ⚠⚠ Y EL CREEP TAMPOCO ERA "cualquier tick rodando libre". El motor lo dice en una linea:
#     "creep es EQUILIBRIO, no un paso al pasar"  (CR_MAX_ACCEL)
# Yo contaba TODO tick con gas, freno y HB sueltos, asi que en conduccion normal --donde el
# humano suelta el gas a 15-20 km/h todo el tiempo-- el histograma se llenaba hasta el bin
# 79 (19.75 km/h) y el motor derivaba creepKmh ~ 19.875. Con eso, `UmbralCasiParado()` cree
# que el vehiculo esta "casi parado" HASTA 20 km/h: la rampa de arranque se dispara donde no
# debe, no despega y devuelve el mando en cada parada de mitad de ruta. Medido en los 4
# vehiculos RUS. Los envelopes que SI funcionan traen `nocreep` en vez de un creep alto.
#
# El gate decisivo es el de aceleracion: un vehiculo que PASA por 18 km/h rodando esta
# decelerando, asi que |a - aGrav| > CR_MAX_ACCEL y queda afuera. Solo cuenta si se ASIENTA.
CR_MIN_MS = 0.15
CR_MAX_MS = 5.0
CR_MAX_ACCEL = 0.05
# EL CREEP ES UN REGIMEN, NO UN INSTANTE (el mismo B2 del recorder, BZEnvelopeRecorder.TryCreep): el equilibrio tiene
# que sostenerse 10 muestras seguidas (0,25 s a 40 Hz). En la toma de la linea del bus, 39 de 532 frames con pedales sueltos
# cumplian |a| <= 0,05 un instante por ruido (7 %): con 532 candidatos llegar a 20 era inevitable. 0,07^10 ~ 3e-12.
# Cualquier rechazo reinicia la racha. (La reversa ya quedaba afuera: aca se exige PRIMERA.)
CR_PERSIST_FRAMES = 10
CR_MAX_PITCH = 0.06
# ⚠⚠ Y FALTABA EL GATE QUE EL MOTOR TIENE Y EL GEMELO NO PUEDE COPIAR.
#
# Online, `TryCreep` arranca con `if (!m_CreepWindow) return;` -- "fuera del banco no se
# mide creep". Esa ventana es CONTEXTO, no una magnitud: offline no existe, y por eso el
# emisor tragaba coast-downs que el motor nunca ve. Con solo el gate de equilibrio siguen
# entrando: un vehiculo pesado bajando de 20 km/h pasa por 17 con desaceleracion chica, y
# el cluster quedaba en 16-18 km/h (bin 71) en 3 de los 4 RUS.
#
# El reemplazo offline no es un filtro mas fino: es LA DEFINICION. El creep (reptado) es la
# velocidad que el vehiculo SOSTIENE A RALENTI con el embrague enganchado. Un vehiculo que
# rueda a 17 km/h no esta a ralenti. Medido en una toma real de sedan:
#
#     quieto, libre     n=36    rpm mediana  950   (min 945)   <- ralenti
#     >18 km/h, libre   n=272   rpm mediana 3554   (min 2396)  <- coast, 2.5x el ralenti
#
# Separacion de 2.5x con margen de sobra. El ralenti se estima del percentil bajo de la
# propia toma, asi que no hace falta saberlo por vehiculo.
#
# ✖ NO se toca CR_MAX_MS. Es constante del MOTOR (BZEnvelopeRecorder), no mia: bajarla aca
# haria que el gemelo offline y el camino online midan cosas distintas, que es exactamente
# lo que el gemelo existe para evitar.
IDLE_MULT = 1.35
# ⚠⚠⚠ EL PISO QUE FALTABA. La compuerta de RPM tenia solo cota SUPERIOR:
# rechazaba "revolucionado" pero con el motor APAGADO (rpm = 0 < techo) el tick PASABA y
# contaba como evidencia de que el vehiculo NO REPTA. Es el mismo falso positivo que el
# handbrake --"no se movio porque estaba apagado", no "porque no reptea"-- y el handbrake
# SI tenia su rechazo explicito. Ninguna toma actual lo ejercia, pero el usuario pregunto
# literalmente si conviene apagar el motor antes de cortar: estaba a una grabacion de pasar.
# 0.5 x ralenti: un motor andando no baja de la mitad de su propio ralenti; uno apagado da 0.
IDLE_FLOOR = 0.5
# ⚠⚠⚠ Y TODAVIA FALTABA LA MARCHA. Este fue el que costo tres vueltas encontrar.
#
# Con el gate de ralenti puesto, el Scammel seguia dando creep 16.25 km/h. Fui a ver QUE
# ticks pasaban todas las compuertas -- controles sueltos, rpm de ralenti, en rango,
# equilibrio -- y estaban a 16.3 km/h EN CUARTA. El ZiS5, a 6.7 en tercera.
#
# ★ Y NO ERA UN ERROR DE MEDICION: ES CIERTO. Un camion en 4ta a ralenti VA a 16 km/h, en
# equilibrio, sin tocar nada. La medicion estaba bien; lo que estaba mal era la PREGUNTA.
#
# `creepKmh` no se consume como "velocidad a ralenti en la marcha que sea": se consume como
# "PRACTICAMENTE PARADO" -- `UmbralCasiParado()`, el gate del CARRY del endpoint, el piso de
# velocidad recomendada. Todos quieren el reptado de PRIMERA. En 4ta la magnitud existe y no
# sirve para nada de eso.
#
# DayZ indexa 0 = reversa, 1 = neutral, 2 = PRIMERA. Se exige la 2:
#   - neutral queda afuera por definicion (motor desconectado: cualquier velocidad es coast),
#   - reversa es otra magnitud,
#   - y de las adelante, solo la mas corta responde a "cuanto se arrastra solo".
#
# Medido sobre las 5 tomas frescas: restringiendo a primera, los tres camiones dan n=0 ->
# `nocreep`. Ninguna toma normal tiene al vehiculo ralenti-rodando en primera, porque el
# humano no repta. Y `nocreep` es EXACTAMENTE lo que traen los envelopes que si funcionan
# (ExpansionBus: `nocreep,1590`). El mapa sincero dice "no repta", que es la verdad.
#
# ✖ El camino online tampoco gatea por marcha: se apoya en `m_CreepWindow` (el banco), que
# offline no existe. Este gate es el equivalente offline de ese contexto, igual que el de
# ralenti -- no una constante nueva del motor.
GEAR_PRIMERA = 2
# ⚠ Y EL UMBRAL DEL MOTOR, PARA NO ESCRIBIR UN HISTOGRAMA QUE VA A DESCARTAR.
#
#     override float CreepKmh() { if (m_CreepCnt < CREEP_MIN_SAMPLES) return 0; ... }
#
# El Gemera salio con UNA sola muestra en el bin 32. El motor la descarta igual (1 < 20) y
# devuelve creep 0 -- pero como el emisor solo escribe `nocreep` cuando chist2 esta VACIO,
# esa unica muestra suprimia la evidencia de que NO REPTA. Resultado: mismo comportamiento,
# menos informacion. `creep 0kmh(1)` en vez de `[NO REPTA: n muestras quieto]`.
#
# Por debajo del minimo del motor, el mapa sincero dice "no repta", que es lo que el motor
# va a usar de todas formas.
CREEP_MIN_SAMPLES = 20


def _bin(v, w, n):
    b = int(v / w)
    return 0 if b < 0 else (n - 1 if b > n - 1 else b)


# Desglose de por que se descartan los ticks libres del muestreo de creep. La leccion del
# dia: una compuerta sin diagnostico es una compuerta que nadie puede auditar.
RECHAZOS = {}


def acumular(fil):
    """Devuelve los histogramas en la MISMA forma que los guarda el motor."""
    e = {"stx2": {}, "shist": {}, "chist2": {}, "lhist": {}, "ahist": {},
         "k": {}, "coast2": {}, "nocreep": 0, "aMin": 0.0, "aMax": 0.0, "lcal": {}, "_ajenas": []}

    # ---------------- steerTransfer: comando -> angulo, en MESETA ----------------
    cmd_prev = ang_prev = None
    estable = 0
    for r in fil:
        cmd, ang = abs(r[STE]), abs(math.radians(r[FWD]))
        ms = (r[CHAS] if r[CHAS] != FALTA else r[KMH]) / 3.6
        if ang >= 1.5708 or ms < K_MIN_MS:
            cmd_prev, ang_prev, estable = cmd, ang, 0
            continue
        if cmd_prev is None:
            cmd_prev, ang_prev = cmd, ang
            continue
        quieto = (abs(cmd - cmd_prev) < STX_EPS_CMD and abs(ang - ang_prev) < STX_EPS_ANG)
        cmd_prev, ang_prev = cmd, ang
        if not quieto:
            estable = 0
            continue
        estable += 1
        if estable < STX_ESTABLE_TICKS:
            continue
        b = _bin(cmd * ST_BUCKETS, 1.0, STX_BUCKETS)
        s, n = e["stx2"].get(b, (0.0, 0))
        e["stx2"][b] = (s + ang, n + 1)

    # ---------------- histograma de angulo de rueda (steerMax) ----------------
    for r in fil:
        ang = abs(math.radians(r[FWD]))
        if ang < 1e-4 or ang >= 1.5708:
            continue
        b = _bin(ang, SBIN, SNBINS)
        e["shist"][b] = e["shist"].get(b, 0) + 1

    # ---------------- creep / nocreep, espejando TryCreep ----------------
    # ⚠ Sin columna `pitch_deg` (tomas viejas) se asume LLANO: no se puede verificar la
    # compuerta de pendiente ni compensar la gravedad. Es la direccion conservadora --en una
    # cuesta el gate de equilibrio RECHAZA en vez de aceptar basura-- pero queda dicho.
    quieto_ticks = 0
    # El ralenti de ESTA toma: percentil bajo de las rpm con motor andando. Percentil y no
    # minimo -- un solo tick raro no puede definir la referencia.
    rpms = sorted(r[RPM] for r in fil if r[RPM] > 1.0)
    idle = rpms[len(rpms) // 20] if rpms else 0.0
    techoRpm = idle * IDLE_MULT if idle > 1.0 else 0.0
    pisoRpm = idle * IDLE_FLOOR if idle > 1.0 else 0.0
    RECHAZOS.clear()
    racha = 0                         # muestras seguidas en equilibrio (B2)
    for i in range(1, len(fil)):
        a_, b_ = fil[i-1], fil[i]
        dt = b_[T] - a_[T]
        if dt < 0.005 or dt > 0.5:
            racha = 0
            continue
        if b_[THR] > C_MAX_THROTTLE or b_[BRK] > C_MAX_BRAKE or b_[HB] > C_MAX_HB:
            racha = 0
            continue
        if techoRpm > 1.0 and b_[RPM] > techoRpm:
            RECHAZOS["rpm_alto"] = RECHAZOS.get("rpm_alto", 0) + 1
            racha = 0
            continue                      # no esta a ralenti: es coast, no reptado
        if pisoRpm > 1.0 and b_[RPM] < pisoRpm:
            RECHAZOS["motor_apagado"] = RECHAZOS.get("motor_apagado", 0) + 1
            racha = 0
            continue                      # motor APAGADO: quieto no prueba nada (ver IDLE_FLOOR)
        pitchRad = 0.0 if b_[PITCH] == FALTA else math.radians(b_[PITCH])
        if abs(pitchRad) > CR_MAX_PITCH:
            racha = 0
            continue
        msA = (a_[CHAS] if a_[CHAS] != FALTA else a_[KMH]) / 3.6
        msB = (b_[CHAS] if b_[CHAS] != FALTA else b_[KMH]) / 3.6
        acc = (msB - msA) / dt
        aGrav = -9.81 * math.sin(pitchRad)
        if msB < CR_MIN_MS:
            # quieto Y estable = evidencia de que NO REPTA, que tambien es una medicion
            if abs(acc - aGrav) <= CR_MAX_ACCEL:
                quieto_ticks += 1
            racha = 0
            continue
        # ⚠ LA MARCHA SE EXIGE ACA, DESPUES del contador de nocreep. Puesta arriba
        # tambien mataria la evidencia de que NO REPTA --un vehiculo detenido puede estar en
        # neutral-- y se perderian las dos cosas: ni chist2 ni nocreep.
        if int(b_[GEAR]) != GEAR_PRIMERA:
            racha = 0
            continue                      # ver GEAR_PRIMERA: en 4ta el ralenti son 16 km/h
        if msB > CR_MAX_MS:
            racha = 0
            continue
        if abs(acc - aGrav) > CR_MAX_ACCEL:
            racha = 0
            continue                      # PASA por esa velocidad, no se asienta en ella
        racha += 1
        if racha < CR_PERSIST_FRAMES:
            RECHAZOS["sin_persistir"] = RECHAZOS.get("sin_persistir", 0) + 1
            continue                      # en equilibrio, pero todavia no sostenido (B2)
        bn = _bin(msB * 3.6, CBIN, CNBINS)
        e["chist2"][bn] = e["chist2"].get(bn, 0) + 1
    # Ver CREEP_MIN_SAMPLES: un histograma que no llega al minimo del motor no es un dato,
    # y ademas tapa el `nocreep` que si lo es.
    if sum(e["chist2"].values()) < CREEP_MIN_SAMPLES:
        e["chist2"] = {}
    if not e["chist2"]:
        e["nocreep"] = quieto_ticks

    # ---------------- launch, espejando TryLaunch (latch + freno + pendiente) ----------
    # Se hace en pasada aparte porque la compensacion de pendiente necesita el aMax, que
    # recien queda listo abajo. Mismo orden que el motor, donde aMax vive en el Store.
    e["_launch_pend"] = []
    parado = False
    for r in fil:
        ms = (r[CHAS] if r[CHAS] != FALTA else r[KMH]) / 3.6
        if r[BRK] > C_MAX_BRAKE:
            parado = False
            continue
        if ms < L_STOPPED_MS:
            parado = True
            continue
        if not parado:
            continue
        parado = False
        if ms > L_MOVING_MS:          # ya venia lanzado: no es el instante
            continue
        # ★ SIN GAS NO HAY DESPEGUE: un cruce con gas 0.0 es la gravedad. La compensacion de
        # pendiente de abajo lo convertia en +g*sin(pitch)/aMax (thrPlano 0.02-0.09 en las 14 muestras que
        # salian de 502 tomas de teclado, todas en bajada). Se exige gas crudo ANTES de compensar, como el motor.
        if r[THR] < L_MIN_THR:
            continue
        if r[THR] > 0.99:
            continue
        e["_launch_pend"].append((r[THR], r[PITCH]))

    # ---------------- aceleracion a fondo, y coast ----------------
    for i in range(1, len(fil)):
        a, b = fil[i-1], fil[i]
        dt = b[T] - a[T]
        if dt < 0.005 or dt > 0.5:
            continue
        va = (a[CHAS] if a[CHAS] != FALTA else a[KMH]) / 3.6
        vb = (b[CHAS] if b[CHAS] != FALTA else b[KMH]) / 3.6
        acc = (vb - va) / dt
        if b[THR] >= A_MIN_THR and acc > 0:
            e["ahist"][_bin(acc, ABIN, ANBINS)] = e["ahist"].get(_bin(acc, ABIN, ANBINS), 0) + 1
            if e["aMin"] <= 0.0001 or acc < e["aMin"]:
                e["aMin"] = acc
            if acc > e["aMax"]:
                e["aMax"] = acc
        libre = (b[THR] < C_MAX_THROTTLE and b[BRK] < C_MAX_BRAKE and b[HB] < C_MAX_HB
                 and abs(math.radians(b[FWD])) < C_MAX_STEER)
        if libre and vb > C_MIN_MS:
            dec = -acc
            if C_MIN_DECEL < dec < C_MAX_DECEL:
                # idx = surf * NBINS + bin. Sin columna de superficie -> DURO (surf 0),
                # que es lo unico que la toma respalda.
                idx = _bin(vb * 3.6, BINW, NBINS)
                s, n = e["coast2"].get(idx, (0.0, 0))
                e["coast2"][idx] = (s + dec, n + 1)

    # ⚠ EL GAS SE COMPENSA POR PENDIENTE ANTES DE BINEAR, y el rango se valida sobre el
    # valor COMPENSADO -- igual que el motor. En bajada la gravedad ayuda a romper la
    # inercia y el piso observado sale de menos. Sin columna `pitch_deg` (tomas viejas) se
    # asume llano: es degradacion honesta, no correccion inventada, PERO en una toma que
    # arranca en pendiente el numero sale sesgado. Queda dicho.
    aMaxLocal = e["aMax"]
    for thr, pitchDeg in e.pop("_launch_pend"):
        thrPlano = thr
        if aMaxLocal > 0.1 and pitchDeg != FALTA:
            thrPlano = thr - (9.81 * math.sin(math.radians(pitchDeg))) / aMaxLocal
        if thrPlano < 0.01 or thrPlano > 0.99:
            continue
        b_ = _bin(thrPlano, LBIN, LNBINS)
        e["lhist"][b_] = e["lhist"].get(b_, 0) + 1
    return e


def acumular_k(fil, wb):
    """Subviraje: k = curvatura LOGRADA / curvatura cinematica, por bin de velocidad."""
    out = {}
    for i in range(1, len(fil)):
        a, b = fil[i-1], fil[i]
        dt = b[T] - a[T]
        if dt < 0.005 or dt > 0.5:
            continue
        ms = (b[CHAS] if b[CHAS] != FALTA else b[KMH]) / 3.6
        if ms < K_MIN_MS:
            continue
        ang = math.radians(b[FWD])
        if abs(ang) < 0.02:                      # sin volante no hay subviraje que medir
            continue
        dpsi = math.radians(b[HEAD] - a[HEAD])   # heading, en grados
        while dpsi > math.pi:  dpsi -= 2 * math.pi
        while dpsi < -math.pi: dpsi += 2 * math.pi
        k_log = (dpsi / dt) / ms                 # curvatura lograda
        k_cin = math.tan(ang) / wb               # curvatura cinematica
        if abs(k_cin) < 1e-6:
            continue
        rel = k_log / k_cin
        if rel <= 0.05 or rel > 1.5:             # fuera de rango: no es subviraje, es ruido
            continue
        bn = _bin(ms * 3.6, BINW, NBINS)
        s, n = out.get(bn, (0.0, 0))
        out[bn] = (s + rel, n + 1)
    return out


def fusionar(es):
    """Fusiona los histogramas de VARIAS tomas de la MISMA clase.

    ★ Por que hace falta: el envelope ya es POR VEHICULO (`envelope_<clase>.csv`), no por
    ruta -- lo unico que lo ataba a una toma era COMO se generaba. Y el creep no se puede
    exigir dentro de la toma de ruta: el terreno no se elige (el usuario: *"si es solo una subida
    por la colina no tengo tiempo ni espacio"*), y en pendiente el creep se rechaza POR
    DISENO (CR_MAX_PITCH) porque no se distingue "no reptea" de "la gravedad lo frena".
    Con esto: una toma corta de calibracion en cualquier llano, una vez por vehiculo, y la
    ruta se graba como la mision la pida.

    Es PARIDAD OFFLINE<->ONLINE, no una feature: el motor ya acumula histogramas de varios
    drives; el emisor offline era el unico que no sabia.

    ✖ Se fusionan TOMAS, no se suma sobre el archivo existente. Sumar sobre el archivo
    hace que correr el mismo comando dos veces DUPLIQUE las muestras; partir siempre de
    las fuentes es reproducible e idempotente.
    """
    out = {"stx2": {}, "shist": {}, "chist2": {}, "lhist": {}, "ahist": {},
           "k": {}, "coast2": {}, "nocreep": 0, "aMin": 0.0, "aMax": 0.0, "lcal": {}, "_ajenas": []}
    for e in es:
        for clave in ("stx2", "k", "coast2"):          # bin -> (suma, n)
            for b, (sm, n) in e[clave].items():
                s0, n0 = out[clave].get(b, (0.0, 0))
                out[clave][b] = (s0 + sm, n0 + n)
        for ln in e.get("_ajenas", []):                    # sin duplicar, en orden
            if ln not in out["_ajenas"]:
                out["_ajenas"].append(ln)
        for clave in ("shist", "chist2", "lhist", "ahist", "lcal"):   # bin -> n
            for b, n in e[clave].items():
                out[clave][b] = out[clave].get(b, 0) + n
        out["nocreep"] += e["nocreep"]
        if e["aMax"] > out["aMax"]:
            out["aMax"] = e["aMax"]
        if e["aMin"] > 0.0001 and (out["aMin"] <= 0.0001 or e["aMin"] < out["aMin"]):
            out["aMin"] = e["aMin"]
    # ⚠ chist2 vuelve a pasar por el minimo del motor DESPUES de fusionar: dos tomas con 12
    # muestras cada una SI son un dato de 24, pero una sola con 12 sigue sin serlo.
    if sum(out["chist2"].values()) < CREEP_MIN_SAMPLES:
        out["chist2"] = {}
    return out


def escribir(e, clase, wb, rueda, out_dir, fp=None, medido=None, fusionado=None, destino=None):
    """EXACTAMENTE el orden y el formato de BZEnvelopeStore.Save, y al FINAL la procedencia (: todo dato
    dice de donde salio): `fp,Mass,GearsCount,MaxSteeringAngle` (el Fingerprint ESTABLE con el que se midio),
    `medido,<tomas>,<frames>,<toma1>|<toma2>...` y `fusionado,<env1>|<env2>`. El motor ignora los kinds que no conoce
    (BZEnvelopeStore.ParseLine), asi que van despues de los suyos y no cambian nada de lo que lee."""
    p = destino if destino else os.path.join(out_dir, "envelope_%s.csv" % clase)
    L = []
    # ⚠ CUATRO campos, ni uno mas: el slot 2 es el steerMax viejo (el motor lo DESCARTA con
    # aviso) y un 5to campo dispara el aviso de "aMax previo descartado". Emitir de mas
    # ensucia el arranque con dos avisos que no corresponden.
    L.append("scalars,%s,0,%s" % (_f(rueda), _f(wb)))
    if e["nocreep"] > 0:
        L.append("nocreep,%d" % e["nocreep"])
    for b in sorted(e["stx2"]):
        s, n = e["stx2"][b]; L.append("stx2,%d,%s,%d" % (b, _f(s), n))
    for b in sorted(e["shist"]):
        L.append("shist,%d,%d" % (b, e["shist"][b]))
    for b in sorted(e["chist2"]):
        L.append("chist2,%d,%d" % (b, e["chist2"][b]))
    for b in sorted(e["lhist"]):
        L.append("lhist,%d,%d" % (b, e["lhist"][b]))
    for b in sorted(e.get("lcal", {})):        # piso medido en CALIBRACION; el emit nunca lo produce, solo lo conserva
        L.append("lcal,%d,%d" % (b, e["lcal"][b]))
    for b in sorted(e["ahist"]):
        L.append("ahist,%d,%d,%s,%s" % (b, e["ahist"][b], _f(e["aMin"]), _f(e["aMax"])))
    for b in sorted(e["k"]):
        s, n = e["k"][b]; L.append("k,%d,%s,%d" % (b, _f(s), n))
    for b in sorted(e["coast2"]):
        s, n = e["coast2"][b]; L.append("coast2,%d,%s,%d" % (b, _f(s), n))
    if fp:
        L.append("fp,%s,%s,%s" % fp)
    if medido:
        L.append("medido,%d,%d,%s" % (medido[0], medido[1], "|".join(medido[2])))
    if fusionado:
        L.append("fusionado,%s" % "|".join(fusionado))
    for ln in e.get("_ajenas", []):            # al final, tal cual entraron
        L.append(ln)
    io.open(p, "w", encoding="utf-8", newline="\n").write("\n".join(L) + "\n")
    return p, L


def _f(v):
    return ("%.6g" % v)


# ★★★ 981 (el usuario, 22/09): EL ENVELOPE SE LLENA CON LAS TOMAS, NO SE CONGELA EN LA PRIMERA.
# La guarda "NO SE PISA" protegia un aprendizaje online que no existe (BZEnvelopeRecorder no esta cableado en juego,
# ) y a cambio congelaba el envelope en la primera toma convertida: el ChernoBus_02, con 124.258 frames
# grabados, corria con una COPIA del envelope del _01. Ahora: si existe envelope_<clase>.csv y el Fingerprint ESTABLE
# es el mismo -> se fusiona (sumar bins) en silencio y la consola dice que crecio; si el Fingerprint cambio (el mod
# actualizo el vehiculo) -> NO se toca y se sale con codigo 3 para que el wizard pregunte SOBRE ESTA TOMA; una toma ya
# incorporada (por nombre, en la linea `medido`) no se suma dos veces. Comparar SOLO Mass, GearsCount y
# MaxSteeringAngle: Wheelbase y RminM se ESTIMAN de la toma y varian entre tomas del mismo vehiculo (53 de 53 en el
# ChernoBus_02 -> falso positivo seguro). El envelope es POR CLASE; --fusionar es para cuando el usuario, que sabe
# que dos clases son el mismo vehiculo, lo ordena.
FP_KEYS = ("mass", "gearsCount", "maxSteeringAngle")


def fingerprint_de(hk):
    """(Mass, GearsCount, MaxSteeringAngle) como strings del header, o None si el header no los trae."""
    if not hk:
        return None
    vals = []
    for k in FP_KEYS:
        v = hk.get(k)
        if v is None or v == "":
            return None
        vals.append(str(v).strip())
    return tuple(vals)


def fp_igual(a, b):
    """Igualdad NUMERICA de los 3 campos estables (1948.29 == 1948.290)."""
    if not a or not b:
        return True                      # sin firma de un lado no hay contradiccion que declarar
    try:
        return all(abs(float(x) - float(y)) < 1e-6 for x, y in zip(a, b))
    except ValueError:
        return a == b


def leer_envelope_csv(path):
    """El envelope de disco -> la misma estructura que acumular()/fusionar(), mas (rueda, wb, fp, medido, fusionado).
    medido = [n_tomas, n_frames, [tomas]]; los kinds que no se conocen se ignoran (como hace el motor)."""
    e = {"stx2": {}, "shist": {}, "chist2": {}, "lhist": {}, "ahist": {},
         "k": {}, "coast2": {}, "nocreep": 0, "aMin": 0.0, "aMax": 0.0, "lcal": {}, "_ajenas": []}
    rueda = 0.0
    wb = 0.0
    fp = None
    medido = [0, 0, []]
    fusionado = []
    for ln in io.open(path, encoding="utf-8", errors="replace"):
        p = ln.strip().split(",")
        if len(p) < 2:
            continue
        k = p[0]
        try:
            if k == "scalars" and len(p) >= 4:
                rueda = float(p[1]); wb = float(p[3])
            elif k == "nocreep":
                e["nocreep"] = int(float(p[1]))
            elif k in ("stx2", "k", "coast2") and len(p) >= 4:
                e[k][int(p[1])] = (float(p[2]), int(p[3]))
            elif k in ("shist", "chist2", "lhist", "lcal") and len(p) >= 3:
                e[k][int(p[1])] = int(p[2])
            elif k == "ahist" and len(p) >= 3:
                e["ahist"][int(p[1])] = int(p[2])
                if len(p) >= 5:
                    e["aMin"] = float(p[3]); e["aMax"] = float(p[4])
            elif k == "fp" and len(p) >= 4:
                fp = (p[1], p[2], p[3])
            elif k == "medido" and len(p) >= 3:
                medido = [int(p[1]), int(p[2]), [x for x in (p[3].split("|") if len(p) >= 4 else []) if x]]
            elif k == "fusionado":
                fusionado = [x for x in p[1].split("|") if x]
            else:
                # lo que este emit no conoce (lcalrun, un kind mas nuevo) se conserva tal cual, como el Store (920)
                e["_ajenas"].append(ln.rstrip("\r\n"))
        except ValueError:
            pass
    return e, rueda, wb, fp, medido, fusionado


def _bins(e):
    return {k: len(e.get(k, {})) for k in ("coast2", "k", "shist", "lhist", "chist2", "ahist", "stx2", "lcal")}


def aporte(e0, e1):
    """Texto de lo que CRECIO: bins antes->despues por campo, nocreep y muestras de coast."""
    b0 = _bins(e0); b1 = _bins(e1)
    partes = ["%s %d->%d" % (k, b0[k], b1[k]) for k in ("coast2", "k", "shist", "lhist", "chist2")]
    partes.append("nocreep %d->%d" % (e0["nocreep"], e1["nocreep"]))
    m0 = sum(n for s, n in e0["coast2"].values()); m1 = sum(n for s, n in e1["coast2"].values())
    partes.append("%+d muestras de coast" % (m1 - m0))
    return " \u00b7 ".join(partes)


def perdidas(e0, e1):
    """Que bins/muestras tenia e0 y no tiene e1 (lo que un --pisar borra). Vacio si no se pierde nada."""
    b0 = _bins(e0)
    b1 = _bins(e1)
    out = []
    for k in ("coast2", "k", "shist", "lhist", "chist2", "ahist", "stx2", "lcal"):
        if b1[k] < b0[k]:
            out.append("%s %d->%d bins" % (k, b0[k], b1[k]))
    if e1["nocreep"] < e0["nocreep"]:
        out.append("nocreep %d->%d" % (e0["nocreep"], e1["nocreep"]))
    return " \u00b7 ".join(out)


def _toma_id(frame):
    return os.path.basename(frame).rsplit(".", 1)[0]


def fusionar_archivos(envs, out_path, clase, forzar):
    """--fusionar a.csv b.csv [...] --out <archivo.csv | dir --clase X>: suma los envelopes que el USUARIO dice que son
    el mismo vehiculo (dos clases hermanas). Sin heuristicas de nombre ni de Fingerprint: lo ordena el usuario."""
    if len(envs) < 2:
        print("--fusionar necesita al menos dos envelopes")
        return 2
    partes = []
    fps = []
    mejor = (-1, 0.0, 0.0)
    medido = [0, 0, []]
    for ep in envs:
        if not os.path.isfile(ep):
            print("no existe: " + ep)
            return 2
        e, rueda, wb, fp, med, fus = leer_envelope_csv(ep)
        partes.append(e)
        fps.append(fp)
        muestras = sum(n for s, n in e["coast2"].values())
        if muestras > mejor[0]:
            mejor = (muestras, wb, rueda)
        medido[0] += med[0]; medido[1] += med[1]; medido[2] += med[2]
    firmas = [f for f in fps if f]
    if firmas and any(not fp_igual(firmas[0], f) for f in firmas[1:]):
        print("ATENCION: los envelopes tienen Fingerprint ESTABLE distinto: %s" % " vs ".join("/".join(f) for f in firmas))
        if not forzar:
            print("   -> no se fusiona. Si igual son el mismo vehiculo, repeti con --forzar.")
            return 3
    e = fusionar(partes)
    if os.path.isdir(out_path):
        if not clase:
            print("--out es un directorio: hace falta --clase para nombrar envelope_<clase>.csv")
            return 2
        destino = os.path.join(out_path, "envelope_%s.csv" % clase)
    else:
        destino = out_path
    nombres = [os.path.basename(x).rsplit(".", 1)[0] for x in envs]
    p, L = escribir(e, clase or "fusion", mejor[1], mejor[2], os.path.dirname(destino) or ".",
                    fp=(firmas[0] if firmas else None), medido=medido, fusionado=nombres, destino=destino)
    print("fusionados %d envelopes -> %s  (%d lineas)" % (len(envs), p, len(L)))
    print("  " + aporte(partes[0], e) + "  (contra el primero)")
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    clase = wb = None
    rueda = 0.0
    pisar = False
    fusion_mode = False
    forzar = False
    dir_tomas = None
    frames = []
    out_dir = None
    i = 1
    while i < len(argv):
        if argv[i] == "--clase": clase = argv[i+1]; i += 2
        elif argv[i] == "--wb":  wb = float(argv[i+1]); i += 2
        elif argv[i] == "--rueda": rueda = float(argv[i+1]); i += 2
        elif argv[i] == "--out": out_dir = argv[i+1]; i += 2
        elif argv[i] == "--pisar": pisar = True; i += 1
        elif argv[i] == "--fusionar": fusion_mode = True; i += 1
        elif argv[i] == "--forzar": forzar = True; i += 1
        elif argv[i] == "--dir": dir_tomas = argv[i+1]; i += 2
        elif argv[i].startswith("--"): i += 1
        else: frames.append(argv[i]); i += 1
    # dir <carpeta> --clase X: TODAS las tomas de esa clase de la carpeta del PathLogger, sin pasarlas una
    # por una (165 del ChernoBus no entran en una linea de comando de Windows). La clase se lee del header apareado.
    if dir_tomas:
        if not clase:
            print("--dir necesita --clase <VehicleClass>")
            return 2
        # Solo lo que nombro el RECORDER: frame_<n>-<hhmmss>_<clase>.csv. Un frame_MERGE*/SEQ* (producto de
        # frame_merge o de un banco) son las mismas tomas otra vez: contarlo duplica muestras. Se dice por nombre
        # lo que se saltea; si es una toma de verdad, se pasa explicita y se fusiona como cualquier otra.
        saltea = []
        for fn in sorted(os.listdir(dir_tomas)):
            if not (fn.startswith("frame_") and fn.endswith(".csv")):
                continue
            hpd = os.path.join(dir_tomas, fn.replace("frame_", "header_").rsplit(".", 1)[0] + ".txt")
            if not os.path.isfile(hpd):
                continue
            cls_d = None
            for ln in io.open(hpd, encoding="utf-8", errors="replace"):
                if ln.startswith("vehicleClass="):
                    cls_d = ln.strip().split("=", 1)[1]
            if cls_d != clase:
                continue
            if not re.match(r"frame_[0-9]+-[0-9]+_", fn):
                saltea.append(fn.rsplit(".", 1)[0])
                continue
            frames.append(os.path.join(dir_tomas, fn))
        print("--dir %s: %d tomas de %s" % (dir_tomas, len(frames), clase))
        if saltea:
            print("   saltea %d que no nombro el recorder (merge/banco; si es una toma, pasala explicita): %s" % (len(saltea), ", ".join(saltea)))
    if not frames:
        print(__doc__)
        return 2
    if fusion_mode:
        return fusionar_archivos(frames, out_dir or os.path.dirname(os.path.abspath(frames[0])), clase, forzar)
    frame = frames[0]
    if out_dir is None:
        out_dir = os.path.dirname(os.path.abspath(frame))
    # Sin --clase/--wb se leen del header apareado (misma convencion que frame_to_route:
    # frame_X.csv -> header_X.txt, mismo dir). Asi el wizard llama con el frame solo.
    hk = {}
    hp = frame.replace("frame_", "header_").rsplit(".", 1)[0] + ".txt"
    if os.path.isfile(hp):
        for ln in io.open(hp, encoding="utf-8"):
            if "=" in ln:
                k, v = ln.strip().split("=", 1)
                hk[k] = v
    if not clase or not wb:
        if hk:
            if not clase:
                clase = hk.get("vehicleClass")
            if not wb and hk.get("wheelbase"):
                wb = float(hk["wheelbase"])
    if not clase or not wb:
        print("faltan --clase / --wb (y no hay header apareado del que sacarlos)")
        return 2
    # ⚠ --rueda es OPCIONAL: el header no trae radio y la API de DayZ tampoco lo expone (el
    # motor lo ESTIMA online, r = v/omega). Con 0 el reader cae a su fallback (FB_WHEEL_R,
    # BZVehicleDataReader:216) y el primer drive lo mide y lo pisa -- exactamente lo que el
    # propio motor guarda antes de que su estimador converja. No se inventa un radio.
    fpN = fingerprint_de(hk)
    destino = os.path.join(out_dir, "envelope_%s.csv" % clase)
    # ★ MULTI-TOMA: varias tomas de la MISMA clase se FUSIONAN. El `idle` se
    # estima POR TOMA (percentil 5 de las rpm con motor andando), asi que cada una tiene
    # que pasar por `acumular` con el suyo -- un ralenti calculado sobre la mezcla seria un
    # promedio sin sentido fisico. Y el `wheelbase` de `scalars` sale de la toma MAS LARGA,
    # que es la que mejor lo estima.
    e0 = None
    rueda0 = 0.0
    wb0 = 0.0
    fp0 = None
    medido0 = [0, 0, []]
    if os.path.exists(destino) and not pisar:
        e0, rueda0, wb0, fp0, medido0, fus0 = leer_envelope_csv(destino)
        if not fp_igual(fp0, fpN):
            print("ENVELOPE_VEHICULO_CAMBIO envelope_%s.csv: medido con Mass %s / %s marchas / steer %s ; esta toma dice Mass %s / %s marchas / steer %s"
                  % ((clase,) + tuple(fp0) + tuple(fpN)))
            print("   -> el vehiculo cambio (actualizacion del mod?): NO se fusiona. --pisar regenera el envelope desde esta toma.")
            return 3
        ya = [f for f in frames if _toma_id(f) in medido0[2]]
        if ya:
            print("envelope_%s.csv ya incorpora: %s" % (clase, ", ".join(_toma_id(f) for f in ya)))
        frames = [f for f in frames if _toma_id(f) not in medido0[2]]
        if not frames:
            print("   -> sin cambios (una toma no se suma dos veces).")
            return 0
    # pisar sobre un envelope existente: se dice QUE SE PIERDE (: el lhist no se repone con tomas de
    # ruta; lo que habia medido en juego se recupera con --fusionar <backup> <este> --forzar).
    e_pisado = None
    if os.path.exists(destino) and pisar:
        e_pisado = leer_envelope_csv(destino)[0]
    partes = []
    filas_tot = 0
    dur_tot = 0.0
    mas_larga = (0, frames[0])
    for fr in frames:
        fil_i = leer(fr)
        # ★ LA GUARDA VA ACA, ANTES DE `escribir`. Ese era el modo de falla real: con `fil`
        # vacio, `acumular` devolvia contadores en cero, `escribir` IGUAL generaba el archivo
        # --"scalars,0,0,<wb>", 20 bytes, cero bins: el STUB-- y recien DESPUES reventaba el
        # IndexError al imprimir "toma: %d filas". O sea que el tool no solo fallaba: dejaba
        # un envelope ENVENENADO en disco, que el motor despues lee como "ni medido ni
        # medido-en-cero" y cae al prior de caja. Un stub es PEOR que no tener envelope.
        exigir_filas(fil_i, fr)
        ei = acumular(fil_i)
        ei["k"] = acumular_k(fil_i, wb)
        partes.append(ei)
        filas_tot += len(fil_i)
        dur_tot += fil_i[-1][T] - fil_i[0][T]
        if len(fil_i) > mas_larga[0]:
            mas_larga = (len(fil_i), fr)
    if len(partes) > 1:
        e_new = fusionar(partes)
        print("fusion de %d tomas: %d filas . %.1f s  (wheelbase de la mas larga: %s)"
              % (len(partes), filas_tot, dur_tot, os.path.basename(mas_larga[1])))
    else:
        e_new = partes[0]
        print("toma: %d filas . %.1f s" % (filas_tot, dur_tot))
    medido = [len(frames), filas_tot, [_toma_id(f) for f in frames]]
    if e0 is not None:
        # EL ENVELOPE SE LLENA: existente + toma(s) nueva(s). scalars del lado con mas frames medidos (o el existente
        # si el de disco no dice cuantos: no se cambia lo que ya estaba por una toma corta).
        e = fusionar([e0, e_new])
        wb_out = wb0 if (wb0 > 0 and (medido0[1] == 0 or medido0[1] >= filas_tot)) else wb
        rueda_out = rueda0 if rueda0 > 0 else rueda
        medido = [medido0[0] + medido[0], medido0[1] + medido[1], medido0[2] + medido[2]]
        p, L = escribir(e, clase, wb_out, rueda_out, out_dir, fp=(fp0 or fpN), medido=medido)
        print("envelope_%s.csv CRECIO: %s  (tomas %d->%d)" % (clase, aporte(e0, e), medido0[0], medido[0]))
    else:
        e = e_new
        # LO CALIBRADO NO SALE DE LAS TOMAS Y NO SE PIERDE AL REGENERAR. `lcal` (piso de despegue medido
        # con la rampa en calibracion) se conserva tal cual: el emit nunca lo produce, asi que nunca tiene con que
        # reemplazarlo. Se dice por consola.
        if e_pisado is not None and e_pisado.get("_ajenas"):
            e["_ajenas"] = list(e_pisado["_ajenas"])   # lcalrun y lo que venga sobreviven al --pisar
        if e_pisado is not None and e_pisado.get("lcal"):
            e["lcal"] = dict(e_pisado["lcal"])
            print("envelope_%s.csv conserva lcal (calibracion de launch): %d muestras" % (clase, sum(e["lcal"].values())))
        p, L = escribir(e, clase, wb, rueda, out_dir, fp=fpN, medido=medido)
        if e_pisado is not None:
            perd = perdidas(e_pisado, e)
            if perd:
                print("envelope_%s.csv PISADO: pierde %s  (lo que las tomas no reponen vuelve con --fusionar <backup> <este> --forzar)" % (clase, perd))
            else:
                print("envelope_%s.csv PISADO: sin perdidas (las tomas cubren todo lo que habia)" % clase)
    print("emitido: %s  (%d lineas)" % (p, len(L)))
    print("  stx2   %2d/%d baldes    shist  %2d bins   chist2 %2d bins%s"
          % (len(e["stx2"]), STX_BUCKETS, len(e["shist"]), len(e["chist2"]),
             "   [NO REPTA: %d ticks quieto]" % e["nocreep"] if e["nocreep"] else ""))
    print("  lhist  %2d bins        ahist  %2d bins   k %2d bins   coast2 %2d bins"
          % (len(e["lhist"]), len(e["ahist"]), len(e["k"]), len(e["coast2"])))
    print("  \u2605 los bins vacios quedan vacios A PROPOSITO: es el mapa de ESTAS tomas.")
    if not e["chist2"] and not e["nocreep"] and RECHAZOS:
        # Sin creep NI nocreep el motor cae al prior de caja. Decir POR QUE, que es lo que
        # convierte "no salio" en "falta esto en la grabacion".
        print("  ! sin creep ni nocreep -> el motor cae al PRIOR DE CAJA. Ticks descartados:")
        for k in sorted(RECHAZOS, key=lambda x: -RECHAZOS[x]):
            print("      %-16s %d" % (k, RECHAZOS[k]))
        print("      -> frena en LLANO, solta todo y ESPERA EL RALENTI (2-3 s) antes de cortar,")
        print("         con el motor ENCENDIDO. Ver --clase multi-toma para hacerlo aparte.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
