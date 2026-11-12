# M2 implementation guide — window placement and display recovery

M2 restores a window's size and location when its host opens the same panel again. It builds on the current M0/M1 open and close API. The host still decides when to open each panel and supplies its identity. Keep persistence in the plugin and keep widget contents and business state in the host.

## 1. Define stable identity

Append these Blueprint properties to `FWpWindowCreateOptions`. The defaults preserve existing callers. Treat both components as a pair in storage and in duplicate checks.

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windowing")
FString PlacementProfile = TEXT("Default");

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windowing")
FString PlacementKey;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windowing")
bool bRestorePlacement = true;
```

An empty key opts out of placement persistence. A key enables saving the normal bounds and optional maximized state. `bRestorePlacement = false` opens with current defaults and still allows later saves for the key. Use case-sensitive keys and profiles, limit each component to a documented length, and reject invalid or whitespace-only components when a key is supplied. Store keys as JSON property values so they never become file paths. A duplicate active key within a profile returns a dedicated result while its original record is creating, open, or closing. Append new result codes at the end of the existing enum. Test two different profiles with the same key and choose distinct profiles for simultaneously active sessions that need independent layouts.

Keep runtime handles transient. The registry keeps the placement identity with the record and releases its reservation on rollback or retirement. A retired runtime handle never resolves a saved placement.

For a map key, compare each component with `FString::Equals(..., ESearchCase::CaseSensitive)`. A dedicated identity struct avoids collisions such as profile `A.B` with key `C` versus profile `A` with key `B.C`.

## 2. Establish the coordinate contract

Store normal outer window bounds in Windows desktop pixels. Desktop coordinates are signed, and displays to the left or above the primary can have negative coordinates. Store the source work area, an advisory monitor ID, and the source window DPI alongside those bounds. Capture `GetWindowRect` in a private Win64 adapter. Verify its coordinate space matches `FDisplayMetrics` under the packaged process's DPI awareness. Use `GetDpiForWindow` after a real window exists and a monitor DPI sample for the target. Keep Unreal and Win32 types out of the JSON schema.

Use a normal-window snapshot while the HWND still exists. Capture it when the window is in the normal state. While minimized or maximized, preserve the last good normal snapshot and update the maximized flag separately. For an initially maximized restored window, obtain the normal snapshot after applying its normal bounds and before maximizing. A minimized window reopens normal unless the last intended state was maximized. Never replace the normal rectangle with minimized or maximized bounds.

Choose an explicit scaling policy. Preserve the logical outer size by multiplying pixel dimensions by `TargetDpi / SavedDpi`. Preserve relative position within the usable range of the source work area, then project it into the target work area. If a window previously occupied the right edge, this keeps it on the right edge even when resolution changes. On the same display with unchanged work area and DPI, this maps the original position back to itself. If the identified display is gone, center in the fallback work area. Round once after the final clamp.

## 3. Implement pure placement math

Add a private resolver that takes plain rectangles, source and target DPI, and a native minimum outer size. The monitor adapter provides work areas using `FDisplayMetrics::RebuildDisplayMetrics` and `FMonitorInfo::WorkArea`. Keep all rectangles in the same coordinate space.

1. Filter monitors with invalid work areas. Select the saved monitor ID when it has an unambiguous match. Otherwise choose the current work area with the largest positive intersection with the old bounds, then the primary, then the first valid work area. If none is valid, fall back to M0 default creation.
2. Validate the saved geometry before computing with it. Require finite coordinates, positive width and height, a valid source work area, and a sane positive DPI. Bound the magnitudes accepted from disk before rounding to integers.
3. Scale the outer size once using the DPI ratio. Clamp it to the work area and account for native chrome when converting the requested minimum client size to an outer minimum. If the minimum cannot fit, prioritize a reachable title bar and record the constraint in the resolver result. Verify Windows' actual minimum tracking behavior after applying bounds.
4. Project the position using the following per-axis helper. Center when a display was replaced. Clamp the result so the outer rectangle fits whenever the native minimum permits it.

```cpp
static double RestoreAxis(double OldStart, double OldOrigin, double OldWorkSize,
    double OldWindowSize, double NewOrigin, double NewWorkSize,
    double NewWindowSize, bool bCenter)
{
    const double OldRange = FMath::Max(0.0, OldWorkSize - OldWindowSize);
    const double NewRange = FMath::Max(0.0, NewWorkSize - NewWindowSize);
    const double Fraction = bCenter || OldRange == 0.0
        ? 0.5
        : FMath::Clamp((OldStart - OldOrigin) / OldRange, 0.0, 1.0);
    return NewOrigin + Fraction * NewRange;
}
```

Unit-test this resolver without Slate. Cover negative coordinates, a taskbar on the left, a removed display, lower resolution, malformed numbers, oversized minimums, and 96→144→96 DPI round trips. Include a case with two monitors of the same friendly name so monitor identity stays advisory.

## 4. Add versioned local storage

Use one file under `FPaths::ProjectSavedDir()/WindowingPlugin/Placement.json`. Add `Json` as a private dependency. Load lazily before the first keyed open. Store profile and key together with the snapshot. A schema version gates decoding.

```json
{
  "schemaVersion": 1,
  "placements": [
    {
      "profile": "Default",
      "key": "Inspector",
      "normal": {"left": -1150, "top": 140, "width": 780, "height": 620},
      "workArea": {"left": -1920, "top": 0, "width": 1920, "height": 1040},
      "monitorId": "advisory-monitor-id",
      "dpi": 96,
      "maximized": false
    }
  ]
}
```

Ignore malformed entries individually. For malformed JSON, report a read error and use defaults. Preserve a file with a newer schema version and disable writes for that run. Serialize to a sibling temporary file, close it, and replace the destination using the platform file replacement path. If replacement fails, retain the last good destination and keep the changes dirty for a retry. Serialize writes through one module-owned service so two sessions cannot overwrite unrelated entries. Unit-test decoding, version handling, and failed writes using a temporary test directory.

If multiple app processes use the same saved directory, acquire a lock keyed by the absolute destination path for the read, merge, and replace operation. Merge only dirty identities into the latest supported file. This preserves unrelated entries written by another process. A lock timeout leaves changes dirty for the next flush. Add a two-writer test with different keys.

## 5. Integrate with window lifetime

1. In the lifetime service, validate and reserve the key before widget creation. Read the saved entry and resolve it against current display metrics. Leave the existing M0 create path in place when the key has no usable placement.
2. Register the Slate window hidden. The private Windows adapter applies the resolved outer rectangle through its HWND before show. Use `SetWindowPos` without activating the window or changing its Z order. Check the actual native rectangle and target DPI after the move. Do not pass outer pixel sizes into Slate's initial client-size arguments or apply both Slate's initial DPI scale and your restore scale to the same rectangle.
3. Capture the resolved normal bounds, then show. If the snapshot asked for maximization, apply it after setting the normal bounds. A placement failure should log a diagnostic and use the default M0 window when it is still valid. Preserve the M0 registration, rollback, and event ordering.
4. Sample placement after layout changes. For a small M2 window count, a periodic sample on the existing Slate post-tick hook is sufficient. Compare against the last saved snapshot, coalesce changes, and write after a short quiet period. A pre-close callback may take a final native snapshot while the HWND is valid. Keep file writes and UObject/Slate release outside that callback stack.
5. Cache the last valid normal bounds through minimize and maximize. Flush dirty keyed snapshots after normal retirement and during the terminal module drain. Never save a failed creation or let one bad geometry sample replace a valid entry.
6. For live monitor changes, debounce a display-metrics notification or detect work-area changes in the periodic sample. Re-resolve open keyed windows that are fully off-screen or have an unreachable title bar. Avoid continually moving windows during drag or ordinary resize. Remove any display-metrics delegate during module shutdown.

Keep the native calls in the Win64 implementation file. Use `Windows/WindowsHWrapper.h` and declare `user32.lib` in the module's Win64 build rules if needed. The host can obtain the HWND from `SWindow::GetNativeWindow()->GetOSWindowHandle()` after checking both pointers. A checked placement application can use this shape.

```cpp
static bool ApplyNormalBounds(HWND Window, const FIntRect& Bounds)
{
    if (!Window || Bounds.Width() <= 0 || Bounds.Height() <= 0)
    {
        return false;
    }
    return ::SetWindowPos(Window, nullptr, Bounds.Min.X, Bounds.Min.Y,
        Bounds.Width(), Bounds.Height(), SWP_NOACTIVATE | SWP_NOZORDER) != 0;
}
```

Call the helper with the validated, rounded resolver output. Re-read the actual rectangle with `GetWindowRect` and DPI with `GetDpiForWindow`. Let Windows process DPI change messages normally, then sample the settled rectangle on a later post-tick before deciding whether another adjustment is necessary.

## 6. Verify M2 in order

| Gate | Automated check |
|---|---|
| Pure geometry | A table of monitor layouts verifies monitor choice, signed positions, work-area clamp, fallback center, and one DPI conversion. |
| Storage | Round-trip two profiles, verify corrupt entry isolation, preserve a future-schema file, and test a failed replacement. |
| Same-display restore | Run the packaged sample twice with the same saved directory. Move and resize the keyed window in run one. In run two compare the actual normal outer rectangle with the saved rectangle within rounding tolerance. |
| Missing monitor | Seed a placement pointing to an absent monitor and verify the real window opens inside a current work area. Use synthetic display metrics for deterministic geometry coverage. |
| Window modes | Move and resize a normal window, maximize, close, and reopen. Check the maximized flag and restore to the saved normal bounds. Repeat for minimize. |
| API compatibility | Existing unkeyed M1 tests still pass. Different keys restore independently. A failed open leaves no reservation or saved placement. |
| Package validation | Run the placement suite in packaged Win64 Development and Shipping. Add a desktop with mixed DPI or changing monitor topology for native integration coverage when available. |

Build order is resolver and tests, storage and tests, native capture and restore, lifecycle integration, then packaged acceptance. M2 ends when a keyed window restores safely across launches and display changes, with the M1 lifecycle suite still passing.
