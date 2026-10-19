# Instala o NS HUEHUE no runner Windows, abre a biblioteca e tira prints das telas principais.
# Os PNGs ficam em $Out. Roda num PC sem GPU (Microsoft Basic Render Driver): o desenho dos menus
# e o mesmo codigo Direct2D do Xbox, so a escala da janela muda.
param([string] $Pkg = "pkg", [string] $Out = "shots")
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$shell = New-Object -ComObject WScript.Shell

function Shot([string] $name) {
    Start-Sleep -Seconds 3
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
    $bmp.Save((Join-Path $Out "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Host "print: $name"
}
function Key([string] $keys) {
    $null = $shell.AppActivate("NS HUEHUE")
    Start-Sleep -Milliseconds 400
    [System.Windows.Forms.SendKeys]::SendWait($keys)
    Start-Sleep -Seconds 2
}

# --- Sideload (so neste runner descartavel) ---------------------------------------------------
$unlock = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock"
New-Item -Path $unlock -Force | Out-Null
Set-ItemProperty $unlock AllowDevelopmentWithoutDevLicense 1 -Type DWord
Set-ItemProperty $unlock AllowAllTrustedApps 1 -Type DWord
Import-Certificate -FilePath "$Pkg\huehue-switch.cer" -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
Import-Certificate -FilePath "$Pkg\huehue-switch.cer" -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
Add-AppxPackage -Path "$Pkg\huehue-switch.appx" -DependencyPath "$Pkg\Microsoft.VCLibs.x64.14.00.appx"
$app = Get-AppxPackage -Name HueHue.HueHueSwitch
if (-not $app) { throw "o pacote nao instalou" }
Write-Host "instalado: $($app.PackageFullName)"

# Interface em portugues (o app segue a lista de idiomas do usuario).
Set-WinUserLanguageList -LanguageList pt-BR, en-US -Force
$aumid = "$($app.PackageFamilyName)!App"

function Launch {
    Start-Process "explorer.exe" "shell:AppsFolder\$aumid"
    Start-Sleep -Seconds 20
}
function Close {
    Get-Process eden-uwp -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 3
}

# Telas do pacote de design (design/ns-huehue-v1): 01 inicio, 18 controles, 11 configuracoes
# (com Creditos), 09 biblioteca vazia; depois com jogos: 02 grade, 07 opcoes, favorito, 05 busca.
# 1. Primeira abertura: biblioteca vazia. Menu inicial: Meus jogos, Configuracoes, Controles.
Launch
Shot "01-inicio"
Key "{DOWN}"
Key "{DOWN}"
Key "{ENTER}"
Shot "18-controles"
Key "{ESC}"
Key "{UP}"
Key "{ENTER}"
Shot "11-configuracoes"
Key "{DOWN}"
Key "{DOWN}"
Key "{DOWN}"
Shot "44-saves-usb"
Key "{ENTER}"
Shot "44b-saves-usb-pagina"
Key "{ESC}"
Key "{DOWN}"
Shot "45-online"
Key "{UP}"
Key "{UP}"
Key "{UP}"
Key "{UP}"
Key "{ENTER}"
Shot "11b-sistema"
Key "{ESC}"
Key "{ESC}"
Key "{UP}"
Key "{ENTER}"
Shot "09-biblioteca-vazia"
Close

$games = Join-Path $env:LOCALAPPDATA "Packages\$($app.PackageFamilyName)\LocalState\games"
New-Item -ItemType Directory -Force $games | Out-Null
foreach ($n in @("Turbo Rally", "Reino Perdido", "Gol de Placa Arena", "Pulo Estelar", "Corrida Espacial",
                 "Ilha do Tesouro", "Fazendinha Feliz", "Robo Futebol", "Castelo de Cartas", "Tuba Tuba Party",
                 "Mestre do Kart", "Ninja das Ruas")) {
    New-Item -ItemType File -Force (Join-Path $games "$n.nsp") | Out-Null
}
Launch
Shot "01b-inicio-com-jogos"
Key "{ENTER}"
Shot "02-meus-jogos"
Key "{RIGHT}"
Shot "02b-selecao"
Key "x"
Shot "07-opcoes-do-jogo"
Key "{DOWN}"
Key "{DOWN}"
Key "{DOWN}"
Key "{DOWN}"
Key "{DOWN}"
Key "{DOWN}"
Key "{ENTER}"
Shot "08-desempenho-e-graficos"
Key "{DOWN}"
Key "{RIGHT}"
Shot "08b-renderizador-opengl"
Key "{LEFT}"
Key "{ESC}"
Key "{DOWN}"
Key "{ENTER}"
Shot "07b-favorito-adicionado"
Key "{ESC}"
Key "y"
Shot "05-buscar"
Key "tu"
Key "{ENTER}"
Shot "05b-resultado-da-busca"
Key "y"
Key "{BACKSPACE}{BACKSPACE}"
Key "{ENTER}"
Key "{UP}"
Key "{UP}"
Key "{RIGHT}"
Key "{ENTER}"
Shot "03-favoritos"
Key "{RIGHT}"
Key "{ENTER}"
Shot "04-recentes"
Key "e"
Shot "lb-rb-configuracoes"
Key "{ESC}"
Key "{ESC}"
Shot "01c-volta-ao-inicio"
Close
Get-ChildItem $Out
