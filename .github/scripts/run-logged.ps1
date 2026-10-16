# Roda um comando guardando a saida em <nome>.log. Se falhar, publica as linhas de erro como
# anotacao do GitHub Actions (::error), que aparece na tela do run e pode ser lida pela API
# mesmo quando o log completo nao esta acessivel.
param(
    [Parameter(Mandatory)] [string] $Name,
    [Parameter(Mandatory)] [string] $Command
)
$log = Join-Path $env:GITHUB_WORKSPACE "$Name.log"
# stdin vem de NUL: nada pode ficar esperando entrada no runner.
cmd /c "$Command < NUL" 2>&1 | Tee-Object -FilePath $log
$code = $LASTEXITCODE
if ($code -ne 0) {
    $lines = Select-String -Path $log -Pattern 'error|fatal|failed|MISSING|not found|cannot' -CaseSensitive:$false |
             Select-Object -Last 30 | ForEach-Object { $_.Line.Trim() }
    if (-not $lines) { $lines = Get-Content $log -Tail 30 }
    $msg = ($lines | ForEach-Object { $_ -replace '%', '%25' -replace "`r", '' }) -join '%0A'
    Write-Host "::error title=$Name falhou (codigo $code)::$msg"
    exit $code
}
exit 0
