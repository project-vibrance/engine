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

Runtime text logs rotate at **5 MiB per file**, with **four older parts** beside
the current file (`.1` is the newest archive). Rotation keeps the current
filename stable so crash reports continue to belong to the same session.
Retention runs at startup, rotation and at most once a minute during logging:
it removes the oldest closed runtime artefacts to target **50 MiB per runtime
folder**, expires closed files after **seven days**, and retains at most
**100 artefacts**, including at most **three minidumps**. Files belonging to live
sessions are protected, so simultaneous processes can temporarily exceed the
folder budget; each process's rotating text logs are bounded to 25 MiB. Cleanup
only recognises Vibrance runtime filenames and never includes unrelated files or
compilation transcripts. Cleanup failures do not permit an active text log to
grow past its size limit: file output pauses until rotation can be retried.

Identical consecutive messages are summarised once every five seconds, with a
repeat count when the next record is emitted. Fatal records are always emitted.
Individual text messages are truncated at 64 KiB; emergency reports have a
1 MiB write budget per reporting module. Windows minidumps are limited to 5 MiB;
an oversized dump is discarded while the exception details remain in the text
report. Compilation logs retain their complete output independently of these
runtime limits.

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
