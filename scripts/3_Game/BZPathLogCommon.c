// ============================================================================
//  BZ_AutoDrive_PathLogger - constantes y helpers
// ============================================================================

enum BZPathLogState {
    OFF      = 0,
    RECORDING = 1,
    PAUSED   = 2
};

class BZPathLog {
    static void Info(string msg)  { Print("[BZPathLog] "        + msg); }
    static void Warn(string msg)  { Print("[BZPathLog][WARN] "  + msg); }
    static void Err(string msg)   { Print("[BZPathLog][ERROR] " + msg); }
}
