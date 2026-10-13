param([string]$OutputDirectory = (Join-Path $PSScriptRoot '../build/diagnostics-verification'))
$ErrorActionPreference = 'Stop'
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$root = Join-Path $OutputDirectory ('transcripts-' + [Guid]::NewGuid().ToString('N'))
$env:VIBRANCE_COMPILATION_LOG_ROOT = $root
$runner = Join-Path $PSScriptRoot 'Invoke-LoggedBuild.ps1'
$fixture = Join-Path $OutputDirectory 'build fixture with spaces.cmd'
Set-Content -LiteralPath $fixture -Encoding ASCII -Value @'
@echo off
echo CONFIGURATION STARTED
echo Argument one: [%~1]
echo Argument two: [%~2]
echo COMPLETE VERBOSE COMPILER COMMAND
echo Compiler stdout preserved
echo Compiler stderr preserved 1>&2
if "%~3"=="fail" (
    echo CMake Error at fixture.cmake:7: 1>&2
    echo   Required fixture dependency was unavailable. 1>&2
    exit /b 17
)
echo INSTALLATION COMPLETED
exit /b 0
'@
foreach ($component in @('app', 'engine', 'installer')) {
    foreach ($mode in @('success', 'fail')) {
        $env:VIBRANCE_LOGGED_BUILD_ARGUMENTS = '"argument with spaces" "" "' + $mode + '"'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $runner -Component $component -BuildScript $fixture -ArgumentsFromEnvironment *> (Join-Path $OutputDirectory "$component-$mode-verification.txt")
        $expected = if ($mode -eq 'fail') { 17 } else { 0 }
        if ($LASTEXITCODE -ne $expected) { throw "$component/$mode returned $LASTEXITCODE; expected $expected" }
        $outcome = if ($mode -eq 'fail') { 'failures' } else { 'successes' }
        $logs = @(Get-ChildItem -LiteralPath (Join-Path $root "$outcome/$component") -Filter '*.log')
        if ($logs.Count -ne 1) { throw "Expected one $component/$mode log" }
        $content = Get-Content -LiteralPath $logs[0].FullName -Raw
        foreach ($required in @('Compiler stdout preserved', 'Compiler stderr preserved', 'Argument one: [argument with spaces]', 'Argument two: []')) {
            if (-not $content.Contains($required)) { throw "Missing transcript output: $required" }
        }
        if ($mode -eq 'fail') {
            $summary = Get-Content -LiteralPath ($logs[0].FullName -replace '\.log$', '.failure.txt') -Raw
            if (-not $summary.Contains('COMPILATION FAILED') -or -not $summary.Contains('Required fixture dependency was unavailable.')) { throw 'Missing prominent failure reason' }
        }
    }
}
Write-Host 'Build logging checks passed: all six folders, stdout/stderr, arguments, full logs, failure reasons and exit codes.'
