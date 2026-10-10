# Runtime and compilation diagnostics

Build with `mingwBuild.bat` on Windows or `unixBuild.sh` on Unix/macOS. These entry
points capture configuration, dependency builds, verbose compiler and linker
commands, installation and packaging, including both stdout and stderr. They
preserve the failing process's exit code. Windows uses the user's actual Documents
known folder, including redirected Documents folders.

```text
Documents/vibrance-dev/compilations/
  successes/
    app/
    engine/
    installer/
  failures/
    app/
    engine/
    installer/
```

Each invocation has a unique UTC timestamp and process identifier. A successful
run has a complete `.log` in its success directory. A failed run has a complete
`.log` and a `.failure.txt` containing a prominent `COMPILATION FAILED` banner,
exit code, compiler/CMake diagnostics with context, and the full log's location.
An interrupted run leaves its partial transcript in the failure directory.
The app's automatic engine refresh produces an engine log as well as including
that output in the app log. For CI or tests, `VIBRANCE_COMPILATION_LOG_ROOT` can
replace the compilation directory. Direct CMake commands can still be used;
use the build entry points when a complete archived transcript is required.

On Windows, app and installer runtime logs are in `Documents/Vibrance`. The
app's macOS logs are in `~/Library/Logs/Vibrance`. Standalone engine users default
to `Documents/vibrance-dev/runtime/engine`, and may choose a path with
`Logger::configure_output`. Engine messages from the app/installer share that
process's runtime log.

Runtime records include UTC and elapsed timestamps, severity, thread identifiers,
startup and exit, operation boundaries and durations, renderer readiness,
resource loading, settings, authentication outcomes, update state transitions,
audio capture, media/notification commands, installer pages, payload verification,
installation results and rollback errors. `BEGIN`/`END` records trace control flow;
semantic success/failure has a separate outcome record. Update diagnostics omit
bearer tokens and credential contents. High-frequency frame/sample processing is
covered through errors and existing optional renderer diagnostics rather than
one log line per frame/sample. Vulkan errors remain visible in Release builds.

Emergency reports use the runtime log's name plus `.crash.txt`. The report is
pre-opened and bypasses logger locks, so aborts and fatal faults can be recorded
even when another thread holds a logging lock. Empty emergency files belong to
healthy runs. Windows native faults also produce `.dmp` minidumps when DbgHelp is
available; these preserve exception codes, addresses and stack/module information
for debugger analysis. The installer bootstrap, updater, uninstaller and startup
service use independent diagnostics before loading the engine. Ordinary desktop
helpers write to Documents/Vibrance; a service resolves Documents for its service
account. Crashes are best-effort: forced process termination and loss of power
cannot execute a crash handler. Check for a missing session-end record as well.

Rebuild and install the engine SDK before rebuilding its consumers. The logger
API and operation diagnostics require matching engine headers and runtime.

Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Test-BuildLogging.ps1`
to verify all six compilation folders, stdout/stderr, argument handling, failure
summaries and exit codes using fixtures. Engine CTest includes concurrent logging,
exception reporting and isolated Windows crash/minidump fixtures.
