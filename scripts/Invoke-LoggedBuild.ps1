param(
    [Parameter(Mandatory = $true)][ValidateSet('app', 'engine', 'installer')][string]$Component,
    [Parameter(Mandatory = $true)][string]$BuildScript,
    [switch]$ArgumentsFromEnvironment,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$BuildArguments
)

$ErrorActionPreference = 'Stop'
$documents = [Environment]::GetFolderPath('MyDocuments')
if (-not $documents) { $documents = Join-Path $env:USERPROFILE 'Documents' }
$root = Join-Path $documents 'vibrance-dev/compilations'
# Tests and CI may choose a temporary location without changing the default.
if ($env:VIBRANCE_COMPILATION_LOG_ROOT) { $root = $env:VIBRANCE_COMPILATION_LOG_ROOT }
foreach ($outcome in @('successes', 'failures')) {
    foreach ($project in @('app', 'engine', 'installer')) {
        [void][IO.Directory]::CreateDirectory((Join-Path $root "$outcome/$project"))
    }
}
$run = '{0}-{1}-{2}' -f [DateTime]::UtcNow.ToString('yyyy-MM-dd_HH-mm-ss-fff'), $PID, [Guid]::NewGuid().ToString('N').Substring(0, 8)
# An interrupted run remains amongst failures, with all output received so far.
$log = Join-Path $root "failures/$Component/$run.log"
$summary = Join-Path $root "failures/$Component/$run.failure.txt"
$exitCode = 1
$reason = 'Build interrupted before completion.'
$writer = [IO.StreamWriter]::new($log, $false, [Text.UTF8Encoding]::new($false))
$writer.AutoFlush = $true
try {
    $writer.WriteLine("Component: $Component; started (UTC): $([DateTime]::UtcNow.ToString('o'))")
    $writer.WriteLine("Build script: $BuildScript")
    $writer.WriteLine('Output includes configuration, verbose compilation and installation/packaging.')
    Write-Host "Compilation log: $log"
    $guard = "VIBRANCE_BUILD_LOG_$($Component.ToUpperInvariant())"
    [Environment]::SetEnvironmentVariable($guard, '1', 'Process')
    # Cmd batch argument quoting follows the existing build entry point contract.
    $quoted = @()
    if ($BuildArguments) {
        $quoted = @($BuildArguments | ForEach-Object { '"' + $_.Replace('"', '""') + '"' })
    }
    $argumentString = $quoted -join ' '
    if ($ArgumentsFromEnvironment) { $argumentString = $env:VIBRANCE_LOGGED_BUILD_ARGUMENTS }
    $command = '""' + $BuildScript + '" ' + $argumentString + '"'
    # Pass cmd's command line directly: PowerShell's native argument conversion
    # otherwise corrupts its nested quotes when repository paths contain spaces.
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
public static class VibranceBuildTranscript {
    public static int Run(string executable, string command, StreamWriter writer) {
        object sync = new object();
        using (Process process = new Process()) {
            process.StartInfo = new ProcessStartInfo(executable, "/d /s /c " + command);
            process.StartInfo.UseShellExecute = false;
            process.StartInfo.CreateNoWindow = true;
            process.StartInfo.RedirectStandardOutput = true;
            process.StartInfo.RedirectStandardError = true;
            DataReceivedEventHandler receive = (sender, args) => {
                if (args.Data == null) return;
                lock (sync) { writer.WriteLine(args.Data); Console.WriteLine(args.Data); }
            };
            process.OutputDataReceived += receive;
            process.ErrorDataReceived += receive;
            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            process.WaitForExit();
            return process.ExitCode;
        }
    }
}
'@
    $exitCode = [VibranceBuildTranscript]::Run($env:ComSpec, $command, $writer)
    $reason = "Build process returned exit code $exitCode."
} catch {
    $reason = "Could not complete the build: $($_.Exception.Message)"
    $writer.WriteLine($reason)
} finally {
    $writer.Dispose()
    if ($exitCode -eq 0) {
        $destination = Join-Path $root "successes/$Component/$run.log"
        Add-Content -LiteralPath $log -Value "`n================ COMPILATION SUCCEEDED ================`nCompleted (UTC): $([DateTime]::UtcNow.ToString('o'))" -Encoding UTF8
        Move-Item -LiteralPath $log -Destination $destination
        Write-Host "COMPILATION SUCCEEDED. Full log: $destination" -ForegroundColor Green
    } else {
        $diagnostics = @(Select-String -LiteralPath $log -Pattern '(?i)fatal error|error:|error [A-Z]+[0-9]+|CMake Error|undefined reference|unresolved external|FAILED:|Error:|exception|not found|missing|cannot|could not' -Context 0, 5 | Where-Object { $_.Line -notmatch '^\s*(?:[A-Z]:\\.*>|echo\b|if\b)' } | Select-Object -First 12 | ForEach-Object { $_.ToString() })
        if ($diagnostics.Count -eq 0) { $diagnostics = @(Get-Content -LiteralPath $log -Tail 30) }
        $banner = @(
            '!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!',
            "COMPILATION FAILED: $($Component.ToUpperInvariant())", $reason,
            'FAILURE REASON / COMPILER DIAGNOSTICS:', ($diagnostics -join "`n"),
            "FULL COMPILATION LOG: $log",
            '!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!'
        ) -join "`n"
        Set-Content -LiteralPath $summary -Value $banner -Encoding UTF8
        Add-Content -LiteralPath $log -Value "`n$banner" -Encoding UTF8
        Write-Host $banner -ForegroundColor Red
    }
}
exit $exitCode
