# Roda um comando guardando a saida em <nome>.log. Se falhar, publica as linhas de erro como
# anotacao do GitHub Actions (::error), que aparece na tela do run e pode ser lida pela API
# mesmo quando o log completo nao esta acessivel.
param(
    [Parameter(Mandatory)] [string] $Name,
    [Parameter(Mandatory)] [string] $Command
)
$log = Join-Path $env:GITHUB_WORKSPACE "$Name.log"
# Log ao vivo: a cada 2 minutos publica o final do log no branch ci-live, para acompanhar etapas
# longas pela API sem depender do visualizador de logs do GitHub.
$live = $null
if ($env:CI_TOKEN) {
    $live = Start-ThreadJob -ArgumentList $log, $Name, $env:GITHUB_REPOSITORY, $env:CI_TOKEN, $env:RUNNER_TEMP -ScriptBlock {
        param($log, $name, $repo, $token, $tmp)
        $dir = Join-Path $tmp "live-$name"
        New-Item -ItemType Directory -Force $dir | Out-Null
        Set-Location $dir
        git init -q -b ci-live 2>$null
        git config user.name huehue-ci; git config user.email ci@users.noreply.github.com
        while ($true) {
            Start-Sleep 120
            try { Get-Content $log -Tail 300 -ErrorAction Stop | Set-Content (Join-Path $dir "$name.log") } catch {}
            "$name $(Get-Date -Format o)" | Set-Content (Join-Path $dir "HORA.txt")
            git add -A 2>$null; git commit -q -m "live $name" 2>$null
            git push -q -f "https://x-access-token:$token@github.com/$repo.git" ci-live 2>$null
        }
    }
}
# stdin vem de NUL: nada pode ficar esperando entrada no runner.
cmd /c "$Command < NUL" 2>&1 | Tee-Object -FilePath $log
$code = $LASTEXITCODE
if ($live) { Stop-Job $live -ErrorAction SilentlyContinue; Remove-Job $live -Force -ErrorAction SilentlyContinue }
if ($code -ne 0) {
    $lines = Select-String -Path $log -Pattern 'error|fatal|failed|MISSING|not found|cannot' -CaseSensitive:$false |
             Select-Object -Last 30 | ForEach-Object { $_.Line.Trim() }
    if (-not $lines) { $lines = Get-Content $log -Tail 30 }
    $msg = ($lines | ForEach-Object { $_ -replace '%', '%25' -replace "`r", '' }) -join '%0A'
    Write-Host "::error title=$Name falhou (codigo $code)::$msg"
    exit $code
}
exit 0
