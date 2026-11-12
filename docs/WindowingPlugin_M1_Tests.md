# M1 automated acceptance

M1 exercises the current API with three simultaneous native UMG windows in one Win64 session. The runner builds and cooks a blank C++ sample with a fresh copy of the plugin, then launches each packaged game process. The sample runs assertions through the public API and writes a text report. The runner requires both a passing report and a clean process exit. The test harness lives in the sample game module and works in Shipping without enabling Unreal's Development automation framework.

Run this from the plugin repository on an interactive Windows desktop with UE 5.8 and its Win64 C++ toolchain installed. The lifecycle scenario routes pointer events through Slate's hit testing for a specific window, posts native Windows character and close messages, and verifies the widget state. OS foreground activation and focus reporting belong to M3.

```powershell
.\Tests\Run-M1Acceptance.ps1
.\Tests\Run-M1Acceptance.ps1 -Configuration Development
```

Pass `-EngineRoot` for a different UE 5.8 install or `-OutputRoot` for a new directory where you want to keep the packages and reports. The default output is a unique directory under the system temp directory. The runner prints its location. Packaging logs are named by configuration. Scenario reports start with `PASS` or `FAIL` and contain the cycle and event counts or the failed assertions.

| Scenario | Assertions |
|---|---|
| `lifecycle` | Three distinct handles and widgets, title changes, mouse and keyboard input before and after neighboring windows close, native title-bar close, reentrant close from opened and closed events, duplicate close rejection, stale and cross-session handles, independent session shutdown, GC while open and closing, invalid inputs, rollback on session loss during widget construction, and 100 open and close cycles. |
| `shutdown` | The game requests normal exit with two windows open and one finalization pending. The runner requires a passing setup report and a successful game exit. |
| `registration-rollback` | Slate off-screen mode registers a window without an HWND. The plugin rejects it, destroys the partial Slate window, publishes no handle or event, and releases the widget for GC. |

The lifecycle report should finish at 100 cycles, 104 opened events, and 104 closed events in the primary session. The test also opens a window in a second session and verifies that its teardown suppresses the late close event. Each stress cycle checks retirement of its handle and removal from Slate. Periodic GC checks verify retired widgets are collectible.

Use the [M0 Blueprint sample](WindowingPlugin_M0_Step_6_Test_Guide.md) for an interactive demonstration. Use this runner as the M1 acceptance gate.

## Verification

All three scenarios passed on UE 5.8.4 (CL 58902052) in both packaged Win64 Development and Shipping. Each lifecycle run completed 100 cycles with 104 opened and 104 closed events.

## Optional M5 docking scenarios

From the plugin root, run `.\Tests\Run-M1Acceptance.ps1 -Configuration Development -IncludeDocking`. This packages the sample, runs the three existing scenarios, then runs `m51` and `m52` in windowed games with a runtime workspace. Add `Shipping` to the configuration list to run both builds.

The docking scenario opens two real windows, exercises all four dock edges, moves a two-panel group out and back, checks the original widget and its state, and checks one close per docked panel. The manual M5.1 guide also covers occupied return edges, a native group title-bar close, and injected transfer rollback. [M5.1 manual acceptance](WindowingPlugin_M5_1_Dock_Undock_Guide.md#6-live-acceptance-in-standalone-game)


The `m52` scenario checks widget output and lookup identity, invalid open outputs, collapse state on horizontal and vertical edges, event counts, restoration of docked preferences after transfer, and null lookup after close. The [M5.2 Editor test guide](WindowingPlugin_M5_2_Editor_Test_Guide.md) covers visible sizing, repaint, Settings, Rename, and native maximize.

The M5.1 and M5.2 scenarios passed in UE 5.8 Win64 Development Editor Standalone against the M5.2 source. Packaged M5 runs remain part of the final regression gate.
