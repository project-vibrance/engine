param(
    [Parameter(Mandatory = $true)][string]$DeployScript,
    [Parameter(Mandatory = $true)][string]$Broker,
    [Parameter(Mandatory = $true)][string]$DbgHelp,
    [Parameter(Mandatory = $true)][string]$SymSrv,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $OutputDirectory ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
function Assert-True($condition, [string]$message) {
    if (-not $condition) { throw $message }
}
function Deploy([string]$source, [string]$destination) {
    & cmake "-DVIBRANCE_RUNTIME_SOURCE=$source" "-DVIBRANCE_RUNTIME_DESTINATION=$destination" -P $DeployScript
    Assert-True ($LASTEXITCODE -eq 0) 'DLL deployment failed'
}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LiquidRuntimeTestMapping {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr module);
}
'@
$source = Join-Path $fixture 'source.dll'
$destination = Join-Path $fixture 'deployed.dll'
Copy-Item -LiteralPath "$env:WINDIR\System32\version.dll" -Destination $source
Deploy $source $destination
$module = [LiquidRuntimeTestMapping]::LoadLibraryExW($destination, [IntPtr]::Zero, 1)
Assert-True ($module -ne [IntPtr]::Zero) 'Cannot map the fixture DLL'
try {
    Deploy $source $destination
    Assert-True (@(Get-ChildItem -LiteralPath $fixture -Filter '*.in-use-*').Count -eq 0) 'Identical DLLs should not be renamed'
    $stream = [IO.File]::OpenWrite($source)
    try {
        $stream.Seek(0, [IO.SeekOrigin]::End) | Out-Null
        $stream.WriteByte(42)
    } finally { $stream.Dispose() }
    Deploy $source $destination
    Assert-True ((Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $destination).Hash) 'The installed DLL is stale'
    Assert-True (@(Get-ChildItem -LiteralPath $fixture -Filter '*.in-use-*').Count -eq 1) 'The mapped image was not retained'
    $before = (Get-FileHash -LiteralPath $destination).Hash
    $ErrorActionPreference = 'Continue'
    & cmake "-DVIBRANCE_RUNTIME_SOURCE=$fixture\missing.dll" "-DVIBRANCE_RUNTIME_DESTINATION=$destination" -P $DeployScript 2>&1 | Out-String | Write-Host
    $ErrorActionPreference = 'Stop'
    Assert-True ($LASTEXITCODE -ne 0) 'A missing source must fail deployment'
    Assert-True ((Get-FileHash -LiteralPath $destination).Hash -eq $before) 'A failed deployment changed the installed DLL'
} finally { [LiquidRuntimeTestMapping]::FreeLibrary($module) | Out-Null }

$installProject = Join-Path $fixture 'install-project'
$installBuild = Join-Path $fixture 'install-build'
$installPrefix = Join-Path $fixture 'override-prefix'
New-Item -ItemType Directory -Path $installProject | Out-Null
[IO.File]::WriteAllText((Join-Path $installProject 'CMakeLists.txt'), @"
cmake_minimum_required(VERSION 3.28)
project(LockedRuntimeInstall NONE)
include([=[$DeployScript]=])
vibrance_install_locked_runtime([=[$source]=] bin)
"@)
& cmake -S $installProject -B $installBuild "-DCMAKE_INSTALL_PREFIX=$fixture\unused-prefix"
Assert-True ($LASTEXITCODE -eq 0) 'Install fixture configuration failed'
& cmake --install $installBuild --prefix $installPrefix
Assert-True ($LASTEXITCODE -eq 0) 'Install prefix override failed'
Assert-True (Test-Path -LiteralPath (Join-Path $installPrefix 'bin\source.dll')) 'The install prefix override was ignored'

$bundle = Join-Path $fixture 'bundle'
$cache = Join-Path $fixture 'cache'
New-Item -ItemType Directory -Path $bundle | Out-Null
$names = @('vibrance_liquid_dwm_extension.dll', 'liquid_glass_win32.cso', 'liquid_glass_ui_win32.cso', 'liquid_glass_fullscreen.cso', 'liquid_glass_blur_horizontal.cso', 'liquid_glass_blur_vertical.cso', 'liquid_glass_compare.cso')
foreach ($name in $names) { [IO.File]::WriteAllText((Join-Path $bundle $name), $name) }
function Stage {
    $staged = & $Broker --stage-runtime (Join-Path $bundle $names[0]) $cache
    Assert-True ($LASTEXITCODE -eq 0) 'Runtime cache staging failed'
    return [string]$staged
}
$first = Stage
Assert-True (Test-Path -LiteralPath $first) 'No cached DLL was published'
# These comparisons are read-only and must cover every byte of the active
# extension and its six shaders, even when a compositor is already ready.
& $Broker --check-runtime (Join-Path $bundle $names[0]) $first
Assert-True ($LASTEXITCODE -eq 0) 'An identical active runtime was rejected'
foreach ($name in $names) {
    [IO.File]::WriteAllText((Join-Path $bundle $name), "$name updated")
    & $Broker --check-runtime (Join-Path $bundle $names[0]) $first
    Assert-True ($LASTEXITCODE -eq 25) "Changing $name did not require a new Windows session"
    [IO.File]::WriteAllText((Join-Path $bundle $name), $name)
}
$dwmSid = [Security.Principal.SecurityIdentifier]::new('S-1-5-90-0')
function Assert-DwmReadAccess([string]$directory) {
    foreach ($path in @($directory) + @($names | ForEach-Object { Join-Path $directory $_ })) {
        $rules = (Get-Acl -LiteralPath $path).GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])
        $allowed = @($rules | Where-Object {
            $_.IdentityReference -eq $dwmSid -and
            $_.AccessControlType -eq [Security.AccessControl.AccessControlType]::Allow
        })
        Assert-True ($allowed.Count -gt 0) "DWM cannot access $path"
        $rights = 0
        foreach ($rule in $allowed) { $rights = $rights -bor [int]$rule.FileSystemRights }
        $readExecute = [int][Security.AccessControl.FileSystemRights]::ReadAndExecute
        Assert-True (($rights -band $readExecute) -eq $readExecute) "DWM lacks read and execute access to $path"
        $write = [int][Security.AccessControl.FileSystemRights]::Write
        Assert-True (($rights -band $write) -eq 0) "DWM should not be granted write access to $path"
    }
}
Assert-DwmReadAccess (Split-Path $first)
# Reproduce the private ACL of a cache created before the permissions fix.
$privateAcl = [Security.AccessControl.FileSecurity]::new()
$privateAcl.SetAccessRuleProtection($true, $false)
$privateAcl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent().User,
    [Security.AccessControl.FileSystemRights]::FullControl,
    [Security.AccessControl.AccessControlType]::Allow))
Set-Acl -LiteralPath $first -AclObject $privateAcl
Assert-True ((Stage) -eq $first) 'Identical bundles should reuse the cache'
Assert-DwmReadAccess (Split-Path $first)
foreach ($name in $names) {
    Assert-True ((Get-FileHash -LiteralPath (Join-Path $bundle $name)).Hash -eq (Get-FileHash -LiteralPath (Join-Path (Split-Path $first) $name)).Hash) "Cached $name differs"
}
[IO.File]::WriteAllText((Join-Path $bundle $names[1]), 'updated shader')
$second = Stage
Assert-True ($second -ne $first) 'A shader change must publish a new bundle'
Assert-True (Test-Path -LiteralPath $first) 'A previously loaded bundle must be retained'
Remove-Item -LiteralPath (Join-Path $bundle $names[2])
$ErrorActionPreference = 'Continue'
& $Broker --check-runtime (Join-Path $bundle $names[0]) $first
Assert-True ($LASTEXITCODE -eq 3) 'An incomplete requested runtime must not be reported as staged or current'
& $Broker --stage-runtime (Join-Path $bundle $names[0]) $cache
$ErrorActionPreference = 'Stop'
Assert-True ($LASTEXITCODE -ne 0) 'Incomplete bundles must fail staging'

# Automatic elevation must cover the broker and symbol-server dependencies too,
# so changing any executable or shader creates a different protected bundle.
$helperBundle = Join-Path $fixture 'helper-bundle'
New-Item -ItemType Directory -Path $helperBundle | Out-Null
$helperNames = @('liquid_glass_dwm_broker.exe', 'liquid_glass_dwm_observer.exe') + $names + @('dbghelp.dll', 'symsrv.dll')
foreach ($name in $helperNames) { [IO.File]::WriteAllText((Join-Path $helperBundle $name), $name) }
$key = & $Broker --bundle-key $helperBundle
Assert-True ($LASTEXITCODE -eq 0 -and $key -match '^[0-9a-f]{64}$') 'Cannot fingerprint the complete helper bundle'
foreach ($name in $helperNames) {
    [IO.File]::WriteAllText((Join-Path $helperBundle $name), "$name updated")
    $updated = & $Broker --bundle-key $helperBundle
    Assert-True ($LASTEXITCODE -eq 0 -and $updated -ne $key) "Changing $name did not version the helper"
    [IO.File]::WriteAllText((Join-Path $helperBundle $name), $name)
}
Remove-Item -LiteralPath (Join-Path $helperBundle 'symsrv.dll')
& $Broker --bundle-key $helperBundle
Assert-True ($LASTEXITCODE -ne 0) 'A helper without symbol-server support must be rejected'

# Inspect Windows' actual task XML without installing or running an elevated task.
# The build directory does not contain packaged dbghelp/symsrv DLLs; use a copy
# of the broker with its complete SDK bundle for this read-only check.
$configBundle = Join-Path $fixture 'config-bundle'
New-Item -ItemType Directory -Path $configBundle | Out-Null
Copy-Item -LiteralPath $Broker -Destination (Join-Path $configBundle 'liquid_glass_dwm_broker.exe')
foreach ($name in $helperNames | Where-Object { $_ -ne 'liquid_glass_dwm_broker.exe' }) {
    [IO.File]::WriteAllText((Join-Path $configBundle $name), $name)
}
Copy-Item -LiteralPath $DbgHelp -Destination (Join-Path $configBundle 'dbghelp.dll') -Force
Copy-Item -LiteralPath $SymSrv -Destination (Join-Path $configBundle 'symsrv.dll') -Force
$configLines = & (Join-Path $configBundle 'liquid_glass_dwm_broker.exe') --auto-config
Assert-True ($LASTEXITCODE -eq 0) 'Cannot generate the automatic helper task definition'
[xml]$config = $configLines -join "`n"
Assert-True ($config.Task.Principals.Principal.LogonType -eq 'InteractiveToken') 'The helper must use the interactive session'
Assert-True ($config.Task.Principals.Principal.RunLevel -eq 'HighestAvailable') 'The helper must retain approved elevation'
Assert-True ($config.Task.Actions.Exec.Arguments -eq '--watch') 'The elevated action must have fixed arguments'
Assert-True ($config.Task.Actions.Exec.Command.EndsWith('\liquid_glass_dwm_observer.exe')) 'The scheduled helper must use the windowless observer'
Assert-True ($config.Task.Actions.Exec.Command.StartsWith([Environment]::GetFolderPath('ProgramFiles') + '\VibranceLiquidGlass\')) 'The elevated executable is not in protected Program Files'
Assert-True ($config.Task.Settings.DisallowStartIfOnBatteries -eq 'false') 'Battery operation must not prevent startup'
Assert-True ($config.Task.Settings.StopIfGoingOnBatteries -eq 'false') 'Battery operation must not stop the helper'
Assert-True ($config.Task.Settings.ExecutionTimeLimit -eq 'PT0S') 'The observer must survive long-running sessions'
Assert-True ($config.Task.Triggers.LogonTrigger.UserId -eq [Security.Principal.WindowsIdentity]::GetCurrent().User.Value) 'Logon startup belongs to another user'
Write-Host 'Locked DLL replacement and immutable runtime cache tests passed.'
