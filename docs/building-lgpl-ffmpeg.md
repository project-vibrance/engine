# Building the Windows LGPL FFmpeg SDKs

Vibrance links FFmpeg statically into the open-source engine DLL. The app and
installer link to that DLL rather than directly to FFmpeg. To keep this layout
redistributable alongside proprietary applications, the FFmpeg build must stay
LGPL-only and the corresponding engine and FFmpeg build materials must remain
available to recipients.

The build is pinned to FFmpeg 8.1.2 and produces separate UCRT SDKs for x64 and
ARM64. Both use the same Windows-hosted LLVM-MinGW installation. NASM is used
only for optimized x64 assembly.

## Prerequisites

- Official FFmpeg 8.1.2 source at `%USERPROFILE%\Downloads\ffmpeg-8.1.2`.
- UCRT LLVM-MinGW at `C:\llvm-mingw`.
- NASM at `C:\dev\nasm-3.02\nasm-3.02`.
- Git for Windows, CMake, and the Vulkan SDK.

Run from PowerShell:

```powershell
.\scripts\build_ffmpeg_windows.ps1 -Architecture all
```

Build only one target with `-Architecture x64` or `-Architecture arm64`.
Custom source, toolchain, NASM, and install roots are accepted as named script
arguments.

The installed SDKs are:

```text
C:\dev\ffmpeg-8.1.2-lgpl-static-windows-x64
C:\dev\ffmpeg-8.1.2-lgpl-static-windows-arm64
```

Each contains the five archives required by the engine:

```text
libavformat.a
libavcodec.a
libavutil.a
libswscale.a
libswresample.a
```

The script rejects a build unless `config.h` reports static LGPL 2.1-or-later
with GPL, nonfree, shared, and version-3 modes disabled. It also verifies the
COFF machine type of each build. Configuration evidence and the LGPL text are
copied to `<sdk>/share/ffmpeg-build`.

The source includes optional GPL files, which is normal. Never add
`--enable-gpl`, `--enable-nonfree`, GPL external libraries such as x264/x265,
or an unreviewed dependency to this build.

## Engine configuration

Use these local CMake values:

```cmake
set(MINGW_PATH "C:/llvm-mingw")
set(LLVM_MINGW_PATH "C:/llvm-mingw")
set(FFMPEG_PATH "C:/dev/ffmpeg-8.1.2-lgpl-static-windows-x64")
set(FFMPEG_ARM64_PATH "C:/dev/ffmpeg-8.1.2-lgpl-static-windows-arm64")
```

Build x64, ARM64, or both:

```powershell
.\mingwBuild.bat Release x64
.\mingwBuild.bat Release arm64
.\mingwBuild.bat Release all
```

## Distribution obligations

For every distributed engine build, retain and provide:

- the exact FFmpeg 8.1.2 source archive;
- this build script and the generated configuration evidence;
- the corresponding engine source and complete build scripts;
- the LGPL 2.1 license and FFmpeg attribution;
- a practical way for recipients to rebuild/relink the engine with a modified
  FFmpeg library.

Do not forbid reverse engineering needed to debug modifications to the LGPL
components. Refer to FFmpeg's current compliance guidance before each release:
https://ffmpeg.org/legal.html
