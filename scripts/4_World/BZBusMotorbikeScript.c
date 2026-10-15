// ============================================================================
//  BZBusMotorbikeScript - la cascara de la MOTO (DayZ 1.30 en adelante; MotorbikeScript no es CarScript).
//
//  Los mismos ganchos que la cascara del auto (BZBusCarScript), con el mismo cuerpo compartido (BZVehiculoGanchos):
//   - OnInput         el runner dueno manda (su conductor de moto sobre la cinta, o el comando cacheado)
//   - EOnPostSimulate el grabador cuadro a cuadro del cliente que maneja (frame_), sin caja automatica
//   - EEHitBy         los avisos de ambush / choque
//  y el VEHICULO INVIOLABLE (las piezas no se sueltan mientras el framework lo maneja), por NetSync como en el auto.
//  Las luces del framework no se sincronizan en la moto todavia (la moto tiene su propio componente de luces).
// ============================================================================
#ifndef DAYZ_1_29
modded class MotorbikeScript {

    // el adaptador de este vehiculo (BZVehiculoMoto): lo crea la fabrica la primera vez que un gancho lo pide
    ref BZVehiculo m_BZV;
    bool m_BZPartsLocked;   // NetSync: VEHICULO INVIOLABLE, lo fija el runner; false = vanilla
    // el ultimo contacto en la zona DELANTERA (GetTickTime; -1 = ninguno): el conductor de moto no da gas contra un choque
    float m_BzContactoFrenteT = -1;
    string m_BzContactoFrenteClase = "";
    // el ultimo contacto de CUALQUIER zona (sin la tripulacion), el de mayor impulso de medio segundo: solo lo muestra la
    // linea del choque (zona, con que, impulso); el choque de frente sigue leyendo lo de arriba
    float m_BzContactoT = -1;
    string m_BzContactoZona = "";
    string m_BzContactoClase = "";
    float m_BzContactoImp;

    BZVehiculo BZV() {
        if (!m_BZV) m_BZV = BZVehiculoFabrica.Para(this);
        return m_BZV;
    }

    void MotorbikeScript() {
        RegisterNetSyncVariableBool("m_BZPartsLocked");
    }

    void BZSetPartsLocked(bool locked) {
        if (!GetGame().IsServer()) return;
        if (m_BZPartsLocked == locked) return;
        m_BZPartsLocked = locked;
        SetSynchDirty();
    }

    override bool CanReleaseAttachment(EntityAI attachment) {
        if (m_BZPartsLocked) return false;
        return super.CanReleaseAttachment(attachment);
    }

    override void OnContact(string zoneName, vector localPos, IEntity other, Contact data) {
        super.OnContact(zoneName, localPos, other, data);
        BZFrameRecorder.Contacto(this, zoneName, other);   // la columna `contacto` de la toma (cliente que graba)
        if (!GetGame().IsServer()) return;
        // el propio chofer (un tripulante de esta moto) no es un choque (laboratorio de motos, herramienta aparte, 27/09:
        // "Engine/Back con SurvivorM_Mirek" en todos los replays)
        Object objC = Object.Cast(other);
        if (objC) {
            int iTr;
            for (iTr = 0; iTr < CrewSize(); iTr++) {
                if (CrewMember(iTr) && CrewMember(iTr) == objC) return;
            }
        }
        // el ultimo contacto, cualquier zona: el de mayor impulso dentro de medio segundo ("terreno" = `other` nulo)
        float ahoraC = GetGame().GetTickTime();
        float impC = data.Impulse;
        if (impC < 0) impC = -impC;
        if (m_BzContactoT < 0 || ahoraC - m_BzContactoT > 0.5 || impC > m_BzContactoImp) {
            m_BzContactoT = ahoraC;
            m_BzContactoImp = impC;
            m_BzContactoZona = zoneName;
            m_BzContactoClase = "terreno";
            if (objC) m_BzContactoClase = objC.GetType();
            if (objC && m_BzContactoClase == "") m_BzContactoClase = "modelo " + objC.GetModelName();
        }
        string zonaC = zoneName;
        zonaC.ToLower();
        if (zonaC.IndexOf("front") < 0) return;
        m_BzContactoFrenteT = GetGame().GetTickTime();
        m_BzContactoFrenteClase = "terreno";
        if (objC) m_BzContactoFrenteClase = objC.GetType();
        if (m_BzContactoFrenteClase == "") m_BzContactoFrenteClase = "objeto sin clase";
    }

    override void OnInput(float dt) {
        super.OnInput(dt);
        if (!GetGame().IsServer()) return;
        BZVehiculoGanchos.OnInput(this, BZV(), dt);
    }

    override void EOnPostSimulate(IEntity other, float timeSlice) {
        super.EOnPostSimulate(other, timeSlice);
        BZVehiculoGanchos.PostSimulate(this, BZV(), timeSlice);
    }

    override void EEHitBy(TotalDamageResult damageResult, int damageType, EntityAI source, int component, string dmgZone, string ammo, vector modelPos, float speedCoef) {
        super.EEHitBy(damageResult, damageType, source, component, dmgZone, ammo, modelPos, speedCoef);
        BZVehiculoGanchos.Golpe(this, source);
    }
}
#endif
