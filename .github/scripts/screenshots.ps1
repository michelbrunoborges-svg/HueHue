# Instala o HueHue Switch no runner Windows, abre a biblioteca e tira prints das telas principais.
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
    $null = $shell.AppActivate("HueHue Switch")
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

# 1. Primeira abertura: biblioteca vazia.
Launch
Shot "1-biblioteca-vazia"
Close

# 2. Biblioteca com jogos ficticios (arquivos vazios: so o nome aparece, a capa e o placeholder).
$games = Join-Path $env:LOCALAPPDATA "Packages\$($app.PackageFamilyName)\LocalState\games"
New-Item -ItemType Directory -Force $games | Out-Null
foreach ($n in @("Turbo Rally", "Reino Perdido", "Gol de Placa Arena", "Pulo Estelar", "Corrida Espacial", "Ilha do Tesouro")) {
    New-Item -ItemType File -Force (Join-Path $games "$n.nsp") | Out-Null
}
Launch
Shot "2-biblioteca"
Key "{RIGHT}"
Shot "3-selecao"
Key "o"
Shot "4-configuracoes"
Key "{DOWN}"
Shot "5-configuracoes-item"
Key "{ENTER}"
Shot "6-submenu"
Key "{ESC}"
Key "{ESC}"
Key "{F1}"
Shot "7-controle"
Key "{ESC}"
Key "{F2}"
Shot "8-opcoes-do-jogo"
Close
Get-ChildItem $Out
