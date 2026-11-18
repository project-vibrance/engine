# Diagnostics

Begin with the component that failed: configuration, compilation, renderer
initialisation, a native provider or application policy. Keep the SDK architecture,
compiler, driver and runtime-companion generation in the report.

## Build logs

`mingwBuild.bat` and `unixBuild.sh` archive complete configuration, build and
installation output and preserve the failing command's exit code. The logging
helpers organise transcripts under `Documents/vibrance-dev/compilations`, with
separate success/failure and component directories. A failure also produces a
short report containing the exit code, relevant diagnostics and full-log path.
`VIBRANCE_COMPILATION_LOG_ROOT` selects an alternative location for automation.
Direct CMake commands print their own output without that archived transcript.

Check configuration output before debugging code. It reports the target
architecture, renderer backend, optional audio/video features and native companion
availability. The CMake cache belongs to that build tree; use separate trees for
different architectures and toolchains.

## Runtime logging

Configure an explicit directory before constructing an engine when your
application needs a particular log location:

```cpp
LoggerOutputOptions logs;
logs.directory = "logs";
logs.filePrefix = "my_application";
Logger::configure_output(std::move(logs));
```

`Logger::fetch_logger()->output_path()` reports the actual current file.
`Logger::run_session` wraps process work with startup, exit and failure reporting.
Severity filtering, ordinary logging, detailed Vulkan logging and validation
logging have separate controls. Runtime records include UTC/elapsed time, severity,
thread identifiers and operation durations.

Runtime logs use bounded rotation and retention. Repeated messages are summarised,
large messages are truncated and crash reports have their own limits. The limits
and live-session protection are implemented in `core/logger.cpp` and the runtime
diagnostics helpers. Compilation transcripts are managed separately. Keep secrets,
credentials and unrelated personal data out of application log messages.

## Renderer checks

Build with `VIBRANCE_ENABLE_VULKAN_VALIDATION=ON` when diagnosing Vulkan correctness.
`VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING=ON` enables detailed renderer records.
Disable both when measuring normal performance.

Targeted tests can be selected by name:

```sh
ctest --test-dir build/windows-x64/Release \
  -R 'vibrance_(vulkan_api_policy|vulkan_shader_profile|control_interaction|stretch_dynamics|appearance_services)_tests' \
  --output-on-failure
```

In PowerShell, select the 1.2 path for an individual test run:

```powershell
$env:VIBRANCE_FORCE_VULKAN_12 = '1'
ctest --test-dir build/windows-x64/Release -R vulkan --output-on-failure
Remove-Item Env:VIBRANCE_FORCE_VULKAN_12
```

The offscreen shader harness also accepts
`VIBRANCE_SHADER_HARNESS_API_VERSION`, `VIBRANCE_SHADER_HARNESS_DEVICE` and
`VIBRANCE_SHADER_HARNESS_SNAPSHOT` for profile selection, adapter filtering and
captured readbacks. Compare complete output and GPU timings on the same adapter.
A forced API profile complements tests on physical legacy hardware.

The control checks cover material changes during gestures, geometry restoration,
text fitting, panel lifetime and retained updates. `vibrance_control_header_checks`
compiles every focused header separately. Desktop/renderer tests can skip when
their prerequisites are absent; a skip is not a hardware qualification result.

## Windows native glass

Run the broker distributed with the active SDK/application:

```powershell
.\liquid_glass_dwm_broker.exe --status
.\liquid_glass_dwm_broker.exe --runtime-update-status
```

Hook readiness and observed optical draw activity are separate diagnostics.
An idle or hidden surface does not establish whether an effect is visible.
Status also identifies device/monitor state, submission failures and the requested
versus active runtime generation. A verified staged replacement can report that
Windows sign-out is required; applications use the public runtime query to present
that state.

When a native effect fails, collect the launcher/setup/broker reports and the
matching SDK manifest. Confirm that the immutable runtime, shaders and companion
DLLs came from the same package before changing renderer parameters.

## Common failures

| Symptom | First checks |
| --- | --- |
| SDK package not found | `CMAKE_PREFIX_PATH`, installed package path and architecture |
| ARM64 dependency/link failure | Matching archives, LLVM-MinGW target and MSVC ARM64 companions |
| Vulkan initialisation rejected | Reported API version, required limits/formats and driver |
| Missing text or glyphs | Actual font path, loaded face, fallback/emoji paths and locale |
| A modal reappears after closing | Retained final transition and presentation completion |
| Missing native media/notification data | Companion loaded/ABI state, support flags and access result |
| Wallpaper preview contains other windows | Use the wallpaper provider for that preview |
| Glass is unavailable | Selected backend, presentation support and native runtime status |

When reporting an issue, include the smallest reproducer, target/platform,
relevant log excerpts, manifest and test result. Keep application document-policy
issues distinct from renderer or provider failures so the fix stays in its owning
module.
