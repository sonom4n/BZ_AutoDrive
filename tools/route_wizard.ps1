# ============================================================================
#  route_wizard.ps1 - CONVERSOR PURO de grabaciones a rutas
#
#  Flujo (unico):
#    1. Selecciona una grabacion (frame_*.csv del PathLogger)
#    2. La convierte a ruta (BZBusRoute_<nombre>.json + _hdr.json + _wp.csv) con
#       frame_to_route.py, deployada en el profile del server (carga en caliente)
#    3. Opcionalmente mirror del par al server B
#
#  Uso:
#    .\route_wizard.ps1
#    .\route_wizard.ps1 -RoutesDir "G:\MiServer\profiles\BZ_AutoDrive\"
# ============================================================================

param(
    [string]$RoutesDir = "",   # vacio: la primera corrida lo pide en la pantalla de paths (una sola) y queda en wizard_config.json
    [string]$ServerBMirror = "",   # PUBLISH: vacio por default -> un admin nuevo NO hereda el mirror (no ve "server B"). un jugador lo tiene persistido en su wizard_config; se setea desde [6] si hay un 2do server.
    [string]$Lang = ""   # "es" | "en". Vacio = preguntar al inicio.
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "i18n_strings.ps1")

# ★ PYTHON EMBEBIDO: el wizard trae su interprete en tools\python\
# (python-3.12-embed: sin instalador, sin PATH, sin pip; el toolchain es stdlib pura). Orden: el embebido, que viaja
# con el mod -> el `python` del PATH (la PC del que desarrolla) -> si no hay ninguno, UN mensaje claro al ARRANCAR,
# no un "The term python is not recognized" a mitad de la conversion.
# --- PY RESOLVER BEGIN ---
$script:PY = Join-Path $PSScriptRoot "python\python.exe"
if (-not (Test-Path $script:PY)) {
    $script:PY = $null
    $pyCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($pyCmd) { $script:PY = $pyCmd.Source }
}
if (-not $script:PY) {
    Write-Host ""
    Write-Host "  Falta Python: no esta tools\python\python.exe (el embebido que viaja con el mod) ni python en el PATH." -ForegroundColor Red
    Write-Host "  Python missing: neither tools\python\python.exe (bundled with the mod) nor python in PATH." -ForegroundColor Red
    Write-Host "  -> reinstala el mod (tools\python\ viene adentro) o instala Python 3  /  reinstall the mod or install Python 3." -ForegroundColor Yellow
    exit 1
}
# --- PY RESOLVER END ---

# Seleccion de idioma (por param o interactiva al inicio)
function Select-Language {
    if ($Lang -ne "") { Set-BZLang $Lang; return }
    Clear-Host
    # Flush del buffer de entrada UNA vez al entrar: limpia type-ahead viejo (ej. teclas que
    # quedaron de un crash/run anterior) para que la primera ReadKey lea TU tecla, no basura
    # vieja. Metodo nativo de PS (no el loop KeyAvailable, que podia comerse teclas vivas).
    try { $Host.UI.RawUI.FlushInputBuffer() } catch {}
    Write-Host ""
    Write-Host ("  " + (T "lang.prompt")) -ForegroundColor Cyan
    Write-Host ("   " + (T "lang.opt_es")) -ForegroundColor Magenta
    Write-Host ("   " + (T "lang.opt_en")) -ForegroundColor Magenta
    while ($true) {
        Write-Host "`n   > " -NoNewline -ForegroundColor Magenta
        $k = [Console]::ReadKey($true)
        if ($k.KeyChar -eq '1') { Set-BZLang "es"; return }
        if ($k.KeyChar -eq '2') { Set-BZLang "en"; return }
    }
}

# Carpetas donde buscar CSVs grabados (PathLogger). La del cliente es portable
# ($env:LOCALAPPDATA, anda en cualquier PC); las del server se DERIVAN de los paths
# configurados (RoutesDir / ServerBMirror) via Update-LogDirs -> NO hardcodeadas.
# CLIENTE: carpeta donde el juego (NUMPAD 5) graba las tomas. Default = %LOCALAPPDATA% del
# que corre el wizard; editable desde [6] (por si el cliente esta en otra PC). Portable.
$ClientRecDir = "$env:LOCALAPPDATA\DayZ\BZ_AutoDrive_PathLogger"
$LOG_DIRS = @($ClientRecDir)

# IMPORTAR ([2]): carpeta donde buscar rutas de BrigadaZ Transport v1. Vacia por default y la
# INDICA EL USUARIO: la ruta de v1 vive en el profile, pero bajo un nombre que NO conocemos
# (CONFIG_PATH es una const del codigo; un repack la cambia). Se persiste en wizard_config.json.
$TransportDir = ""

# INFORMES: carpeta de salida (conservada en el config por compatibilidad; el conversor
# puro no genera informes, pero se mantiene el campo para no romper wizard_config.json).
$ReportsDir = ""

function Update-LogDirs {
    # Recordings del server = carpeta hermana de las rutas: ...\BZ_AutoDrive\ <-> ..._PathLogger\
    # GUARD: Split-Path de un valor que NO es un path (ej. RoutesDir mal tipeado sin separadores)
    # devuelve string vacio -> Join-Path con Path vacio TIRA. Chequear cada parent antes de usar.
    $dirs = @($script:ClientRecDir)
    # de que carpeta viene cada grabacion (la etiqueta [A]/[B]/[Cl] del selector) se deriva de ESTAS carpetas configuradas,
    # no de letras de unidad de ninguna PC en particular.
    $script:LOG_DIR_SRV = ""
    $script:LOG_DIR_B = ""
    if ($script:RoutesDir) {
        $rdParent = Split-Path $script:RoutesDir -Parent
        if ($rdParent) { $script:LOG_DIR_SRV = (Join-Path $rdParent "BZ_AutoDrive_PathLogger"); $dirs += $script:LOG_DIR_SRV }
    }
    if ($script:ServerBMirror) {
        $bRoutes = Split-Path $script:ServerBMirror -Parent          # ...\profiles\BZ_AutoDrive
        if ($bRoutes) {
            $bParent = Split-Path $bRoutes -Parent
            if ($bParent) { $script:LOG_DIR_B = (Join-Path $bParent "BZ_AutoDrive_PathLogger"); $dirs += $script:LOG_DIR_B }
        }
    }
    $script:LOG_DIRS = @($dirs | Where-Object { $_ } | Select-Object -Unique)
}

# Marca de estado de un path para mostrar al lado en la UI. required=true -> si falta es
# ERROR (carpeta de rutas); required=false -> opcional (mirror B / cliente), solo aviso.
# Test-Path de un drive de red caido (una unidad de red apagada) devuelve false rapido, no cuelga.
function Get-PathMark {
    param([string]$p, [bool]$required)
    if (-not $p) { return "" }
    if (Test-Path $p) { return "  [OK]" }
    if ($required) { return "  " + (T "path.missing") }
    return "  " + (T "path.offline")
}

# ============================================================================
#  UI helpers (colores y boxes)
# ============================================================================
$C_TITLE   = "Cyan"
$C_LABEL   = "White"
$C_INFO    = "Gray"
$C_WARN    = "Yellow"
$C_OK      = "Green"
$C_ERR     = "Red"
$C_HILITE  = "Cyan"        # acento (era Magenta) -> identidad azul/cyan del logo
$C_FRAME   = "DarkCyan"    # marcos y separadores (azul de acento)

# Habilita ANSI truecolor en la consola (para el degrade azul del banner, estilo
# logo). Windows Terminal ya lo trae; conhost necesita ENABLE_VIRTUAL_TERMINAL_
# PROCESSING. Si falla, $script:VTok queda false y el banner usa 16 colores.
$script:VTok = $false
function Enable-VTMode {
    try {
        if (-not ([System.Management.Automation.PSTypeName]'Native.VT').Type) {
            Add-Type -Namespace Native -Name VT -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr GetStdHandle(int nStdHandle);
[DllImport("kernel32.dll")] public static extern bool GetConsoleMode(IntPtr hConsoleHandle, out uint lpMode);
[DllImport("kernel32.dll")] public static extern bool SetConsoleMode(IntPtr hConsoleHandle, uint dwMode);
'@
        }
        $h = [Native.VT]::GetStdHandle(-11)
        $m = [uint32]0
        [void][Native.VT]::GetConsoleMode($h, [ref]$m)
        [void][Native.VT]::SetConsoleMode($h, ($m -bor 0x0004))
        $script:VTok = $true
    } catch { $script:VTok = $false }
}

# Banner ASCII "BZ_AutoDrive" con degrade azul (cyan arriba -> azul profundo abajo),
# imitando el glow del logo. Glifos en '#' -> bloque solido en runtime (sin lios de
# encoding). Truecolor si VT esta ok; sino fallback a 16 colores (cyan->azul).
function Show-BZBanner {
    $blk = [char]0x2588
    $glyphs = @(
        @('#### ','#   #','#### ','#   #','#### '),  # B
        @('#####','   # ','  #  ',' #   ','#####'),  # Z
        @('     ','     ','     ','     ','#####'),  # _
        @(' ### ','#   #','#####','#   #','#   #'),  # A
        @('#   #','#   #','#   #','#   #',' ### '),  # U
        @('#####','  #  ','  #  ','  #  ','  #  '),  # T
        @(' ### ','#   #','#   #','#   #',' ### '),  # O
        @('#### ','#   #','#   #','#   #','#### '),  # D
        @('#### ','#   #','#### ','#  # ','#   #'),  # R
        @('#####','  #  ','  #  ','  #  ','#####'),  # I
        @('#   #','#   #','#   #',' # # ','  #  '),  # V
        @('#####','#    ','#### ','#    ','#####')   # E
    )
    Write-Host ""
    if ($script:VTok) {
        $ESC = [char]27
        $grad = @(@(150,225,255),@(95,185,250),@(55,150,242),@(38,118,214),@(30,95,185))
        for ($row = 0; $row -lt 5; $row++) {
            $line = ""
            foreach ($g in $glyphs) { $line += $g[$row].Replace('#', $blk) + " " }
            $c = $grad[$row]
            $color = "$ESC[38;2;$($c[0]);$($c[1]);$($c[2])m"
            Write-Host ("   " + $color + $line + "$ESC[0m")
        }
        $sep = ([string]([char]0x2500) * 73)
        Write-Host ("   " + "$ESC[38;2;70;165;245m" + $sep + "$ESC[0m")
        Write-Host ("   " + "$ESC[38;2;120;140;165m" + "       V E H I C L E   A I   " + [char]0x00B7 + "   C A L I B R A T I O N   W I Z A R D" + "$ESC[0m")
    } else {
        $cols = @("Cyan","Cyan","Blue","Blue","DarkBlue")
        for ($row = 0; $row -lt 5; $row++) {
            $line = ""
            foreach ($g in $glyphs) { $line += $g[$row].Replace('#', $blk) + " " }
            Write-Host ("   " + $line) -ForegroundColor $cols[$row]
        }
        Write-Host ("   " + ([string]([char]0x2500) * 73)) -ForegroundColor DarkCyan
        Write-Host ("          V E H I C L E   A I   -   C A L I B R A T I O N   W I Z A R D") -ForegroundColor DarkGray
    }
    Write-Host ""
}

# ---- Theming truecolor: paleta azul del logo, con fallback a 16 colores ----
# Mapea los nombres de color que ya usa el wizard a RGB del logo. Wc es drop-in de
# "Write-Host $Text -ForegroundColor $Color": si VT esta ok pinta truecolor, sino
# cae al color de consola original. Los semanticos (Yellow/Green/Red) se conservan.
$script:BZMap = @{
    'Cyan'     = '150;225;255'   # cyan claro  (titulos / acento brillante)
    'White'    = '224;232;242'   # casi blanco azulado (labels)
    'Gray'     = '120;140;165'   # gris azulado (info secundaria)
    'Magenta'  = '90;180;250'    # -> azul cielo (por si quedo alguno)
    'Blue'     = '70;150;245'
    'DarkCyan' = '70;165;245'    # azul de acento (marcos, separadores)
    'DarkBlue' = '38;118;214'
    'DarkGray' = '95;110;130'
    'Yellow'   = '240;200;70'    # semantico
    'Green'    = '95;210;130'    # semantico
    'Red'      = '235;95;95'     # semantico
}
function Wc {
    param([string]$Text, [string]$Color = "White", [switch]$NoNewline)
    if ($script:VTok -and $script:BZMap.ContainsKey($Color)) {
        $s = "$([char]27)[38;2;$($script:BZMap[$Color])m" + $Text + "$([char]27)[0m"
        if ($NoNewline) { Write-Host $s -NoNewline } else { Write-Host $s }
    } else {
        if ($NoNewline) { Write-Host $Text -ForegroundColor $Color -NoNewline }
        else            { Write-Host $Text -ForegroundColor $Color }
    }
}

function Write-Box {
    param([string]$Text, [string]$Color = "Cyan", [int]$Width = 64)
    $tl=[char]0x250C; $tr=[char]0x2510; $bl=[char]0x2514; $br=[char]0x2518
    $hz=[char]0x2500; $vt=[char]0x2502
    $top = [string]$tl + ([string]$hz * ($Width - 2)) + [string]$tr
    $bot = [string]$bl + ([string]$hz * ($Width - 2)) + [string]$br
    $padded = $Text.PadRight($Width - 4)
    Write-Host ""
    Wc $top $C_FRAME
    Wc ([string]$vt + " ") $C_FRAME -NoNewline
    Wc $padded $C_TITLE -NoNewline
    Wc (" " + [string]$vt) $C_FRAME
    Wc $bot $C_FRAME
}

function Write-Section {
    param([string]$Text, [string]$Color = "Cyan")
    $hz=[char]0x2500
    Write-Host ""
    Wc (">> " + $Text) $C_HILITE
    Wc ("   " + ([string]$hz * ($Text.Length + 3))) $C_FRAME
}

function Write-KV {
    param([string]$Label, [string]$Value, [string]$ValueColor = "White", [int]$LabelPad = 18)
    Wc ("   " + $Label.PadRight($LabelPad) + ": ") $C_INFO -NoNewline
    Wc $Value $ValueColor
}

function Read-Choice {
    param([string]$Prompt, [string[]]$ValidKeys)
    while ($true) {
        Write-Host ""
        Wc ("   " + $Prompt + " ") $C_HILITE -NoNewline
        $key = [Console]::ReadKey($true)
        $k = $key.KeyChar.ToString().ToUpper()
        if ($ValidKeys -contains $k) {
            Wc $k $C_HILITE
            return $k
        }
    }
}

function Pause-Wizard {
    Write-Host ""
    Wc ("   " + (T "common.enter_continue")) $C_INFO
    [Console]::ReadLine() | Out-Null
}

# ============================================================================
#  1) Seleccion de la grabacion (frame_*.csv)
# ============================================================================
function Select-Csv {
    Clear-Host
    Write-Box ("  " + (T "csv.title")) $C_TITLE
    Write-Section (T "csv.header")

    $allCsvs = @()
    foreach ($d in $LOG_DIRS) {
        if (Test-Path $d) {
            $csvs = Get-ChildItem $d -Filter "frame_*.csv" -ErrorAction SilentlyContinue
            foreach ($c in $csvs) {
                $allCsvs += [PSCustomObject]@{ File = $c; Source = $d }
            }
        }
    }

    if ($allCsvs.Count -eq 0) {
        Write-Host ("   " + (T "csv.none")) -ForegroundColor $C_ERR
        foreach ($d in $LOG_DIRS) { Write-Host ("     - " + $d) -ForegroundColor $C_INFO }
        Pause-Wizard
        return $null
    }

    # ASCENDENTE: la mas reciente queda ULTIMA (abajo, pegada al prompt de la terminal) -> no hay
    # que scrollear para arriba para verla. Y el indice [N] crece con la recencia = "numero de toma"
    # intuitivo y estable (las tomas se agregan al final, no se insertan en el medio).
    $allCsvs = $allCsvs | Sort-Object { $_.File.LastWriteTime }

    $idx = 1
    $opts = @()
    foreach ($entry in $allCsvs) {
        $f = $entry.File
        $kb = [math]::Round($f.Length / 1KB, 0)
        $src = if ($script:LOG_DIR_B -and ($entry.Source -eq $script:LOG_DIR_B)) { "B" } elseif ($script:LOG_DIR_SRV -and ($entry.Source -eq $script:LOG_DIR_SRV)) { "A" } else { "Cl" }
        # Vehiculo desde el sidecar header_<stamp>.txt (frame_<stamp>.csv -> header_<stamp>.txt).
        # Identifica la toma por FECHA HORA + VEHICULO sin abrir el CSV grande. Si no hay header, blanco.
        $veh = ""
        $hdrName = ($f.Name -replace '^frame_', 'header_') -replace '\.csv$', '.txt'
        $hdrPath = Join-Path (Split-Path $f.FullName -Parent) $hdrName
        if (Test-Path $hdrPath) {
            $vl = Get-Content $hdrPath -TotalCount 3 -ErrorAction SilentlyContinue | Where-Object { $_ -match '^vehicleClass=' } | Select-Object -First 1
            if ($vl -match '^vehicleClass=(.+)$') { $veh = $matches[1].Trim() }
        }
        Write-Host ("   [" + $idx.ToString().PadLeft(2) + "] " + $f.LastWriteTime.ToString("dd/MM HH:mm") + "  " + $veh.PadRight(16) + " " + $f.Name.PadRight(30) + " " + $kb.ToString().PadLeft(5) + " KB  [" + $src + "]") -ForegroundColor $C_HILITE
        $opts += $f.FullName
        $idx++
    }
    Write-Host ""
    Write-Host ("   [ 0] " + (T "common.back")) -ForegroundColor $C_INFO

    while ($true) {
        Write-Host ""
        Write-Host ("   " + (T "common.choice_num") + " ") -NoNewline -ForegroundColor $C_HILITE
        $sel = [Console]::ReadLine()
        if ($sel -eq "0") { return $null }
        $n = 0
        if ([int]::TryParse($sel, [ref]$n) -and $n -ge 1 -and $n -le $opts.Count) {
            return $opts[$n - 1]
        }
        Write-Host ("   " + (T "common.invalid")) -ForegroundColor $C_ERR
    }
}

# ============================================================================
#  2) Convertir la grabacion elegida -> ruta deployada (frame_to_route.py)
# ============================================================================
function Invoke-Convert {
    param([string]$framePath)

    $py = Join-Path $PSScriptRoot "frame_to_route.py"
    if (-not (Test-Path $py)) {
        Write-Host ("   frame_to_route.py " + $(if ($global:BZ_LANG -eq 'en') { "not found in " } else { "no encontrado en " }) + $PSScriptRoot) -ForegroundColor $C_ERR
        Pause-Wizard
        return
    }

    # frame_to_route.py necesita el header_<stamp>.txt apareado (mismo dir). Sin el, aborta.
    $frameDir  = Split-Path $framePath -Parent
    $frameName = Split-Path $framePath -Leaf
    $hdrName   = ($frameName -replace '^frame_', 'header_') -replace '\.csv$', '.txt'
    $hdrPath   = Join-Path $frameDir $hdrName
    if (-not (Test-Path $hdrPath)) {
        Write-Host ("   " + $(if ($global:BZ_LANG -eq 'en') { "Paired header not found: " } else { "No encuentro el header apareado: " }) + $hdrPath) -ForegroundColor $C_ERR
        Pause-Wizard
        return
    }

    # Nombre de ruta -> BZBusRoute_<nombre>.json (asi aparece en el Reproductor).
    # Sanitizado a [A-Za-z0-9_]; se re-pregunta si queda vacio (el conversor lo necesita).
    $routeName = ""
    while ($true) {
        $raw = Read-Host ("   " + $(if ($global:BZ_LANG -eq 'en') { "Route name (letters/numbers/_ only)" } else { "Nombre de la ruta (solo letras/numeros/_)" }))
        $routeName = ($raw -replace '[^A-Za-z0-9_]', '_').Trim('_')
        if ($routeName -ne "") { break }
        Write-Host ("   " + (T "common.invalid")) -ForegroundColor $C_ERR
    }

    $outBase = "BZBusRoute_" + $routeName
    Write-Section (T "convert.title") $C_TITLE
    Write-KV (T "convert.kv_input")  $framePath
    Write-KV (T "convert.kv_output") (Join-Path $script:RoutesDir ($outBase + ".json"))

    Write-Host ""
    $k = Read-Choice (T "convert.confirm") @("S","Y","N")
    if ($k -ne "S" -and $k -ne "Y") { return }

    # ENVELOPE DESDE LA TOMA, ANTES del conversor: frame_to_route.py copia envelope_<clase>.csv al hdr de la ruta
    # y deriva el creep de esa ficha. Corriendo despues, la primera ruta de cada vehiculo salia sin ficha y las
    # siguientes con una ficha sin la toma que se estaba convirtiendo. envelope_emit.py lee clase/wb del header
    # apareado y funde el mapa sincero de la toma en la ficha de la clase. NO se espeja al server B: cada server
    # aprende el suyo (no-sync A<->B).
    $pyEnv = Join-Path $PSScriptRoot "envelope_emit.py"
    if (Test-Path $pyEnv) {
        $prevEAP2 = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $envOut = & $script:PY $pyEnv $framePath '--out' $script:RoutesDir 2>&1 | Out-String
        $envCode = $LASTEXITCODE
        $ErrorActionPreference = $prevEAP2
        if ($envOut -and $envOut.Trim() -ne "") { Write-Host ("   " + $envOut.TrimEnd()) -ForegroundColor $C_INFO }
        # ★ 981 (el usuario, 22/09): el envelope se LLENA con cada toma de la clase (fusion en silencio, y la
        # consola dice que crecio) y SOLO pregunta cuando el Fingerprint estable del vehiculo cambio (Mass / marchas /
        # angulo de direccion): envelope_emit.py sale con codigo 3 y deja las dos firmas impresas arriba. La pregunta
        # es sobre ESTA toma, no generica.
        if ($envCode -eq 3) {
            Write-Host ""
            Write-Host ("   " + $(if ($global:BZ_LANG -eq 'en') { "The vehicle changed since its envelope was measured (mod update?). Regenerate the envelope from THIS take?" } else { "El vehiculo cambio desde que se midio su envelope (actualizacion del mod?). Regenero el envelope desde ESTA toma?" })) -ForegroundColor $C_WARN
            $kEnv = Read-Choice $(if ($global:BZ_LANG -eq 'en') { "[R] regenerate   [S] keep as is" } else { "[R] regenerar   [S] dejar como esta" }) @("R","S")
            if ($kEnv -eq "R") {
                $ErrorActionPreference = "Continue"
                $envOut2 = & $script:PY $pyEnv $framePath '--out' $script:RoutesDir '--pisar' 2>&1 | Out-String
                $ErrorActionPreference = $prevEAP2
                if ($envOut2 -and $envOut2.Trim() -ne "") { Write-Host ("   " + $envOut2.TrimEnd()) -ForegroundColor $C_INFO }
            }
        }
    }

    # Llamada al conversor. Se baja ErrorActionPreference alrededor del native call: en
    # PS 5.1 la fusion 2>&1 envuelve el stderr en ErrorRecord y con Stop podria tirar.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    # keep-inputs: conserva el pedal grabado (cols 8-9, hasInputData=1) para el
    # feedforward de 1.2. Efecto declarado: 1.1 TAMBIEN reproduce pedal en rutas con hasInputData
    # (conducta publicada de 1.1, no un colateral silencioso) -- las rutas viejas no cambian.
    # cinta: cada conversion emite <base>_cinta.csv FULL-RES (40Hz,
    # indexada por arco) junto a la ruta -- el overlay de ejecucion de LA CINTA (E2).
    $pyOut = & $script:PY $py $framePath $routeName '--keep-inputs' '--cinta' '--profile' $script:RoutesDir 2>&1 | Out-String
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prevEAP

    if ($pyOut -and $pyOut.Trim() -ne "") {
        Write-Host ""
        Write-Host ($pyOut.TrimEnd()) -ForegroundColor $C_INFO
    }

    if ($code -ne 0) {
        Write-Host ""
        Write-Host ("   " + ((T "convert.error") -f ("frame_to_route.py exit " + $code))) -ForegroundColor $C_ERR
        Pause-Wizard
        return
    }

    Write-Host ""
    Write-Host ("   " + (T "convert.ok")) -ForegroundColor $C_OK
    Write-Host ("   " + ((T "convert.deployed") -f $routeName)) -ForegroundColor $C_OK

    # Mirror al server B: copia los CUATRO archivos (.json + _hdr.json + _wp.csv + _cinta.csv). Sin la cinta, en B
    # la ruta corria sin cinta.
    if ($script:ServerBMirror -and (Test-Path $script:ServerBMirror)) {
        $copied = 0
        foreach ($suf in @(".json", "_hdr.json", "_wp.csv", "_cinta.csv")) {
            $src = Join-Path $script:RoutesDir ($outBase + $suf)
            if (Test-Path $src) { Copy-Item $src (Join-Path $script:ServerBMirror ($outBase + $suf)) -Force; $copied++ }
        }
        Write-Host ("   " + $(if ($global:BZ_LANG -eq 'en') { "Copied to server B ($copied files): " } else { "Copiado al server B ($copied archivos): " }) + $script:ServerBMirror) -ForegroundColor $C_INFO
    }

    Pause-Wizard
}

# ============================================================================
#  3) Importar una ruta de BrigadaZ Transport v1 (sin regrabar)
#
#  La ruta de v1 NO vive en el PBO: LoadConfig la lee del profile del server. Pero el nombre de
#  esa carpeta NO se puede asumir: CONFIG_PATH es una const del codigo de v1, y un repack /
#  fork la cambia. Asi que la carpeta LA INDICA EL USUARIO ([B] Buscar en otra carpeta) y se
#  RECUERDA en wizard_config.json. Se escanea RECURSIVO: apuntando a profiles\ la encuentra
#  se llame como se llame la carpeta.
#
#  Se busca en:
#    1. TransportDir  -> la que indico el usuario (persistida)
#    2. RoutesDir\_importar\  -> buzon nuestro, para el JSON traido de OTRO server
#  Y siempre queda [P] para pegar el path a un .json puntual.
# ============================================================================
function Get-TransportDirs {
    $dirs = @()
    if ($script:TransportDir) { $dirs += $script:TransportDir }
    if ($script:RoutesDir)    { $dirs += (Join-Path $script:RoutesDir "_importar") }
    return @($dirs | Where-Object { $_ } | Select-Object -Unique)
}

# Un .json es "ruta de Transport v1" si el arranque del archivo declara Waypoints. Se mira
# solo el principio (4 KB): un JSON de ruta pesa megas y no vale la pena parsearlo para listar.
function Test-IsTransportRoute {
    param([string]$path)
    try {
        $fs = [IO.File]::OpenRead($path)
        try {
            $buf = New-Object byte[] 4096
            $n = $fs.Read($buf, 0, $buf.Length)
            $head = [Text.Encoding]::UTF8.GetString($buf, 0, $n)
        } finally { $fs.Dispose() }
    } catch { return $false }
    if ($head -notmatch '"Waypoints"') { return $false }
    # Si el arranque ya declara identidad o pipeline de AutoDrive, es una ruta NUESTRA (alguien
    # la dejo en _importar por error): no tiene sentido ofrecerla para importar.
    if ($head -match '"(Fingerprint|UsePurePursuit|UseInverseModel)"') { return $false }
    return $true
}

function Select-TransportRoute {
    Clear-Host
    Write-Box ("  " + (T "imp.title")) $C_TITLE
    Write-Section (T "imp.header")

    $dirs = Get-TransportDirs
    # El buzon _importar se crea al entrar aca: asi el usuario tiene DONDE dejar el JSON que
    # se trajo de otro server, sin inventarse una carpeta.
    foreach ($d in $dirs) {
        if ($d -like "*_importar" -and -not (Test-Path $d)) {
            try { New-Item -ItemType Directory -Force $d | Out-Null } catch {}
        }
    }

    # RECURSIVO: el usuario apunta a profiles\ (o a donde tenga el archivo) y lo encontramos
    # aunque la carpeta del mod se llame distinto. Tope de archivos mirados: un profiles\ puede
    # tener muchos .json y no vale la pena abrirlos todos.
    $MAX_SCAN = 400
    $found = @()
    $mirados = 0
    foreach ($d in $dirs) {
        if (-not (Test-Path $d)) { continue }
        $js = Get-ChildItem $d -Filter "*.json" -File -Recurse -ErrorAction SilentlyContinue |
              Where-Object { $_.Name -notlike "*_hdr.json" }
        foreach ($j in $js) {
            if ($mirados -ge $MAX_SCAN) { break }
            $mirados++
            if (Test-IsTransportRoute $j.FullName) {
                $found += [PSCustomObject]@{ File = $j; Source = $j.DirectoryName }
            }
        }
    }

    $opts = @()
    if ($found.Count -eq 0) {
        Write-Host ("   " + (T "imp.none")) -ForegroundColor $C_WARN
        if ($dirs.Count -eq 0) {
            Write-Host ("     " + (T "imp.no_dir")) -ForegroundColor $C_INFO
        }
        foreach ($d in $dirs) { Write-Host ("     - " + $d + (Get-PathMark $d $false)) -ForegroundColor $C_INFO }
    } else {
        $found = $found | Sort-Object { $_.File.LastWriteTime }
        $idx = 1
        foreach ($e in $found) {
            $f = $e.File
            $kb = [math]::Round($f.Length / 1KB, 0)
            Write-Host ("   [" + $idx.ToString().PadLeft(2) + "] " + $f.LastWriteTime.ToString("dd/MM HH:mm") + "  " +
                        $f.Name.PadRight(28) + " " + $kb.ToString().PadLeft(6) + " KB   " + $e.Source) -ForegroundColor $C_HILITE
            $opts += $f.FullName
            $idx++
        }
    }

    Write-Host ""
    Write-Host ("   " + (T "imp.where")) -ForegroundColor $C_INFO
    Write-Host ""
    Write-Host ("   " + (T "imp.opt_browse")) -ForegroundColor $C_HILITE
    Write-Host ("   " + (T "imp.opt_paste"))  -ForegroundColor $C_HILITE
    Write-Host ("   [ 0] " + (T "common.back")) -ForegroundColor $C_INFO

    while ($true) {
        Write-Host ""
        Write-Host ("   " + (T "common.choice_num") + " ") -NoNewline -ForegroundColor $C_HILITE
        $sel = [Console]::ReadLine()
        if ($null -eq $sel) { return $null }   # EOF (stdin cerrado/redirigido): salir, no reventar
        $sel = $sel.Trim()
        if ($sel -eq "0") { return $null }
        if ($sel -eq "B" -or $sel -eq "b") {
            $typed = (Read-Host ("   " + (T "imp.ask_browse"))).Trim().Trim('"')
            if ($typed -eq "") { continue }
            if (-not (Test-Path $typed)) {
                Write-Host ("   " + ((T "imp.not_found") -f $typed)) -ForegroundColor $C_ERR
                continue
            }
            # Si te pasan el .json directo en vez de la carpeta, lo tomamos igual.
            if (Test-Path $typed -PathType Leaf) { return $typed }
            $script:TransportDir = $typed
            Save-WizardConfig (Join-Path $PSScriptRoot "wizard_config.json")
            return (Select-TransportRoute)     # re-escanea con la carpeta nueva, ya recordada
        }
        if ($sel -eq "P" -or $sel -eq "p") {
            $typed = (Read-Host ("   " + (T "imp.ask_paste"))).Trim().Trim('"')
            if ($typed -eq "") { continue }
            if (Test-Path $typed) { return $typed }
            Write-Host ("   " + ((T "imp.not_found") -f $typed)) -ForegroundColor $C_ERR
            continue
        }
        $n = 0
        if ([int]::TryParse($sel, [ref]$n) -and $n -ge 1 -and $n -le $opts.Count) { return $opts[$n - 1] }
        Write-Host ("   " + (T "common.invalid")) -ForegroundColor $C_ERR
    }
}

# Identidad del vehiculo: lo unico que una toma de v1 no puede saber. Se ofrece lo que ya hay
# en la PC: los header_*.txt de cualquier grabacion (uno por vehiculo, el mas reciente) y los
# _hdr.json de tomas ya calibradas (esos ademas traen el EndpointBrakeDecel MEDIDO).
# VehicleClass declarado por la ruta de v1. Se mira solo el arranque del archivo (el header va
# antes de los Waypoints): parsear 5 MB de JSON para leer un string no vale la pena.
function Get-V1VehicleClass {
    param([string]$path)
    try {
        $fs = [IO.File]::OpenRead($path)
        try {
            $buf = New-Object byte[] 4096
            $n = $fs.Read($buf, 0, $buf.Length)
            $head = [Text.Encoding]::UTF8.GetString($buf, 0, $n)
        } finally { $fs.Dispose() }
    } catch { return "" }
    if ($head -match '"VehicleClass"\s*:\s*"([^"]+)"') { return $matches[1] }
    return ""
}

function Select-Fingerprint {
    param([string]$MatchVehicle = "")
    Clear-Host
    Write-Box ("  " + (T "imp.fp_title")) $C_TITLE
    Write-Section (T "imp.fp_header")
    Write-Host ("   " + (T "imp.fp_intro")) -ForegroundColor $C_INFO
    Write-Host ""

    $cands = @()
    $vistos = @{}
    foreach ($d in $LOG_DIRS) {
        if (-not (Test-Path $d)) { continue }
        $hs = Get-ChildItem $d -Filter "header_*.txt" -File -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending
        foreach ($h in $hs) {
            $vl = Get-Content $h.FullName -TotalCount 3 -ErrorAction SilentlyContinue |
                  Where-Object { $_ -match '^vehicleClass=' } | Select-Object -First 1
            if ($vl -notmatch '^vehicleClass=(.+)$') { continue }
            $veh = $matches[1].Trim()
            if ($vistos.ContainsKey($veh)) { continue }   # uno por vehiculo: el mas reciente
            $vistos[$veh] = $true
            $cands += [PSCustomObject]@{ Path = $h.FullName; Veh = $veh; Kind = (T "imp.fp_rec"); Note = $h.LastWriteTime.ToString("dd/MM HH:mm") }
        }
    }
    if (Test-Path $script:RoutesDir) {
        $hdrs = Get-ChildItem $script:RoutesDir -Filter "BZBusRoute_*_hdr.json" -File -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending
        foreach ($hf in $hdrs) {
            try { $j = Get-Content $hf.FullName -Raw | ConvertFrom-Json } catch { continue }
            if (-not $j.Fingerprint) { continue }
            $nota = if ($j.EndpointBrakeDecel) { (T "imp.fp_route_note") } else { "" }
            $cands += [PSCustomObject]@{ Path = $hf.FullName; Veh = [string]$j.VehicleClass; Kind = (T "imp.fp_route"); Note = ($hf.BaseName -replace '_hdr$','') + " " + $nota }
        }
    }

    if ($cands.Count -eq 0) {
        Write-Host ("   " + (T "imp.fp_empty")) -ForegroundColor $C_WARN
    }

    # Orden: primero lo que COINCIDE con el vehiculo que declara la ruta v1, y dentro de eso las
    # tomas calibradas antes que las grabaciones sueltas (la calibrada trae el freno medido).
    # Sin esto la lista sale por fecha y en una PC con muchas grabaciones (58 aca) se va de pantalla.
    $esRuta = (T "imp.fp_route")
    $cands = @($cands | Sort-Object `
        @{ Expression = { if ($MatchVehicle -and $_.Veh -eq $MatchVehicle) { 0 } else { 1 } } }, `
        @{ Expression = { if ($_.Kind -eq $esRuta) { 0 } else { 1 } } })

    $TOPE = 14
    $idx = 1
    $opts = @()
    foreach ($c in $cands) {
        if ($idx -gt $TOPE) { $opts += $c.Path; $idx++; continue }   # seleccionable por numero, sin imprimir
        $marca = ""
        $color = $C_HILITE
        if ($MatchVehicle -and $c.Veh -eq $MatchVehicle) { $marca = "  <- " + (T "imp.fp_match"); $color = $C_OK }
        Write-Host ("   [" + $idx.ToString().PadLeft(2) + "] " + $c.Veh.PadRight(22) + " " + $c.Kind.PadRight(16) + " " + $c.Note + $marca) -ForegroundColor $color
        $opts += $c.Path
        $idx++
    }
    if ($cands.Count -gt $TOPE) {
        Write-Host ("   " + ((T "imp.fp_more") -f ($cands.Count - $TOPE))) -ForegroundColor $C_INFO
    }
    Write-Host ""
    Write-Host ("   " + (T "imp.fp_paste")) -ForegroundColor $C_HILITE
    Write-Host ("   " + (T "imp.fp_none")) -ForegroundColor $C_INFO

    while ($true) {
        Write-Host ""
        Write-Host ("   " + (T "common.choice_num") + " ") -NoNewline -ForegroundColor $C_HILITE
        $sel = [Console]::ReadLine()
        if ($null -eq $sel) { return "" }      # EOF: seguir sin identidad, no reventar
        $sel = $sel.Trim()
        if ($sel -eq "0") { return "" }
        if ($sel -eq "P" -or $sel -eq "p") {
            $typed = (Read-Host ("   " + (T "imp.ask_paste_fp"))).Trim().Trim('"')
            if ($typed -eq "") { continue }
            if (Test-Path $typed) { return $typed }
            Write-Host ("   " + ((T "imp.not_found") -f $typed)) -ForegroundColor $C_ERR
            continue
        }
        $n = 0
        if ([int]::TryParse($sel, [ref]$n) -and $n -ge 1 -and $n -le $opts.Count) { return $opts[$n - 1] }
        Write-Host ("   " + (T "common.invalid")) -ForegroundColor $C_ERR
    }
}

# ============================================================================
# MERGE DE TRAMOS: la feature "continuar grabacion"
#  deja la ruta larga partida en varios frame_*.csv. frame_merge.py los une:
#  orden por EMPALME DE POSICION (no por timestamp), duplicados detectados y
#  descartados con aviso, markers de cada tramo preservados, t_cum re-acumulado.
#  Al final ofrece encadenar directo a Invoke-Convert (ruta + envelope + mirror).
# ============================================================================
function Select-CsvMulti {
    Clear-Host
    Write-Box ("  " + $(if ($global:BZ_LANG -eq 'en') { "MERGE: pick the SEGMENTS (2+)" } else { "MERGE: elegir los TRAMOS (2 o mas)" })) $C_TITLE
    $allCsvs = @()
    foreach ($d in $LOG_DIRS) {
        if (Test-Path $d) {
            $csvs = Get-ChildItem $d -Filter "frame_*.csv" -ErrorAction SilentlyContinue
            foreach ($c in $csvs) { $allCsvs += [PSCustomObject]@{ File = $c; Source = $d } }
        }
    }
    if ($allCsvs.Count -lt 2) {
        Write-Host ("   " + (T "csv.none")) -ForegroundColor $C_ERR
        Pause-Wizard
        return $null
    }
    $allCsvs = $allCsvs | Sort-Object { $_.File.LastWriteTime }
    $idx = 1
    $opts = @()
    foreach ($entry in $allCsvs) {
        $f = $entry.File
        $veh = ""
        $hdrName = ($f.Name -replace '^frame_', 'header_') -replace '\.csv$', '.txt'
        $hdrPath = Join-Path (Split-Path $f.FullName -Parent) $hdrName
        if (Test-Path $hdrPath) {
            $vl = Get-Content $hdrPath -TotalCount 3 -ErrorAction SilentlyContinue | Where-Object { $_ -match '^vehicleClass=' } | Select-Object -First 1
            if ($vl -match '^vehicleClass=(.+)$') { $veh = $matches[1].Trim() }
        }
        Write-Host ("   [" + $idx.ToString().PadLeft(2) + "] " + $f.LastWriteTime.ToString("dd/MM HH:mm") + "  " + $veh.PadRight(16) + " " + $f.Name) -ForegroundColor $C_HILITE
        $opts += $f.FullName
        $idx++
    }
    Write-Host ""
    Write-Host ("   " + $(if ($global:BZ_LANG -eq 'en') { "Numbers comma-separated (order does NOT matter: position decides the chain). 0 = back" } else { "Numeros separados por coma (el orden NO importa: la posicion decide la cadena). 0 = volver" })) -ForegroundColor $C_INFO
    while ($true) {
        Write-Host ""
        Write-Host "   > " -NoNewline -ForegroundColor $C_HILITE
        $sel = [Console]::ReadLine()
        if ($sel.Trim() -eq "0") { return $null }
        $nums = @($sel -split '[, ]+' | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ })
        $nums = @($nums | Where-Object { $_ -ge 1 -and $_ -le $opts.Count } | Select-Object -Unique)
        if ($nums.Count -ge 2) { return @($nums | ForEach-Object { $opts[$_ - 1] }) }
        Write-Host ("   " + (T "common.invalid")) -ForegroundColor $C_ERR
    }
}

function Invoke-Merge {
    $py = Join-Path $PSScriptRoot "frame_merge.py"
    if (-not (Test-Path $py)) {
        Write-Host ("   frame_merge.py " + $(if ($global:BZ_LANG -eq 'en') { "not found in " } else { "no encontrado en " }) + $PSScriptRoot) -ForegroundColor $C_ERR
        Pause-Wizard
        return
    }
    $frames = Select-CsvMulti
    if (-not $frames -or $frames.Count -lt 2) { return }
    Write-Host ""
    $raw = Read-Host ("   " + $(if ($global:BZ_LANG -eq 'en') { "Name for the merged take (letters/numbers/_)" } else { "Nombre de la toma unida (solo letras/numeros/_)" }))
    $name = ($raw -replace '[^A-Za-z0-9_]', '_').Trim('_')
    if ($name -eq "") { return }
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $out = & $script:PY $py $name @($frames) 2>&1 | Out-String
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prevEAP
    if ($out -and $out.Trim() -ne "") { Write-Host ($out.TrimEnd()) -ForegroundColor $C_INFO }
    if ($code -ne 0) {
        Write-Host ("   " + $(if ($global:BZ_LANG -eq 'en') { "MERGE failed (see above): nothing written." } else { "El MERGE fallo (ver arriba): no se escribio nada." })) -ForegroundColor $C_ERR
        Pause-Wizard
        return
    }
    $merged = Join-Path (Split-Path $frames[0] -Parent) ("frame_" + $name + ".csv")
    if (Test-Path $merged) {
        Write-Host ""
        $k = Read-Choice ($(if ($global:BZ_LANG -eq 'en') { "Convert the merged take to a route now?" } else { "Convertir la toma unida en ruta ahora?" })) @("S","Y","N")
        if ($k -eq "S" -or $k -eq "Y") { Invoke-Convert $merged; return }
    }
    Pause-Wizard
}

function Invoke-ImportTransport {
    param([string]$v1Path)

    $py = Join-Path $PSScriptRoot "transport_v1_to_route.py"
    if (-not (Test-Path $py)) {
        Write-Host ("   transport_v1_to_route.py " + $(if ($global:BZ_LANG -eq 'en') { "not found in " } else { "no encontrado en " }) + $PSScriptRoot) -ForegroundColor $C_ERR
        Pause-Wizard
        return
    }

    # La ruta v1 declara su VehicleClass -> se usa para poner PRIMERO el fingerprint que coincide.
    $fp = Select-Fingerprint (Get-V1VehicleClass $v1Path)

    $routeName = ""
    while ($true) {
        $raw = Read-Host ("   " + $(if ($global:BZ_LANG -eq 'en') { "Route name (letters/numbers/_ only)" } else { "Nombre de la ruta (solo letras/numeros/_)" }))
        $routeName = ($raw -replace '[^A-Za-z0-9_]', '_').Trim('_')
        if ($routeName -ne "") { break }
        Write-Host ("   " + (T "common.invalid")) -ForegroundColor $C_ERR
    }

    # Perfil de obstaculos (AR_OnWay): un bus de linea 24/7 suele querer 'robusto' (sortea autos parados).
    Write-Host ""
    Write-Host ("   " + $(if ($global:BZ_LANG -eq 'en') { "Obstacle profile (cars stopped on the road):" } else { "Perfil de obstaculos (autos parados en el camino):" })) -ForegroundColor $C_INFO
    Write-Host ("     [R] " + $(if ($global:BZ_LANG -eq 'en') { "Robust        - brake + drive around (24/7 bus line)" } else { "Robusto       - frena + esquiva (bus de linea 24/7)" }))
    Write-Host ("     [I] " + $(if ($global:BZ_LANG -eq 'en') { "Interceptable - brake and wait (missions)" } else { "Interceptable - frena y se queda (misiones)" }))
    Write-Host ("     [N] " + $(if ($global:BZ_LANG -eq 'en') { "None          - off (pure replica)" } else { "Ninguno       - off (replica pura)" }))
    $ko = Read-Choice ($(if ($global:BZ_LANG -eq 'en') { "Choose [R/I/N]" } else { "Elegi [R/I/N]" })) @("R", "I", "N")
    $obsProfile = "ninguno"
    if ($ko -eq "R") { $obsProfile = "robusto" } elseif ($ko -eq "I") { $obsProfile = "interceptable" }

    $outBase = "BZBusRoute_" + $routeName
    Write-Section (T "imp.title") $C_TITLE
    Write-KV (T "imp.kv_input")   $v1Path
    Write-KV (T "imp.kv_vehicle") $(if ($fp) { $fp } else { "-" })
    Write-KV (T "imp.kv_output")  (Join-Path $script:RoutesDir ($outBase + ".json"))
    Write-KV ($(if ($global:BZ_LANG -eq 'en') { "Obstacles" } else { "Obstaculos" })) $obsProfile

    Write-Host ""
    $k = Read-Choice (T "imp.confirm") @("S","Y","N")
    if ($k -ne "S" -and $k -ne "Y") { return }

    $pyArgs = @($py, $v1Path, $routeName, '--profile', $script:RoutesDir)
    if ($fp) { $pyArgs += @('--fingerprint', $fp) }
    $pyArgs += @('--obstaculos', $obsProfile)

    # ErrorActionPreference bajado alrededor del native call: en PS 5.1 la fusion 2>&1 envuelve
    # el stderr en ErrorRecord y con Stop podria tirar.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $pyOut = & $script:PY @pyArgs 2>&1 | Out-String
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prevEAP

    if ($pyOut -and $pyOut.Trim() -ne "") {
        Write-Host ""
        Write-Host ($pyOut.TrimEnd()) -ForegroundColor $C_INFO
    }
    if ($code -ne 0) {
        Write-Host ""
        Write-Host ("   " + ((T "convert.error") -f ("transport_v1_to_route.py exit " + $code))) -ForegroundColor $C_ERR
        Pause-Wizard
        return
    }

    Write-Host ""
    Write-Host ("   " + (T "imp.ok")) -ForegroundColor $C_OK
    Write-Host ("   " + ((T "convert.deployed") -f $routeName)) -ForegroundColor $C_OK

    if ($script:ServerBMirror -and (Test-Path $script:ServerBMirror)) {
        $copied = 0
        foreach ($suf in @(".json", "_hdr.json", "_wp.csv", "_cinta.csv")) {
            $src = Join-Path $script:RoutesDir ($outBase + $suf)
            if (Test-Path $src) { Copy-Item $src (Join-Path $script:ServerBMirror ($outBase + $suf)) -Force; $copied++ }
        }
        Write-Host ("   " + $(if ($global:BZ_LANG -eq 'en') { "Copied to server B ($copied files): " } else { "Copiado al server B ($copied archivos): " }) + $script:ServerBMirror) -ForegroundColor $C_INFO
    }

    Pause-Wizard
}

# ============================================================================
#  Config de paths (portable: wizard_config.json al lado del script)
# ============================================================================
function Invoke-ConfigPaths {
    param([bool]$primera = $false)
    Clear-Host
    Write-Box ("  " + (T "cfgp.title")) $C_TITLE
    if ($primera) {
        Write-Host ""
        Write-Host ("   " + (T "cfgp.first_intro")) -ForegroundColor $C_WARN
    }
    $mir = if ($script:ServerBMirror) { $script:ServerBMirror } else { (T "cfgp.none") }
    Write-KV (T "cfgp.kv_routes") ($script:RoutesDir + (Get-PathMark $script:RoutesDir $true))
    Write-KV (T "cfgp.kv_client") ($script:ClientRecDir + (Get-PathMark $script:ClientRecDir $false))
    Write-KV (T "cfgp.kv_mirror") ($mir + $(if ($script:ServerBMirror) { (Get-PathMark $script:ServerBMirror $false) } else { "" }))
    Write-Host ""
    Write-Host ("   " + (T "cfgp.hint")) -ForegroundColor $C_INFO
    Write-Host ""
    # La carpeta de rutas es OBLIGATORIA: sin una configurada, Enter no "mantiene" nada y se vuelve a pedir; si la tipeada
    # no existe se ofrece crearla (nunca se crea una carpeta por un tecleo accidental).
    while ($true) {
        $pr = if ($script:RoutesDir) { (T "cfgp.ask_routes") } else { (T "cfgp.ask_routes_first") }
        $s = ([string](Read-Host ("   " + $pr))).Trim()
        if ($s -eq "") {
            if ($script:RoutesDir) { break }
            Write-Host ("   " + (T "cfgp.empty_required")) -ForegroundColor $C_WARN
            continue
        }
        if (Test-Path $s) { $script:RoutesDir = $s; break }
        Write-Host ("   " + ((T "cfgp.not_exists") -f $s)) -ForegroundColor $C_ERR
        $cr = ([string](Read-Host ("   " + (T "cfgp.create_q")))).Trim()
        if ($cr -match '^[sSyY]') {
            try { New-Item -ItemType Directory -Force $s | Out-Null; $script:RoutesDir = $s; break }
            catch { Write-Host ("   " + (T "cfgp.create_fail")) -ForegroundColor $C_ERR }
        }
    }
    $c = ([string](Read-Host ("   " + (T "cfgp.ask_client")))).Trim()
    if ($c -ne "") { $script:ClientRecDir = $c }
    $m = ([string](Read-Host ("   " + (T "cfgp.ask_mirror")))).Trim()
    if ($m -eq "-") { $script:ServerBMirror = "" }
    elseif ($m -ne "") { $script:ServerBMirror = $m }
    $cfgPath = Join-Path $PSScriptRoot "wizard_config.json"
    Save-WizardConfig $cfgPath
    Update-LogDirs
    Write-Host ""
    Write-Host ("   " + ((T "cfgp.saved") -f $cfgPath)) -ForegroundColor $C_OK
    Pause-Wizard
}

# Chequeo de salud de carpetas al arrancar (post Resolve-Paths). Escanea los paths
# configurados; si alguno no existe o no esta en linea (ej. una unidad de red apagada),
# lo lista con [X] y ofrece cambiarlo [1] o continuar igual [2]. NO atrapa al usuario como
# hacia el viejo paste-flow: si una carpeta de red esta caida, igual entras y la editas.
function Test-PathsHealth {
    while ($true) {
        $bad = @()
        if ((-not $script:RoutesDir) -or (-not (Test-Path $script:RoutesDir))) { $bad += ,@((T "cfgp.kv_routes"),  $script:RoutesDir,   $true) }
        if (-not (Test-Path $script:ClientRecDir)) { $bad += ,@((T "cfgp.kv_client"),  $script:ClientRecDir, $false) }
        if ($script:ServerBMirror -and -not (Test-Path (Split-Path $script:ServerBMirror -Parent))) { $bad += ,@((T "cfgp.kv_mirror"), $script:ServerBMirror, $false) }
        if ($bad.Count -eq 0) { return }
        Clear-Host
        Write-Box ("  " + (T "pathchk.title")) $C_TITLE
        Write-Host ("   " + (T "pathchk.intro")) -ForegroundColor $C_WARN
        Write-Host ""
        foreach ($b in $bad) { Write-KV $b[0] ($b[1] + (Get-PathMark $b[1] $b[2])) }
        Write-Host ""
        Write-Host ("   " + (T "pathchk.opt_edit")) -ForegroundColor $C_HILITE
        Write-Host ("   " + (T "pathchk.opt_cont")) -ForegroundColor $C_INFO
        Write-Host ""
        $k = (Read-Host ("   " + (T "pathchk.prompt"))).Trim()
        if ($k -eq "1") { Invoke-ConfigPaths; continue }
        return
    }
}

# ============================================================================
#  Menu principal (conversor puro)
# ============================================================================
function Show-MainMenu {
    Clear-Host
    Show-BZBanner
    Wc ("  " + (T "menu.subtitle")) $C_INFO
    Write-Section (T "menu.header")
    Wc ("   " + (T "menu.convert"))   $C_HILITE
    Wc ("   " + (T "menu.import_v1")) $C_HILITE
    Wc ("   " + (T "menu.merge"))     $C_HILITE
    Wc ("   " + (T "menu.config"))    $C_INFO
    Wc ("   " + (T "menu.quit"))      $C_INFO
    return Read-Choice (T "common.choice") @("1","2","3","6","Q")
}

# ============================================================================
#  Config portable: el wizard recuerda la carpeta del server (wizard_config.json
#  al lado del script). Asi un modder con el server en otro path (ej G:\...) lo
#  setea UNA vez y no toca -RoutesDir nunca mas. Ver [[project_framework_modder_ux]].
# ============================================================================
function Save-WizardConfig {
    param([string]$cfgPath)
    $cfg = [PSCustomObject]@{ RoutesDir = $script:RoutesDir; ClientRecDir = $script:ClientRecDir; ServerBMirror = $script:ServerBMirror; ReportsDir = $script:ReportsDir; TransportDir = $script:TransportDir }
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($cfgPath, ($cfg | ConvertTo-Json), $utf8)
}

function Resolve-Paths {
    param([bool]$explicit)
    $cfgPath = Join-Path $PSScriptRoot "wizard_config.json"

    # 1. -RoutesDir explicito en la linea de comandos -> gana y se persiste.
    if ($explicit) {
        Write-Host ("   " + ((T "cfgp.routes_param") -f $script:RoutesDir)) -ForegroundColor $C_INFO
        Save-WizardConfig $cfgPath
        return
    }
    # 2. Config guardada de un uso anterior.
    if (Test-Path $cfgPath) {
        try {
            $cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
            if ($cfg.RoutesDir)     { $script:RoutesDir = $cfg.RoutesDir }
            if ($cfg.ServerBMirror) { $script:ServerBMirror = $cfg.ServerBMirror }
            if ($cfg.ClientRecDir)  { $script:ClientRecDir  = $cfg.ClientRecDir }
            if ($cfg.ReportsDir)    { $script:ReportsDir    = $cfg.ReportsDir }
            if ($cfg.TransportDir)  { $script:TransportDir  = $cfg.TransportDir }
        } catch { }
        if ($script:RoutesDir -and (Test-Path $script:RoutesDir)) {
            Write-Host ("   " + ((T "cfgp.routes_cfg") -f $script:RoutesDir)) -ForegroundColor $C_INFO
        } else {
            # Configurado en un uso anterior pero AHORA no esta en linea (ej. una unidad de red apagada). NO re-detectar ni
            # atrapar: mantener el path y dejar que el chequeo de salud (Test-PathsHealth) avise y ofrezca cambiarlo o seguir.
            Write-Host ("   " + ((T "cfgp.routes_offline") -f $script:RoutesDir)) -ForegroundColor $C_WARN
        }
        return
    }
    # 3. Primera corrida (sin config y sin parametro): no se adivina ningun path de ninguna PC. Initialize-WizardPaths abre
    #    la pantalla de paths, que no acepta una carpeta de rutas vacia, y la guarda.
}

# Arranque de paths, en UN solo lugar (la simulacion de primera corrida lo llama igual que el main loop): parametro o
# config -> si es la PRIMERA corrida, la pantalla de paths (una sola, en el idioma elegido) -> carpetas de grabaciones ->
# chequeo de salud.
function Initialize-WizardPaths {
    param([bool]$explicit)
    $cfgPath = Join-Path $PSScriptRoot "wizard_config.json"
    $script:FirstRun = (-not (Test-Path $cfgPath)) -and (-not $explicit)
    Resolve-Paths $explicit
    if ($script:FirstRun) { Invoke-ConfigPaths $true }
    Update-LogDirs
    Test-PathsHealth
}

# ============================================================================
#  Main loop
# ============================================================================
# Guard: solo corre si el script se EJECUTA directamente, no si se dot-sourcea
# (permite cargar las funciones para tests/demos sin lanzar la TUI).
if ($MyInvocation.InvocationName -ne '.') {
    try {
        Enable-VTMode
        Select-Language
        Initialize-WizardPaths ($PSBoundParameters.ContainsKey('RoutesDir'))
        while ($true) {
          try {
            $choice = Show-MainMenu
            switch ($choice) {
                "1" {
                    $frame = Select-Csv
                    if ($frame) { Invoke-Convert $frame }
                }
                "2" {
                    $v1 = Select-TransportRoute
                    if ($v1) { Invoke-ImportTransport $v1 }
                }
                "3" {
                    Invoke-Merge
                }
                "6" {
                    Invoke-ConfigPaths
                }
                "Q" {
                    Write-Host ""
                    Write-Host ("  " + (T "common.bye")) -ForegroundColor $C_TITLE
                    exit 0
                }
            }
          } catch {
            # Error DE UNA OPERACION: mostrar y VOLVER AL MENU (no matar el wizard).
            Write-Host ""
            Write-Host "  Error en la operacion: $_" -ForegroundColor $C_ERR
            Write-Host $_.ScriptStackTrace -ForegroundColor $C_INFO
            Write-Host ""
            Write-Host "  (La operacion fallo, pero el wizard sigue abierto. Volvemos al menu principal.)" -ForegroundColor $C_WARN
            Pause-Wizard
          }
        }
    } catch {
        # Error FATAL de arranque (VTMode / idioma / paths): aca si salimos.
        Write-Host ""
        Write-Host "  Error: $_" -ForegroundColor $C_ERR
        Write-Host $_.ScriptStackTrace -ForegroundColor $C_INFO
        exit 1
    }
}
