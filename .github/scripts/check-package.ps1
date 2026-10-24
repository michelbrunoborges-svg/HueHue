# NS HUEHUE: confere um pacote antes de entrega-lo: assinatura, conteudo (manifesto, versao,
# arquivos do app e do Mesa) e instalacao como atualizacao sobre a 0.3.21.0, a ultima que instalou
# no Xbox do Michel. Erros saem como anotacoes do job (::error::), lidas pela API.
param(
    [Parameter(Mandatory)] [string] $Pkg,
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $Repo
)
$env:APP_VERSION = $Version
$pkg = $Pkg
$ErrorActionPreference = "Stop"
trap {
  # A mensagem vira anotacao do job (lida pela API), com o log do instalador quando houver.
  Write-Host "::error::$($_.Exception.Message -replace "`r?`n", ' | ')"
  try {
    Get-AppxLog -All -ErrorAction SilentlyContinue | Where-Object { $_.Type -eq "Error" -or $_.LevelDisplayName -eq "Error" } |
      Select-Object -First 6 | ForEach-Object { Write-Host "::error::appx: $($_.Message -replace "`r?`n", ' | ')" }
  } catch {}
  exit 1
}

# O instalador se chama NSHueHue-<versao>.appx (0.4.7 em diante); os anteriores, huehue-switch.appx.
$appx = (Get-ChildItem $pkg -Filter "NSHueHue-*.appx" | Select-Object -First 1).FullName
if (-not $appx) { $appx = "$pkg\huehue-switch.appx" }
$cer = (Get-ChildItem $pkg -Filter "NSHueHue-*.cer" | Select-Object -First 1).FullName
if (-not $cer) { $cer = "$pkg\huehue-switch.cer" }
# 1. Assinatura valida com o certificado publicado junto.
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe |
  Where-Object FullName -match "x64" | Sort-Object FullName -Descending | Select-Object -First 1
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
& $signtool.FullName verify /pa /v $appx | Select-Object -Last 4
if ($LASTEXITCODE) { throw "assinatura invalida" }
# 2. Conteudo: manifesto com a versao do build, identidade de sempre e os arquivos do app.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path $appx))
$names = $zip.Entries | ForEach-Object FullName
$manifest = (New-Object IO.StreamReader(($zip.Entries | Where-Object FullName -eq "AppxManifest.xml").Open())).ReadToEnd()
$zip.Dispose()
foreach ($n in @("eden-uwp.exe","dxil.dll","spirv_to_dxil.dll","opengl32.dll","libgallium_wgl.dll","libglapi.dll","AppxManifest.xml","AppxBlockMap.xml","AppxSignature.p7x")) {
  if ($names -notcontains $n) { throw "falta $n no pacote" }
}
if ($manifest -notmatch 'Name="HueHue.HueHueSwitch"') { throw "identidade do pacote mudou" }
if ($manifest -notmatch "Version=`"$env:APP_VERSION`"") { throw "versao do manifesto diferente de $env:APP_VERSION" }
Write-Host "::notice::conteudo ok: $($names.Count) arquivos, versao $env:APP_VERSION"
# 3. Instala a ultima versao que funcionou no Xbox (0.3.21.0) e depois esta, por cima.
$unlock = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock"
New-Item -Path $unlock -Force | Out-Null
Set-ItemProperty $unlock AllowDevelopmentWithoutDevLicense 1 -Type DWord
Set-ItemProperty $unlock AllowAllTrustedApps 1 -Type DWord
gh release download v0.3.21.0 -R $Repo -D old
Import-Certificate -FilePath "old\huehue-switch.cer" -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
Import-Certificate -FilePath "old\huehue-switch.cer" -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
Add-AppxPackage -Path "old\huehue-switch.appx" -DependencyPath "old\Microsoft.VCLibs.x64.14.00.appx"
Write-Host "::notice::instalada: $((Get-AppxPackage -Name HueHue.HueHueSwitch).Version)"
Add-AppxPackage -Path $appx -DependencyPath "$pkg\Microsoft.VCLibs.x64.14.00.appx"
$now = (Get-AppxPackage -Name HueHue.HueHueSwitch).Version
Write-Host "::notice::atualizada para: $now"
if ($now -ne $env:APP_VERSION) { throw "a atualizacao nao entrou ($now)" }
Remove-AppxPackage (Get-AppxPackage -Name HueHue.HueHueSwitch).PackageFullName
