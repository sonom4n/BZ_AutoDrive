// Expone el emote/gesto que el player esta ejecutando AHORA.
// m_CurrentGestureID es el campo que usa toda la logica de emotes de EmoteManager,
// pero es protected y NO tiene getter publico: GetGesture() devuelve m_GestureID
// (otro campo, seteado por SetGesture(), no necesariamente el emote en curso).
// Como modded class si podemos leer el protected.
// Lo usa BZBusService.CheckHailGesture (parada a demanda: el player hace OK -> Boris frena).
modded class EmoteManager
{
    int BZ_CurrentGesture()
    {
        return m_CurrentGestureID;
    }
}
