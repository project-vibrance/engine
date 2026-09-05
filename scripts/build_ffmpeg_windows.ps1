[CmdletBinding()]
param(
    [string]$SourceDir = (Join-Path $env:USERPROFILE 'Downloads\ffmpeg-8.1.2'),
    [ValidateSet('x64', 'arm64', 'all')]
    [string]$Architecture = 'all',
    [string]$LlvmMingwRoot = 'C:\llvm-mingw',
    [string]$NasmRoot = 'C:\dev\nasm-3.02\nasm-3.02',
    [string]$InstallRoot = 'C:\dev',
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount)
)

$ErrorActionPreference = 'Stop'

function Assert-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found at '$Path'."
    }
}

function Invoke-Checked([string]$FilePath, [string[]]$Arguments) {
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
    }
}

$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
Assert-File (Join-Path $SourceDir 'configure') 'FFmpeg configure script'
Assert-File (Join-Path $SourceDir 'COPYING.LGPLv2.1') 'FFmpeg LGPL text'

$version = (Get-Content -LiteralPath (Join-Path $SourceDir 'RELEASE') -Raw).Trim()
if ($version -ne '8.1.2') {
    throw "This build is pinned to FFmpeg 8.1.2; '$SourceDir' reports '$version'."
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repositoryRoot 'build\dependencies'
$sourceCopy = Join-Path $buildRoot "ffmpeg-$version-source-native-make"
$bash = Join-Path $env:ProgramFiles 'Git\bin\bash.exe'
$make = Join-Path $LlvmMingwRoot 'bin\mingw32-make.exe'
$readobj = Join-Path $LlvmMingwRoot 'bin\llvm-readobj.exe'

Assert-File $bash 'Git Bash'
Assert-File $make 'LLVM-MinGW GNU Make'
Assert-File $readobj 'LLVM readobj'
Assert-File (Join-Path $LlvmMingwRoot 'bin\x86_64-w64-mingw32-clang.exe') 'x64 compiler'
Assert-File (Join-Path $LlvmMingwRoot 'bin\aarch64-w64-mingw32-clang.exe') 'ARM64 compiler'

New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
if (-not (Test-Path -LiteralPath $sourceCopy)) {
    New-Item -ItemType Directory -Path $sourceCopy | Out-Null
    Get-ChildItem -LiteralPath $SourceDir -Force |
        Where-Object { $_.Name -ne '.git' } |
        Copy-Item -Destination $sourceCopy -Recurse -Force
}

# Native Windows GNU Make cannot create FFmpeg's very long archive response
# files through cmd.exe. GNU Make's file function writes the same list without
# passing it through the Windows command line. This changes build machinery
# only; no compiled FFmpeg source is modified.
$libraryMakefile = Join-Path $sourceCopy 'ffbuild\library.mak'
$libraryMakefileText = [IO.File]::ReadAllText($libraryMakefile)
$oldResponseRule = "`t" + '$(Q)echo $^ > $@.objs'
$newResponseRule = "`t" + '$(file >$@.objs,$^)'
if ($libraryMakefileText.Contains($oldResponseRule)) {
    [IO.File]::WriteAllText(
        $libraryMakefile,
        $libraryMakefileText.Replace($oldResponseRule, $newResponseRule))
} elseif (-not $libraryMakefileText.Contains($newResponseRule)) {
    throw "FFmpeg's response-file rule has changed; review '$libraryMakefile'."
}

$targets = if ($Architecture -eq 'all') { @('x64', 'arm64') } else { @($Architecture) }
$originalPath = $env:PATH
try {
    $env:PATH = "$(Join-Path $LlvmMingwRoot 'bin');$NasmRoot;$originalPath"

    foreach ($target in $targets) {
        if ($target -eq 'x64') {
            Assert-File (Join-Path $NasmRoot 'nasm.exe') 'NASM'
            $ffmpegArch = 'x86_64'
            $toolPrefix = 'x86_64-w64-mingw32-'
            $expectedMachine = 'IMAGE_FILE_MACHINE_AMD64'
            $crossArguments = @()
        } else {
            $ffmpegArch = 'aarch64'
            $toolPrefix = 'aarch64-w64-mingw32-'
            $expectedMachine = 'IMAGE_FILE_MACHINE_ARM64'
            $crossArguments = @('--enable-cross-compile', "--cross-prefix=$toolPrefix")
        }

        $buildDir = Join-Path $buildRoot "ffmpeg-$version-windows-$target-static-lgpl"
        $prefix = Join-Path $InstallRoot "ffmpeg-$version-lgpl-static-windows-$target"
        New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
        $sourceJunction = Join-Path $buildDir 'src'
        if (-not (Test-Path -LiteralPath $sourceJunction)) {
            New-Item -ItemType Junction -Path $sourceJunction -Target $sourceCopy | Out-Null
        }

        $configureArguments = @(
            'src/configure',
            "--prefix=$($prefix.Replace('\', '/'))",
            '--target-os=mingw32',
            "--arch=$ffmpegArch",
            "--cc=${toolPrefix}clang",
            "--cxx=${toolPrefix}clang++",
            "--ar=${toolPrefix}ar",
            "--ranlib=${toolPrefix}ranlib",
            "--strip=${toolPrefix}strip",
            "--nm=${toolPrefix}nm",
            "--windres=${toolPrefix}windres",
            '--enable-static',
            '--disable-shared',
            '--enable-pic',
            '--disable-autodetect',
            '--disable-gpl',
            '--disable-nonfree',
            '--disable-version3',
            '--disable-debug',
            '--disable-doc',
            '--disable-programs',
            '--disable-avdevice',
            '--disable-avfilter'
        ) + $crossArguments

        Push-Location $buildDir
        try {
            Invoke-Checked $bash $configureArguments
            Invoke-Checked $make @("-j$Jobs")
            Invoke-Checked $make @('install')
        } finally {
            Pop-Location
        }

        $configHeader = Join-Path $buildDir 'config.h'
        $configText = [IO.File]::ReadAllText($configHeader)
        foreach ($required in @(
            '#define FFMPEG_LICENSE "LGPL version 2.1 or later"',
            '#define CONFIG_SHARED 0',
            '#define CONFIG_STATIC 1',
            '#define CONFIG_GPL 0',
            '#define CONFIG_NONFREE 0',
            '#define CONFIG_VERSION3 0')) {
            if (-not $configText.Contains($required)) {
                throw "License/configuration check failed for ${target}: missing '$required'."
            }
        }

        $objectFile = Join-Path $buildDir 'libavutil\adler32.o'
        $headerText = (& $readobj --file-headers $objectFile) -join "`n"
        if ($headerText -notmatch [regex]::Escape($expectedMachine)) {
            throw "Architecture check failed for '$objectFile'; expected $expectedMachine."
        }

        $complianceDir = Join-Path $prefix 'share\ffmpeg-build'
        New-Item -ItemType Directory -Force -Path $complianceDir | Out-Null
        Copy-Item -LiteralPath $configHeader -Destination $complianceDir -Force
        Copy-Item -LiteralPath (Join-Path $buildDir 'ffbuild\config.mak') -Destination $complianceDir -Force
        Copy-Item -LiteralPath (Join-Path $SourceDir 'LICENSE.md') -Destination $complianceDir -Force
        Copy-Item -LiteralPath (Join-Path $SourceDir 'COPYING.LGPLv2.1') -Destination $complianceDir -Force
        $configureArguments | Set-Content -LiteralPath (Join-Path $complianceDir 'configure-arguments.txt')

        Write-Host "Verified $target LGPL static FFmpeg SDK: $prefix"
    }
} finally {
    $env:PATH = $originalPath
}
