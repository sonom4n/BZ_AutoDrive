// ============================================================================
//  BZWorldProbe.c - BZ_AutoDrive 1.2
//
//  LA UNICA PUERTA AL MUNDO del cerebro (SEAM.md 1.1). Consultas que NO son del
//  vehiculo (piso bajo las ruedas, altura del terreno). Dos metodos, sin estado.
//
//  La clase base es el NULL OBJECT: "todo asfalto, terreno a la altura dada". Sirve
//  para testear los modulos sin mundo. La version viva la inyecta el Runner.
//
//  Es la excepcion ACOTADA a "el cerebro no toca el motor": entra por interfaz, es
//  stubeable, y no guarda nada. No es un atajo al Car.
// ============================================================================

class BZWorldProbe
{
    int SurfaceKindAt(vector pos)
    {
        return BZSurf.HARD;
    }

    float GroundYAt(float x, float z)
    {
        return 0;
    }

    // F1: antes de reversar, ¿hay OTRO vehiculo en ese punto? El propio casco se excluye con SetPropio.
    void SetPropio(Object o)
    {
    }

    bool VehiculoAjenoEn(vector pos, float radio)
    {
        return false;
    }

    // la clase del primer OBSTACULO con colision dentro del radio, o "" si no hay. Cuenta todo
    // lo que un vehiculo no atraviesa (otro vehiculo, una garita, un farol, un guardrail, un arbol, una roca, una pared); no
    // cuentan las personas, los items sueltos, los infectados, los animales ni los arbustos.
    string ObstaculoEn(vector pos, float radio)
    {
        return "";
    }
}

// Version viva: consulta el motor. La usa el Runner; en test se usa la base.
class BZWorldProbeLive extends BZWorldProbe
{
    // Clasificacion GRUESA duro/blando. Los nombres salen de la tabla de v1.1
    // (GetSurfaceRolling/GetSurfaceFriction), que ya estaba validada in-game.
    // Es a proposito binaria: el envelope mide CoastDecel por estas dos clases, asi
    // que partirlo mas fino sin medicion que lo respalde seria inventar precision.
    override int SurfaceKindAt(vector pos)
    {
        string surf = "";
        GetGame().SurfaceGetType3D(pos[0], pos[1], pos[2], surf);
        if (surf == "") return BZSurf.HARD;
        if (surf.Contains("asphalt")) return BZSurf.HARD;
        if (surf.Contains("concrete")) return BZSurf.HARD;
        if (surf.Contains("road")) return BZSurf.HARD;
        return BZSurf.SOFT;
    }

    override float GroundYAt(float x, float z)
    {
        return GetGame().SurfaceY(x, z);
    }

    private Object m_Propio;

    override void SetPropio(Object o)
    {
        m_Propio = o;
    }

    // Misma API que SpawnSpotOccupied / DestinoOcupadoAr del servicio (GetObjectsAtPosition3D + CarScript).
    override bool VehiculoAjenoEn(vector pos, float radio)
    {
        array<Object> objsV = new array<Object>;
        array<CargoBase> proxV = new array<CargoBase>;
        GetGame().GetObjectsAtPosition3D(pos, radio, objsV, proxV);
        int iV;
        for (iV = 0; iV < objsV.Count(); iV++)
        {
            Object oV = objsV.Get(iV);
            if (!oV) continue;
            if (oV == m_Propio) continue;
            if (BZVehiculoFabrica.EsVehiculo(oV)) return true;
        }
        return false;
    }

    override string ObstaculoEn(vector pos, float radio)
    {
        array<Object> objsO = new array<Object>;
        array<CargoBase> proxO = new array<CargoBase>;
        GetGame().GetObjectsAtPosition3D(pos, radio, objsO, proxO);
        int iO;
        for (iO = 0; iO < objsO.Count(); iO++)
        {
            Object oO = objsO.Get(iO);
            if (!oO) continue;
            if (oO == m_Propio) continue;
            if (oO.IsMan()) continue;
            if (ItemBase.Cast(oO)) continue;
            if (DayZInfected.Cast(oO)) continue;
            if (DayZAnimal.Cast(oO)) continue;
            if (oO.IsBush()) continue;
            return oO.GetType();
        }
        return "";
    }
}
