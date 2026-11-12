# M0 sample and acceptance tests

Use the implemented plugin described in the [M0 implementation notes](WindowingPlugin_M0.md). Build a Blueprint control panel in the main game window and open a second widget in its own Windows window. The control panel shows handles, results, and event counts in both Development and Shipping builds. All harness assets go in the sample project.

### 6.1 Prepare the sample project

1. Create a **Games → Blank → C++** project in UE 5.8. Use a separate project folder, for example `C:\Unreal Projects\WindowingM0Sample`.
2. With the sample editor closed, copy `WindowingPlugin.uplugin`, `Source`, and `Resources` into the sample's `Plugins/WindowingPlugin` folder. Build the sample's **Development Editor | Win64** target. If prompted, enable **WindowingPlugin** in **Edit → Plugins** and restart the editor.
3. Check that the copied `WpWindowRuntimeEntry.h` uses `TObjectPtr<UWpWindowRecord>` for `Record`. The sample build should compile the plugin without the incremental GC warning on `AddReferencedObject`.
4. Create a folder named `Content/WindowingM0`. Save a basic level as `L_WindowingM0`. In **Project Settings → Maps & Modes**, set both **Editor Startup Map** and **Game Default Map** to that level.

| Asset | Type | Purpose |
|---|---|---|
| `L_WindowingM0` | Level | Hosts the control panel and gives you a visible main game window |
| `WBP_WindowingControl` | Widget Blueprint | Opens, renames, queries, and closes test windows |
| `WBP_WindowingSmokeTest` | Widget Blueprint | Tests mouse, keyboard, resize, and close from inside a window |
| `BP_WindowingM0GameInstance` | Blueprint derived from GameInstance | Stores a handle for the single-window self-close test |

Create the GameInstance Blueprint. Give it a variable named `SelfCloseHandle` of type **Wp Window Handle**. Assign the Blueprint as the **Game Instance Class** in **Maps & Modes**. Compile it before wiring the widgets.

### 6.2 Build the widget that goes in the native window

Create `WBP_WindowingSmokeTest`. In the Designer, put a Vertical Box in a Border with some padding. Give it these children.

| Control | Name | Setup |
|---|---|---|
| Text Block | `TxtCounter` | Set initial text to `Clicks: 0` and enable **Is Variable** |
| Button with Text Block | `BtnIncrement` | Label it **Increment** |
| Editable Text Box | `InputTest` | Set the hint to **Type here**. Use a slot that can expand horizontally |
| Button with Text Block | `BtnCloseThis` | Label it **Close this window (single-window test)** |

In the Graph, add an Integer variable `Clicks` with default value 0.

```text
BtnIncrement → On Clicked
  → Set Clicks = Clicks + 1
  → Format Text "Clicks: {Count}" with Count = Clicks
  → Set Text on TxtCounter
```

Wire the close button to **Get Game Instance → Cast To BP_WindowingM0GameInstance → Get SelfCloseHandle → Request Close Window**. The target of **Request Close Window** is **Get Wp Windowing Subsystem**. With **Context Sensitive** enabled, search for the subsystem by class name. Depending on the graph context, the node can appear as a typed **Get Game Instance Subsystem** node. Confirm its output type is `WpWindowingSubsystem`.

Before requesting close, branch on **Is Window Open** for `SelfCloseHandle`. Run this button test with just one window opened as Slot A. Slot B is for the two-window test. The GameInstance variable is a test fixture for handing a known handle to the widget. Keep the native test widget out of **Add to Viewport**. Its window is created by the plugin.

### 6.3 Build the control panel

Create `WBP_WindowingControl`. Put its controls in a Vertical Box in a Border anchored near the top left. Make the Text Blocks below variables so the graph can call **Set Text** on them.

- Buttons labeled **Open A**, **Open B**, **Rename A**, **Close A**, **Close B**, **Close A Twice**, **Refresh State**, and **Quit**.
- A Check Box labeled **Close during opened event**.
- A Text Block for the last operation and result. Include the result code and message.
- A Text Block for the state of A and B.
- A Text Block for opened count, closed count, and the last close reason. Show the event GUID if you want to match events to slots.

Add these variables in the Graph.

| Variable | Type | Default |
|---|---|---|
| `Windowing` | Wp Windowing Subsystem object reference | None |
| `HandleA`, `HandleB` | Wp Window Handle | Default struct |
| `OpenedCount`, `ClosedCount` | Integer | 0 |
| `CloseDuringOpened` | Boolean | False |

Use **Event On Initialized** to get the subsystem, store it in `Windowing`, and bind **On Window Opened** and **On Window Closed** once. Drag from `Windowing` to find each **Bind Event to** node. Drag from its red **Event** pin and add a matching Custom Event. Connect the Check Box's **On Check State Changed** to set `CloseDuringOpened`.

```text
HandleOpened(Handle)
  → OpenedCount = OpenedCount + 1
  → Refresh event counter text
  → If CloseDuringOpened: Request Close Window using the event's Handle

HandleClosed(Event)
  → Break Wp Window Closed Event
  → ClosedCount = ClosedCount + 1
  → Refresh event counter text with Reason and optional Handle.Id
```

The opened event fires inside **Open Widget Window**, before the button graph receives `OutHandle`. Use the event's own handle for the immediate close test. Keep `HandleA` and `HandleB` after close so you can query or reuse an old handle for the stale-handle test. Replace a slot only after a later successful open.

Wire **Open A** as follows. To avoid losing a handle, allow a slot to open only when **Get Window State** for that slot returns **Invalid**.

```text
Open A → On Clicked
  → Get Window State(Windowing, HandleA) == Invalid
  → Branch on result
     True → Open Widget Window
              Target = Windowing
              Widget Class = WBP_WindowingSmokeTest
              Options = Make Wp Window Create Options
                Title = "M0 Smoke A"
                Initial Client Size = (640, 480)
                Minimum Client Size = (320, 240)
              Owning Player = empty
          → Break Wp Window Result
          → Display Code and Message
          → If Code == Success
              Set HandleA = OutHandle
              Get Game Instance → Cast To BP_WindowingM0GameInstance
              Set SelfCloseHandle = OutHandle
     False → Display "Slot A already has a live or closing window."
```

Repeat for **Open B** with `HandleB` and title `M0 Smoke B`. Only A assigns `SelfCloseHandle`. For **Rename A**, call **Set Window Title** with `HandleA` and `M0 Smoke A Renamed`. Wire **Close A** and **Close B** to **Request Close Window** with the respective handle. Break each returned result and display its code and message. In **Close A Twice**, call **Request Close Window** twice on the same execution chain without a delay. Display both result codes on one line.

For **Refresh State**, call **Get Window State** for each slot and display both enum values. Use **Format Text** with enum-to-string conversions where needed. Wire **Quit** to **Quit Game**, using **Get Owning Player** as the player controller and **Quit** as the quit preference.

### 6.4 Put the control panel in the main window

In `L_WindowingM0`, open the Level Blueprint and wire this graph. Keep the input setup in the sample level.

```text
Event BeginPlay
  → Get Player Controller(0)
  → Create Widget(WBP_WindowingControl, Owning Player = Player Controller)
  → Add to Viewport using that widget
  → Set Input Mode Game and UI
      Player Controller = Player Controller
      In Widget to Focus = WBP_WindowingControl instance
      Mouse Lock Mode = Do Not Lock
      Hide Cursor During Capture = false
  → Set Show Mouse Cursor = true on Player Controller
```

Compile and save all Blueprints and the map. Try one **Standalone Game** run from the editor before packaging. Open A, click **Increment** in its native window, type into the text box, then close via the title-bar X. You should see **Opened = 1**, **Closed = 1**, and reason `UserOrPlatform` in the main control panel. Use **Refresh State** after the close event to see `Invalid` for A.

### 6.5 Package and run the actual Windows executables

1. In **Project Settings → Packaging**, include `L_WindowingM0` in **List of Maps to Include in a Packaged Build**. Confirm it is still the **Game Default Map**. The map and widget class references bring the harness assets into the cook.
2. Choose **Development** for the package's build configuration. Use the editor's **Platforms → Windows** packaging action and output to a dedicated folder such as `C:\Unreal Projects\WindowingM0Packages\Development`. Check that the packaging log reports success.
3. Launch the packaged game's top-level EXE from that output. Use a windowed launch if the host starts fullscreen. For example, from the output directory run `& .\WindowingM0Sample.exe -windowed -ResX=1280 -ResY=720 -log` in PowerShell, substituting the actual executable name. Run the cases below. Record what you observe in the main control panel.
4. Package again with **Shipping** selected and choose a separate output folder. Launch that EXE and repeat the packaged runtime cases. Use the on-screen counters and result text because Shipping logs may be unavailable.

### 6.6 Run the packaged runtime cases

Start each row with the previous windows closed. Compare changes in the event counts rather than absolute values, or restart the EXE between rows.

| Test | Actions | Pass condition |
|---|---|---|
| Open and interact | Open A. Move it, resize it, click **Increment** three times, and type in the text box. Click back into the main game window. | A is a separate native window. It shows three clicks and the entered text. The main window accepts interaction. |
| Title and independent windows | Open A and B, then **Rename A**. Close B and then A. | Only A's title changes. Each window has its own widget state and closes independently. Two opened events and two closed events. |
| API close and duplicate request | Open A, then **Close A Twice**. After the event, refresh state and press **Close A** again. | `Success`, then `AlreadyClosing`. One closed event with `HostRequest`. After retirement, state is `Invalid` and close returns `NotFound`. |
| Title-bar close and reopen | Open A, close via its X, and reopen A. Record both opened GUIDs if displayed. | The X produces one `UserOrPlatform` event. Reopen succeeds with a fresh GUID. |
| Fresh widget on reopen | Open A, increment the counter, type some text, close A, and reopen it. | A starts at `Clicks: 0` with an empty text box. The main panel retains its counts and records the new open. |
| Close during the opened event | Check **Close during opened event**, then open A. Uncheck it after the test. | Open returns `Success`, the listener's close is accepted, and A finishes closing with one opened event and one `HostRequest` event. The native window may only flash briefly. |
| Close from the native widget | Open only A and click **Close this window** inside A. | A closes with one `HostRequest` event. The main control panel stays usable. |
| Normal exit with windows open | Open A and B. Quit from the main panel. Repeat in a fresh launch by closing the main game window via its X. | The process and both native windows exit without a crash or lingering window. An exit can suppress closed events. |
| Exit with finalization pending | In a temporary Development graph, call **Request Close Window** for A and **Quit Game** on the same execution chain. Repeat with B still open. | The process exits cleanly even though A's logical finalization may still be pending. |

### 6.7 Run targeted checks in the Development sample

- **Invalid inputs.** Duplicate the Open node in a test event. Leave **Widget Class** unset to expect `InvalidWidgetClass`. With a valid class, set initial width to 0 or minimum width greater than initial width to expect `InvalidSize`. Break `OutHandle` and check that its GUID is invalid. Counts should stay unchanged. For the stale-handle case, use a saved handle after its window closes and retires.
- **Force GC.** For a deterministic check, use a temporary sample C++ Blueprint callable helper that calls `CollectGarbage(RF_NoFlags)` on the game thread. Include `UObject/UObjectGlobals.h`. Trigger it in a separate button while A is open, then interact with A again. In a second test, chain it immediately after a successful **Request Close Window** in the same button graph so it runs before the next Slate post-tick. Use a debugger to confirm the closing entry survives the collection and retires on the drain. The ordinary Blueprint **Collect Garbage** node schedules collection and is useful for the open-window check when precise timing is unnecessary.
- **Creation rollback.** In the disposable sample copy of `WpWindowLifetimeService.cpp`, add a temporary `return Fail(EWpWindowResultCode::NativeCreationFailed, TEXT("Injected test failure"));` immediately after the post-`TakeWidget()` session check and before host construction. Confirm the output GUID is invalid, counts stay unchanged, and the entry retires at drain. For the second test, temporarily split the combined registration/show condition. Handle an actual registration failure as before, then inject the same failure immediately after successful `RegisterWindow()`, before `ShowWindow()`. Confirm the hidden native window is removed and the entry retires. Remove the injections and rebuild any final packages created from that copy.
- **GameInstance teardown.** In editor Play settings, use **New Editor Window (PIE)** and stop play with a managed window open. Check that the managed window closes and that no late close event reaches the deinitializing subsystem. Use a debugger breakpoint on `BeginSessionShutdown` and the delegate broadcast if the UI disappears before you can observe it.
- **Cross-session isolation.** Use multi-client PIE in a single process with two local GameInstances. Open A from the first session. In a debugger or temporary editor-only harness, pass A's handle to the second session's subsystem and call query, rename, and close. Expect `Invalid` for query and `NotFound` for rename and close. Repeat the open call on the second subsystem with the first session's PlayerController to expect `InvalidOwningContext`. End the first session independently if your fixture supports it and verify the second session's window remains usable. Stopping the entire PIE run checks teardown of both sessions. Record the isolation check as pending if your fixture only has one GameInstance.
- **Repeat and inspect.** In Development, repeat open and close for 20 cycles. Observe `FWpWindowLifetimeService::Entries` in the debugger or add temporary private logging in the sample. After each drain the entry count should return to zero. Check the log for Slate destruction warnings, GC warnings, or shutdown assertions.
- **Unavailable environment.** If you have a headless sample run that can still invoke the open call, such as a Windows game test using `-nullrhi`, expect `SlateUnavailable`. On a compiled non-Windows test target, expect `UnsupportedPlatform`. Record the environments you actually exercised.

Record the engine build, sample revision, configuration, test case, result code, opened/closed count changes, close reason, and any log warnings. Treat a Development smoke pass as the first checkpoint. Record the Shipping and targeted Development checks separately.

If **Open Widget Window** is missing, check the sample plugin is enabled and that the Blueprint has a typed `WpWindowingSubsystem` reference. If the EXE loads the wrong level, verify the Game Default Map and cooked map list. If a close event is missing, inspect whether the record reached `Closing` and whether Slate has removed the window from its top-level list and cleared its native handle. An editor-only success still needs a packaged run.
