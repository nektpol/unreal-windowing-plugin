# M1 lifecycle and multi-window plan

M1 hardens the M0 API for at least three simultaneous native UMG windows in one Win64 session. Keep the current Blueprint and C++ API and handle semantics. Use the clean UE 5.8 sample from [M0 acceptance testing](WindowingPlugin_M0_Step_6_Test_Guide.md).

1. **Extend the sample to three slots.** Add Open, Rename, Close, and State controls for C. Record each slot's GUID and the events it receives. Keep the single-window self-close case.
2. **Exercise reentrant event handlers.** Close A from B's opened listener. Request close of a different live window from a closed listener. Reopen a slot after its closed event finishes. Verify event counts, close reasons, and handle states.
3. **Exercise teardown with three live records.** Close them in different orders through the API and title-bar X. Force GC with windows open and while one is closing. Quit with multiple windows open, including a case with finalization already pending. Repeat the GameInstance isolation and creation rollback cases.
4. **Stress repeated use.** Run 100 open and close cycles in packaged Development. Check that the registry returns to zero after the final drain. Inspect logs and the debugger for stale records, GC warnings, leaked windows, and duplicate callbacks. Add targeted regression coverage for lifecycle failures found.
5. **Record package results.** Run the three-window interaction and shutdown cases in packaged Win64 Development and Shipping. Record the engine build, sample revision, configuration, case, and outcome.

| Case | Pass condition |
|---|---|
| Three windows | Each has its own handle, title, and widget state. Mouse and keyboard input continue to work after another window closes. |
| Mixed close order | Each published handle receives one closed event during normal operation. First accepted close reason wins. Retired handles resolve to `Invalid` for queries and `NotFound` for mutation calls. |
| Reentry and rollback | Event listeners can close another live window. Failed creation emits no lifecycle events and leaves no record after drain. |
| GC and repeated use | Open widgets stay interactive. Closing entries stay tracked until teardown. The registry returns to zero after 100 cycles. |
| Session and process shutdown | Session teardown releases its own windows. Application exit releases all managed windows without callbacks into a shutting-down subsystem or unloaded module. |

M2 covers window placement persistence and display recovery. M3 covers activation and focus reporting. Docking and existing-widget attachment remain separate extensions.
