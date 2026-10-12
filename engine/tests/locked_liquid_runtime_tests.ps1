param(
    [Parameter(Mandatory = $true)][string]$DeployScript,
    [Parameter(Mandatory = $true)][string]$Broker,
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
$names = @('vibrance_liquid_dwm_extension.dll', 'liquid_glass_win32.cso', 'liquid_glass_ui_win32.cso', 'liquid_glass_fullscreen.cso', 'liquid_glass_blur_horizontal.cso', 'liquid_glass_blur_vertical.cso')
foreach ($name in $names) { [IO.File]::WriteAllText((Join-Path $bundle $name), $name) }
function Stage {
    $staged = & $Broker --stage-runtime (Join-Path $bundle $names[0]) $cache
    Assert-True ($LASTEXITCODE -eq 0) 'Runtime cache staging failed'
    return [string]$staged
}
$first = Stage
Assert-True (Test-Path -LiteralPath $first) 'No cached DLL was published'
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
& $Broker --stage-runtime (Join-Path $bundle $names[0]) $cache
$ErrorActionPreference = 'Stop'
Assert-True ($LASTEXITCODE -ne 0) 'Incomplete bundles must fail staging'
Write-Host 'Locked DLL replacement and immutable runtime cache tests passed.'
