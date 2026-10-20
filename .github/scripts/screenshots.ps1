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

# Telas do pacote de design (design/ns-huehue-v1). Desde a 0.4.4 o inicio e uma lista:
# Jogos, Favoritos, Recentes, Configuracoes, Sair do NS (sem cabecalho de abas; B nao sai).
# 1. Primeira abertura: biblioteca vazia.
Launch
Shot "01-inicio"
for ($i = 0; $i -lt 4; $i++) { Key "{DOWN}" }
Shot "01d-sair-do-ns"
Key "{UP}"
Key "{ENTER}"
Shot "11-configuracoes"
Key "{DOWN}"
Key "{ENTER}"
Shot "18-controles"
# Controles no PC: dispositivo, tipo, Modo Xbox/Switch, zona morta, Mapear botoes, teclado.
for ($i = 0; $i -lt 4; $i++) { Key "{DOWN}" }
Key "{ENTER}"
Shot "18b-mapear-botoes"
Key "{RIGHT}"
Shot "18c-mapear-botoes-alterado"
Key "{LEFT}"
Key "{ESC}"
Key "{ESC}"
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
for ($i = 0; $i -lt 9; $i++) { Key "{DOWN}" }
Shot "11c-sistema-volume"
Key "{RIGHT}"
Shot "11d-sistema-volume-110"
Key "{LEFT}"
Key "{ESC}"
Key "{ESC}"
Shot "01e-inicio-de-volta"
Close
Launch
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
# Jogos abre com o cursor em "Todos" (0.4.4); L/R (q/e) trocam as abas.
Shot "02-jogos-foco-em-todos"
Key "e"
Shot "02c-rb-favoritos"
Key "q"
Key "{DOWN}"
Key "{RIGHT}"
Shot "02b-selecao"
Key "x"
Shot "07-opcoes-do-jogo"
for ($i = 0; $i -lt 6; $i++) { Key "{DOWN}" }
Key "{ENTER}"
Shot "08-desempenho-e-graficos"
Key "{DOWN}"
Key "{RIGHT}"
Shot "08b-renderizador-opengl"
Key "{LEFT}"
for ($i = 0; $i -lt 11; $i++) { Key "{DOWN}" }
Shot "08c-desempenho-avancado"
Key "{ESC}"
Key "{DOWN}"
Key "{ENTER}"
Shot "07c-controles-do-jogo"
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
Shot "03-favoritos"
Key "{RIGHT}"
Shot "04-recentes"
Key "e"
Shot "04b-rb-traducoes"
Key "{ESC}"
Shot "01c-volta-ao-inicio"
Close
Launch
Key "{DOWN}"
Key "{ENTER}"
Shot "03b-favoritos-pelo-inicio"
Close
Launch
# Temas (Configuracoes > Sistema > Temas): Switch Escuro com previa, aplicado, e o reinicio.
Key "{DOWN}"
Key "{DOWN}"
Key "{DOWN}"
Key "{ENTER}"
Key "{ENTER}"
Key "{DOWN}"
Key "{ENTER}"
Shot "53-temas"
Key "{RIGHT}"
Key "{RIGHT}"
Key "{DOWN}"
Key "{ENTER}"
Shot "54-previa-switch-escuro"
Key "{ESC}"
Key "{RIGHT}"
Key "{ENTER}"
Shot "57-inicio-switch-escuro"
Key "{RIGHT}"
Shot "01s-switch-selecao"
# Tema Switch (0.4.4): L/R andam entre os jogos, X abre as opcoes do jogo e Y a busca.
Key "e"
Shot "01v-switch-rb-proximo-jogo"
Key "x"
Shot "07s-opcoes-do-jogo-pelo-inicio"
Key "{ESC}"
Shot "01w-switch-volta-ao-inicio"
Key "{DOWN}"
Key "{RIGHT}"
Shot "01t-switch-atalhos"
for ($i = 0; $i -lt 5; $i++) { Key "{RIGHT}" }
Shot "01x-switch-sair-do-ns"
Close
Launch
Shot "01u-switch-reaberto"
# Atalho 0: Biblioteca; 3: Controles; 4: Configuracoes.
Key "{DOWN}"
Key "{ENTER}"
Shot "02s-jogos-switch"
Key "{ESC}"
Key "{DOWN}"
Key "{RIGHT}"
Key "{RIGHT}"
Key "{RIGHT}"
Key "{ENTER}"
Shot "18s-controles-switch"
Key "{ESC}"
Key "{DOWN}"
Key "{RIGHT}"
Key "{ENTER}"
Shot "11s-configuracoes-switch"
# Volta ao tema Claro: Sistema > Temas.
Key "{ENTER}"
Key "{DOWN}"
Key "{ENTER}"
Key "{LEFT}"
Key "{DOWN}"
Key "{RIGHT}"
Key "{ENTER}"
Shot "57b-inicio-switch-claro"
Close
Launch
# Catalogo: o pacote NS HUEHUE (Assets/themes), previa e aplicado; Arquivos e USB.
Key "{DOWN}"
Key "{RIGHT}"
Key "{RIGHT}"
Key "{RIGHT}"
Key "{RIGHT}"
Key "{ENTER}"
Key "{ENTER}"
Key "{DOWN}"
Key "{ENTER}"
Key "{RIGHT}"
Key "{RIGHT}"
Shot "53b-temas-catalogo"
Key "{DOWN}"
Key "{ENTER}"
Shot "54b-previa-ns-huehue"
Key "{ESC}"
Key "{RIGHT}"
Key "{ENTER}"
Shot "57c-inicio-ns-huehue"
# O fundo do tema tambem nas outras telas: a grade de jogos e as configuracoes.
Key "{ENTER}"
Shot "02u-jogos-ns-huehue"
Key "{ESC}"
Key "{DOWN}"
Key "{DOWN}"
Key "{DOWN}"
Key "{ENTER}"
Shot "11u-configuracoes-ns-huehue"
Key "{DOWN}"
Key "{DOWN}"
Key "{ENTER}"
Shot "12-arquivos-e-usb"
Close
Get-ChildItem $Out
