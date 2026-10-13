param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

function Get-PeMachine {
    param([string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "Missing DOS header: $Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset -gt $stream.Length - 6) {
            throw "Invalid PE header offset: $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Missing PE signature: $Path"
        }
        $machine = $reader.ReadUInt16()
        switch ($machine) {
            0x8664 { return 'x64' }
            0xAA64 { return 'arm64' }
            default { return ('unknown-0x{0:X4}' -f $machine) }
        }
    }
    finally {
        $stream.Dispose()
    }
}

$systemDirectory = [Environment]::SystemDirectory
$os = Get-CimInstance Win32_OperatingSystem
$modules = @('uDWM.dll', 'dwmcore.dll') | ForEach-Object {
    $path = Join-Path $systemDirectory $_
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required DWM module is absent: $path"
    }
    $item = Get-Item -LiteralPath $path
    [pscustomobject]@{
        name = $_
        machine = Get-PeMachine -Path $path
        fileVersion = $item.VersionInfo.FileVersion
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
}

$result = [pscustomobject]@{
    osVersion = $os.Version
    osBuild = $os.BuildNumber
    modules = $modules
}
$json = $result | ConvertTo-Json -Depth 4
if ($OutputPath) {
    [System.IO.File]::WriteAllText(
        [System.IO.Path]::GetFullPath($OutputPath), $json,
        [System.Text.UTF8Encoding]::new($false))
}
$json
