param(
    [Parameter(Mandatory = $true)] [string] $BaselineSpirv,
    [Parameter(Mandatory = $true)] [string] $CandidateSpirv,
    [ValidateSet('blur', 'shape', 'text', 'text-batch', 'liquid-cycle', 'foreground-group')] [string] $Shader = 'blur',
    [string] $Compiler = 'clang++',
    [switch] $Validation,
    [switch] $EquivalenceOnly,
    [switch] $BuildOnly,
    [string] $FreeTypeIncludeDirectory,
    [string] $FreeTypeLibrary,
    [string] $FontPath
)

$ErrorActionPreference = 'Stop'
if (-not $env:VULKAN_SDK) { throw 'VULKAN_SDK must point to the Vulkan SDK.' }
$taskEngineRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutputDirectory = Join-Path $taskEngineRoot 'build/portable-shader-performance'
New-Item -ItemType Directory -Path $taskOutputDirectory -Force | Out-Null
$taskExecutable = Join-Path $taskOutputDirectory 'blur_shader_performance.exe'
$taskCompiler = (Get-Command $Compiler -ErrorAction Stop).Source
$taskCompileArguments = @(
    '-std=c++20', '-O2', '-Wall', '-Wextra', '-Wno-missing-field-initializers',
    ('-I' + (Join-Path $env:VULKAN_SDK 'Include')),
    (Join-Path $PSScriptRoot 'blur_shader_performance.cpp'),
    (Join-Path $env:VULKAN_SDK 'Lib/vulkan-1.lib'), '-o', $taskExecutable
)
if ($FreeTypeIncludeDirectory -or $FreeTypeLibrary -or $FontPath) {
    if ($Shader -notin @('text', 'text-batch') -or -not $FreeTypeIncludeDirectory -or -not $FreeTypeLibrary -or -not $FontPath) {
        throw 'FreeType font fixtures require -Shader text or text-batch, -FreeTypeIncludeDirectory, -FreeTypeLibrary and -FontPath.'
    }
    $taskCompileArguments += @(
        '-DVIBRANCE_TEXT_FREETYPE=1',
        ('-I' + (Resolve-Path -LiteralPath $FreeTypeIncludeDirectory).Path),
        (Resolve-Path -LiteralPath $FreeTypeLibrary).Path
    )
}
& $taskCompiler @taskCompileArguments
if ($LASTEXITCODE -ne 0) { throw "Harness compilation failed: $LASTEXITCODE" }
if ($BuildOnly) { return }

$taskRunArguments = @(
    (Resolve-Path -LiteralPath $BaselineSpirv).Path,
    (Resolve-Path -LiteralPath $CandidateSpirv).Path
)
if ($Shader -ne 'blur') { $taskRunArguments += $Shader }
$taskPreviousValidation = $env:VIBRANCE_SHADER_HARNESS_VALIDATION
$taskPreviousEquivalenceOnly = $env:VIBRANCE_SHADER_HARNESS_EQUIVALENCE_ONLY
$taskPreviousFont = $env:VIBRANCE_SHADER_HARNESS_FONT
try {
    $env:VIBRANCE_SHADER_HARNESS_VALIDATION = if ($Validation) { '1' } else { '0' }
    $env:VIBRANCE_SHADER_HARNESS_EQUIVALENCE_ONLY = if ($EquivalenceOnly) { '1' } else { '0' }
    $env:VIBRANCE_SHADER_HARNESS_FONT = if ($FontPath) { (Resolve-Path -LiteralPath $FontPath).Path } else { $null }
    # Windows PowerShell represents native stderr as non-terminating errors
    # when the caller captures combined output. Validation warnings should
    # remain visible; the harness exit code reports actual comparison errors.
    $taskPreviousNativeErrorPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $taskExecutable @taskRunArguments
        $taskHarnessExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $taskPreviousNativeErrorPreference
    }
    if ($taskHarnessExitCode -ne 0) { throw "Offscreen comparison failed: $taskHarnessExitCode" }
} finally {
    $env:VIBRANCE_SHADER_HARNESS_VALIDATION = $taskPreviousValidation
    $env:VIBRANCE_SHADER_HARNESS_EQUIVALENCE_ONLY = $taskPreviousEquivalenceOnly
    $env:VIBRANCE_SHADER_HARNESS_FONT = $taskPreviousFont
}
