# ============================================================================
#  i18n_strings.ps1 - tabla de strings del wizard en ES / EN
#
#  Uso:
#    . .\i18n_strings.ps1
#    Set-BZLang "en"          # o "es" (default)
#    T "menu.title"           # devuelve el string en el idioma activo
#    (T "convert.input") -f $path   # strings con placeholders {0}, {1}...
#
#  Convencion de claves: "seccion.subclave". Fallback: si falta en el idioma
#  activo, usa ES; si falta en ES, devuelve la clave (para detectar huecos).
#
#  Para agregar idioma: duplicar el bloque y traducir los valores (mismas claves).
# ============================================================================

$global:BZ_LANG = "es"

function Set-BZLang {
    param([string]$lang)
    if ($BZ_STRINGS.ContainsKey($lang)) { $global:BZ_LANG = $lang }
    else { Write-Host "Idioma '$lang' no disponible, usando 'es'." -ForegroundColor Yellow; $global:BZ_LANG = "es" }
}

function T {
    param([string]$key)
    $s = $null
    if ($BZ_STRINGS[$BZ_LANG].ContainsKey($key)) { $s = $BZ_STRINGS[$BZ_LANG][$key] }
    elseif ($BZ_STRINGS["es"].ContainsKey($key)) { $s = $BZ_STRINGS["es"][$key] }  # fallback ES
    else { $s = $key }  # fallback: la clave cruda (marca el hueco)
    return $s
}

$global:BZ_STRINGS = @{

    # ----------------------------------------------------------------- ESPAÑOL
    es = @{
        # comunes
        "common.enter_continue" = "[Enter para continuar...]"
        "common.choice"         = "Eleccion:"
        "common.choice_num"     = "Eleccion (numero):"
        "common.invalid"        = "Invalido, reintenta."
        "common.back"           = "Volver"
        "common.exit"           = "Salir"
        "common.yes_no"         = "[S/n]"
        "common.bye"            = "Adios."

        # seleccion de idioma
        "lang.prompt"           = "Idioma / Language:"
        "lang.opt_es"           = "[1] Espanol"
        "lang.opt_en"           = "[2] English"

        # menu principal
        "menu.title"            = "BZ_AutoDrive - Route Wizard"
        "menu.subtitle"         = "Pipeline: grabacion -> conversion -> deploy"
        "menu.header"           = "Menu principal"
        "menu.convert"          = "[1] Convertir CSV grabado -> JSON nueva toma"
        "menu.import_v1"        = "[2] Importar ruta de BrigadaZ Transport v1 (sin regrabar)"
        "menu.quit"             = "[Q] Salir"

        # importar ruta de BrigadaZ Transport v1 (Select-TransportRoute + Invoke-ImportTransport)
        "imp.title"             = "Importar ruta de BrigadaZ Transport v1"
        "imp.header"            = "Rutas de BrigadaZ Transport encontradas"
        "imp.none"              = "No encontre ninguna ruta de BrigadaZ Transport en:"
        "imp.no_dir"            = "(todavia no me indicaste donde buscar)"
        "imp.where"             = "La ruta de v1 NO esta en el PBO: vive en el profile de TU server. La carpeta puede llamarse de cualquier forma (un repack la cambia), asi que decime donde buscar con [B] y me acuerdo. Apuntame a profiles\ y la encuentro adentro."
        "imp.opt_browse"        = "[B] Buscar en otra carpeta (se busca adentro de todas sus subcarpetas)"
        "imp.ask_browse"        = "Carpeta donde buscar (o el .json directo)"
        "imp.opt_paste"         = "[P] Pegar la ruta completa a un .json"
        "imp.ask_paste"         = "Path al .json de BrigadaZ Transport"
        "imp.not_found"         = "No existe: {0}"
        "imp.fp_title"          = "Identidad del vehiculo"
        "imp.fp_header"         = "De que vehiculo es esta ruta?"
        "imp.fp_intro"          = "Wheelbase y Fingerprint dependen del VEHICULO, no de la ruta. Salen de cualquier grabacion del vehiculo, aunque dure 10 segundos."
        "imp.fp_rec"            = "grabacion"
        "imp.fp_route"          = "ruta convertida"
        "imp.fp_route_note"     = "incluye el freno medido"
        "imp.fp_match"          = "coincide con la ruta"
        "imp.fp_more"           = "(+{0} mas, ordenados por relevancia: escribi el numero igual, o [P] para pegar un path)"
        "imp.fp_paste"          = "[P] Pegar la ruta a un header_*.txt o a un _hdr.json"
        "imp.ask_paste_fp"      = "Path al header_*.txt o _hdr.json"
        "imp.fp_none"           = "[0] Sin identidad (la ruta anda, pero el control no sale fiel)"
        "imp.fp_empty"          = "No hay grabaciones ni tomas calibradas todavia. Subite al vehiculo, graba 10 segundos y volve."
        "imp.confirm"           = "Confirmar importacion? [S/n]"
        "imp.kv_input"          = "Ruta v1"
        "imp.kv_vehicle"        = "Vehiculo"
        "imp.kv_output"         = "Salida"
        "imp.ok"                = "Importacion OK."

        # conversion (Select-Csv + Invoke-CsvConvert)
        "csv.title"             = "Convertir grabacion (CSV) a ruta (JSON)"
        "csv.header"            = "CSVs disponibles (PathLogger)"
        "csv.none"              = "No se encontraron CSVs de grabacion en:"
        "convert.title"         = "Convertir CSV -> JSON"
        "convert.kv_input"      = "CSV input"
        "convert.kv_output"     = "JSON output"
        "convert.kv_backup"     = "Backup actual"
        "convert.backup_auto"   = "automatico"
        "convert.confirm"       = "Confirmar conversion? [S/n]"
        "convert.ok"            = "Conversion OK."
        "convert.no_converter"  = "csv_to_route.ps1 no encontrado en {0}"
        "convert.error"         = "Error: {0}"
        "convert.ask_name"      = "Nombre de la ruta (Enter = la activa 'BZBusRoute')"
        "convert.deployed"      = "Ruta '{0}' lista y desplegada -> aparece en el Reproductor (carga en caliente, sin reiniciar)."
        "convert.name_exists"   = "Ya existe una ruta con ese nombre ({0})."
        "convert.overwrite_q"   = "Sobrescribir? (se hace un backup automatico igual) [S/N]"
        "convert.name_retry"    = "-> OK, elegi otro nombre."
        "convert.aronway_q"         = "AR_OnWay (escudo vs obstruccion externa):  [T] Transporte robusto (frena + escapa)   [I] Interceptable (frena y se queda)   [N] Ninguno"
        "convert.aronway_robust"    = "-> Transporte robusto: frena ante otro vehiculo y se escapa pasado el obstaculo si lo bloquean o chocan."
        "convert.aronway_intercept" = "-> Interceptable: frena pero NO se escapa (queda bloqueado) -> para misiones donde el jugador lo intercepta."
        "convert.aronway_none"      = "-> Ninguno: sin AR_OnWay (replica pura)."

        # configuracion de paths (menu.config + cfgp + pathchk)
        "menu.config"           = "[6] Configurar paths (carpetas del server)"
        "cfgp.title"            = "Configurar paths"
        "cfgp.kv_routes"        = "SERVIDOR (carpeta de rutas)"
        "cfgp.kv_client"        = "CLIENTE (carpeta de grabaciones)"
        "cfgp.kv_mirror"        = "2do server (opcional)"
        "cfgp.kv_reports"       = "INFORMES (carpeta de salida)"
        "cfgp.hint"             = "Enter = mantener cada uno.  2do server / Informes: '-' = ninguno.  Informes vacio = derivado del SERVIDOR.  Las grabaciones del server se derivan del SERVIDOR."
        "cfgp.ask_routes"       = "Nuevo SERVIDOR / carpeta de rutas (Enter = mantener)"
        "cfgp.ask_client"       = "Nuevo CLIENTE / carpeta de grabaciones (Enter = mantener)"
        "cfgp.ask_mirror"       = "2do server, opcional (Enter = mantener, '-' = ninguno)"
        "cfgp.ask_reports"      = "INFORMES, opcional (Enter = mantener, '-' = derivado del SERVIDOR)"
        "cfgp.warn_missing"     = "Ojo: la carpeta '{0}' no existe todavia."
        "cfgp.saved"            = "Guardado en {0}."
        "pathchk.title"         = "Chequeo de carpetas"
        "pathchk.intro"         = "Hay carpetas configuradas que no existen o no estan en linea:"
        "pathchk.opt_edit"      = "[1] Cambiar paths ahora"
        "pathchk.opt_cont"      = "[2] Continuar igual"
        "pathchk.prompt"        = "Elegi (Enter = continuar)"
        "convert.mirror_hint"   = "Server B configurado: copia el par _hdr.json + _wp.csv a {0} tambien."
    }

    # ----------------------------------------------------------------- ENGLISH
    en = @{
        # common
        "common.enter_continue" = "[Press Enter to continue...]"
        "common.choice"         = "Choice:"
        "common.choice_num"     = "Choice (number):"
        "common.invalid"        = "Invalid, try again."
        "common.back"           = "Back"
        "common.exit"           = "Exit"
        "common.yes_no"         = "[Y/n]"
        "common.bye"            = "Goodbye."

        # language selection
        "lang.prompt"           = "Idioma / Language:"
        "lang.opt_es"           = "[1] Espanol"
        "lang.opt_en"           = "[2] English"

        # main menu
        "menu.title"            = "BZ_AutoDrive - Route Wizard"
        "menu.subtitle"         = "Pipeline: recording -> conversion -> deploy"
        "menu.header"           = "Main menu"
        "menu.convert"          = "[1] Convert recorded CSV -> new route JSON"
        "menu.import_v1"        = "[2] Import a BrigadaZ Transport v1 route (no re-recording)"
        "menu.quit"             = "[Q] Quit"

        # import a BrigadaZ Transport v1 route (Select-TransportRoute + Invoke-ImportTransport)
        "imp.title"             = "Import a BrigadaZ Transport v1 route"
        "imp.header"            = "BrigadaZ Transport routes found"
        "imp.none"              = "No BrigadaZ Transport route found in:"
        "imp.no_dir"            = "(you haven't told me where to look yet)"
        "imp.where"             = "The v1 route is NOT inside the PBO: it lives in YOUR server profile. That folder can be named anything (a repack renames it), so tell me where to look with [B] and I'll remember. Point me at profiles\ and I'll find it in there."
        "imp.opt_browse"        = "[B] Look in another folder (searches all its subfolders)"
        "imp.ask_browse"        = "Folder to search (or the .json itself)"
        "imp.opt_paste"         = "[P] Paste the full path to a .json"
        "imp.ask_paste"         = "Path to the BrigadaZ Transport .json"
        "imp.not_found"         = "Not found: {0}"
        "imp.fp_title"          = "Vehicle identity"
        "imp.fp_header"         = "Which vehicle is this route for?"
        "imp.fp_intro"          = "Wheelbase and Fingerprint depend on the VEHICLE, not on the route. Any recording of that vehicle works, even a 10-second one."
        "imp.fp_rec"            = "recording"
        "imp.fp_route"          = "converted route"
        "imp.fp_route_note"     = "includes the measured braking"
        "imp.fp_match"          = "matches the route"
        "imp.fp_more"           = "(+{0} more, sorted by relevance: type the number anyway, or [P] to paste a path)"
        "imp.fp_paste"          = "[P] Paste the path to a header_*.txt or a _hdr.json"
        "imp.ask_paste_fp"      = "Path to the header_*.txt or _hdr.json"
        "imp.fp_none"           = "[0] No identity (the route runs, but control won't be faithful)"
        "imp.fp_empty"          = "No recordings or calibrated routes yet. Get in the vehicle, record 10 seconds and come back."
        "imp.confirm"           = "Confirm import? [Y/n]"
        "imp.kv_input"          = "v1 route"
        "imp.kv_vehicle"        = "Vehicle"
        "imp.kv_output"         = "Output"
        "imp.ok"                = "Import OK."

        # conversion (Select-Csv + Invoke-CsvConvert)
        "csv.title"             = "Convert recording (CSV) to route (JSON)"
        "csv.header"            = "Available CSVs (PathLogger)"
        "csv.none"              = "No recording CSVs found in:"
        "convert.title"         = "Convert CSV -> JSON"
        "convert.kv_input"      = "CSV input"
        "convert.kv_output"     = "JSON output"
        "convert.kv_backup"     = "Current backup"
        "convert.backup_auto"   = "automatic"
        "convert.confirm"       = "Confirm conversion? [Y/n]"
        "convert.ok"            = "Conversion OK."
        "convert.no_converter"  = "csv_to_route.ps1 not found in {0}"
        "convert.error"         = "Error: {0}"
        "convert.ask_name"      = "Route name (Enter = the active 'BZBusRoute')"
        "convert.deployed"      = "Route '{0}' ready and deployed -> shows up in the Player (hot-load, no restart)."
        "convert.name_exists"   = "A route with that name already exists ({0})."
        "convert.overwrite_q"   = "Overwrite? (an automatic backup is made anyway) [Y/N]"
        "convert.name_retry"    = "-> OK, pick another name."
        "convert.aronway_q"         = "AR_OnWay (shield vs external obstruction):  [T] Robust transport (brakes + escapes)   [I] Interceptable (brakes and stays)   [N] None"
        "convert.aronway_robust"    = "-> Robust transport: brakes for another vehicle and escapes past the obstacle if blocked or rammed."
        "convert.aronway_intercept" = "-> Interceptable: brakes but does NOT escape (stays blocked) -> for missions where the player intercepts it."
        "convert.aronway_none"      = "-> None: no AR_OnWay (pure replay)."

        # path configuration (menu.config + cfgp + pathchk)
        "menu.config"           = "[6] Configure paths (server folders)"
        "cfgp.title"            = "Configure paths"
        "cfgp.kv_routes"        = "SERVER (routes folder)"
        "cfgp.kv_client"        = "CLIENT (recordings folder)"
        "cfgp.kv_mirror"        = "2nd server (optional)"
        "cfgp.kv_reports"       = "REPORTS (output folder)"
        "cfgp.hint"             = "Enter = keep each.  2nd server / Reports: '-' = none.  Reports empty = derived from SERVER.  Server recordings are derived from SERVER."
        "cfgp.ask_routes"       = "New SERVER / routes folder (Enter = keep)"
        "cfgp.ask_client"       = "New CLIENT / recordings folder (Enter = keep)"
        "cfgp.ask_mirror"       = "2nd server, optional (Enter = keep, '-' = none)"
        "cfgp.ask_reports"      = "REPORTS, optional (Enter = keep, '-' = derived from SERVER)"
        "cfgp.warn_missing"     = "Note: folder '{0}' doesn't exist yet."
        "cfgp.saved"            = "Saved to {0}."
        "pathchk.title"         = "Folder check"
        "pathchk.intro"         = "Some configured folders are missing or offline:"
        "pathchk.opt_edit"      = "[1] Change paths now"
        "pathchk.opt_cont"      = "[2] Continue anyway"
        "pathchk.prompt"        = "Choose (Enter = continue)"
        "convert.mirror_hint"   = "Server B configured: copy the _hdr.json + _wp.csv pair to {0} as well."
    }
}
