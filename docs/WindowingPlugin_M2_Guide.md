# M2 implementation guide — window placement and display recovery

M2 restores a window's size and location when its host opens the same panel again. It builds on the current M0/M1 open and close API. The host still decides when to open each panel and supplies its identity. Keep persistence in the plugin and keep widget contents and business state in the host.

For paste-ready C++, use [the placement functions and integration blocks](#7-copy-ready-placement-functions). Sections 1–6 explain the contract and acceptance tests.

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
4. Use `RestoreAxis` from section 7 to project the position. Center when a display was replaced. Clamp the result so the outer rectangle fits whenever the native minimum permits it.

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

Use the native adapter in section 7 with the validated, rounded resolver output. It lets Windows process DPI change messages normally. Sample the settled rectangle on a later post-tick before deciding whether another adjustment is necessary.

## 6. Verify M2 in order

The UE 5.8 Win64 Development Editor build passed. The `WindowingPlugin.M2.Geometry` and `WindowingPlugin.M2.Storage` editor tests passed, and the live window placement check was confirmed by the user. Packaged M2 runs and physical monitor topology changes have no recorded results here.

### Run the editor tests

In the Unreal Editor console, run `Automation RunTests WindowingPlugin.M2`. You can also find **WindowingPlugin → M2** in the Session Frontend **Automation** tab. **Geometry** checks monitor fallback, DPI scaling, and title bar reachability. **Storage** checks the JSON round trip, merging profiles, and preserving a file written by a newer schema.

### Check a real window in the editor

Use the existing **WBP_WindowingControl** from the [Blueprint sample](WindowingPlugin_M0_Step_6_Test_Guide.md). In its **Open A** graph, refresh **Make Wp Window Create Options** if the new pins are missing. Set **Placement Profile** to `M2EditorTest`, **Placement Key** to `WindowA`, and **Restore Placement** to true. Compile the Blueprint.

1. Choose **Standalone Game** from the editor Play menu and click **Open A**. Move the native window away from its original position and resize it.
2. Close A with its title-bar X, then click **Open A** again. A should reopen at the saved position and size. Its widget counter and input should start fresh.
3. Maximize A and close it with the title-bar X. Stop Standalone and launch it again. Click **Open A**. It should reopen maximized. Click **Restore Down** and check that the moved and resized normal bounds return.
4. Inspect `Saved/WindowingPlugin/Placement.json` under the project directory. Find the row for profile `M2EditorTest` and key `WindowA`. It should contain the normal bounds, source work area, DPI, and maximized state.

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

## 7. Copy-ready placement functions

Add the two files below under `Source/WindowingPlugin/Private`. Add `"Json"` to the module's private dependencies and `PublicSystemLibraries.Add("user32.lib");` inside a `Target.Platform == UnrealTargetPlatform.Win64` guard. These functions use the options from section 1 and the schema from section 4.

The functions and integration compiled and linked in the M1 sample's UE 5.8.4 Win64 Development Editor target. The M2 runtime acceptance cases are in section 6.

### WpPlacement.h

```cpp
#pragma once

#include "CoreMinimal.h"

struct FWpPlacementIdentity
{
    FString Profile;
    FString Key;

    bool operator==(const FWpPlacementIdentity& Other) const
    {
        return Profile.Equals(Other.Profile, ESearchCase::CaseSensitive) &&
            Key.Equals(Other.Key, ESearchCase::CaseSensitive);
    }

    friend uint32 GetTypeHash(const FWpPlacementIdentity& Value)
    {
        return HashCombine(GetTypeHash(Value.Profile), GetTypeHash(Value.Key));
    }
};

struct FWpPlacementSnapshot
{
    FIntRect Normal = FIntRect(0, 0, 0, 0);
    FIntRect WorkArea = FIntRect(0, 0, 0, 0);
    FString MonitorId;
    int32 Dpi = 96;
    bool bMaximized = false;

    bool operator==(const FWpPlacementSnapshot& Other) const
    {
        return Normal == Other.Normal && WorkArea == Other.WorkArea &&
            MonitorId == Other.MonitorId && Dpi == Other.Dpi &&
            bMaximized == Other.bMaximized;
    }
};

struct FWpPlacementMonitor
{
    FIntRect WorkArea;
    FString Id;
    int32 Dpi;
    bool bPrimary;
};

using FWpPlacementMap = TMap<FWpPlacementIdentity, FWpPlacementSnapshot>;

namespace WpPlacement
{
    bool IsValidIdentity(const FWpPlacementIdentity& Identity);
    int32 SelectMonitor(const FWpPlacementSnapshot& Saved,
        TConstArrayView<FWpPlacementMonitor> Monitors);
    TOptional<FIntRect> ResolveBounds(const FWpPlacementSnapshot& Saved,
        const FWpPlacementMonitor& Monitor, FIntPoint MinimumOuterSize);
    TArray<FWpPlacementMonitor> ReadMonitors();
    bool IsNormal(void* NativeHandle);
    bool Capture(void* NativeHandle, FWpPlacementSnapshot& InOut);
    bool Restore(void* NativeHandle, const FWpPlacementSnapshot& Saved,
        FVector2D MinimumClientSize);
    bool HasReachableTitleBar(const FIntRect& Bounds,
        TConstArrayView<FWpPlacementMonitor> Monitors);

    enum class ELoadResult { Loaded, Missing, Invalid, UnsupportedVersion, IoError };
    ELoadResult Load(const FString& Path, FWpPlacementMap& Out);
    bool MergeAndSave(const FString& Path, const FWpPlacementMap& Dirty);
    FString DefaultPath();
}
```

### WpPlacement.cpp — identity, monitor selection, and geometry

Start the file with this block. Add the next two blocks to the same file.

```cpp
#include "WpPlacement.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
    constexpr int32 CoordinateLimit = 1000000;
    constexpr int32 SchemaVersion = 1;

    bool ValidRect(const FIntRect& Rect)
    {
        return Rect.Min.X >= -CoordinateLimit && Rect.Min.Y >= -CoordinateLimit &&
            Rect.Max.X <= CoordinateLimit && Rect.Max.Y <= CoordinateLimit &&
            Rect.Min.X < Rect.Max.X && Rect.Min.Y < Rect.Max.Y;
    }

    bool ValidDpi(int32 Dpi)
    {
        return Dpi >= 48 && Dpi <= 768;
    }

    bool ValidSnapshot(const FWpPlacementSnapshot& Saved)
    {
        return ValidRect(Saved.Normal) && ValidRect(Saved.WorkArea) && ValidDpi(Saved.Dpi);
    }

    int64 Overlap(const FIntRect& A, const FIntRect& B)
    {
        const int32 Width = FMath::Max(0, FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X));
        const int32 Height = FMath::Max(0, FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y));
        return static_cast<int64>(Width) * Height;
    }

    int32 RestoreAxis(int32 OldStart, int32 OldOrigin, int32 OldWork, int32 OldSize,
        int32 NewOrigin, int32 NewWork, int32 NewSize, bool bCenter)
    {
        const int32 OldRange = FMath::Max(0, OldWork - OldSize);
        const int32 NewRange = FMath::Max(0, NewWork - NewSize);
        const double Fraction = bCenter || OldRange == 0
            ? 0.5
            : FMath::Clamp(static_cast<double>(OldStart - OldOrigin) / OldRange, 0.0, 1.0);
    return NewOrigin + FMath::RoundToInt32(Fraction * NewRange);
    }
}

bool WpPlacement::IsValidIdentity(const FWpPlacementIdentity& Identity)
{
    const auto ValidPart = [](const FString& Part)
    {
        return Part.Len() <= 128 && !Part.TrimStartAndEnd().IsEmpty();
    };
    return ValidPart(Identity.Profile) && ValidPart(Identity.Key);
}

int32 WpPlacement::SelectMonitor(const FWpPlacementSnapshot& Saved,
    TConstArrayView<FWpPlacementMonitor> Monitors)
{
    int32 First = INDEX_NONE;
    int32 Primary = INDEX_NONE;
    int32 MatchingId = INDEX_NONE;
    int32 MatchCount = 0;
    int32 BestOverlap = INDEX_NONE;
    int64 BestArea = 0;

    for (int32 Index = 0; Index < Monitors.Num(); ++Index)
    {
        const FWpPlacementMonitor& Monitor = Monitors[Index];
        if (!ValidRect(Monitor.WorkArea) || !ValidDpi(Monitor.Dpi))
        {
            continue;
        }
        if (First == INDEX_NONE)
        {
            First = Index;
        }
        if (Monitor.bPrimary)
        {
            Primary = Index;
        }
        if (!Saved.MonitorId.IsEmpty() && Saved.MonitorId == Monitor.Id)
        {
            MatchingId = Index;
            ++MatchCount;
        }
        const int64 Area = Overlap(Saved.Normal, Monitor.WorkArea);
        if (Area > BestArea)
        {
            BestOverlap = Index;
            BestArea = Area;
        }
    }

    if (MatchCount == 1)
    {
        return MatchingId;
    }
    if (BestOverlap != INDEX_NONE)
    {
        return BestOverlap;
    }
    return Primary != INDEX_NONE ? Primary : First;
}

TOptional<FIntRect> WpPlacement::ResolveBounds(const FWpPlacementSnapshot& Saved,
    const FWpPlacementMonitor& Monitor, FIntPoint MinimumOuterSize)
{
    if (!ValidSnapshot(Saved) || !ValidRect(Monitor.WorkArea) || !ValidDpi(Monitor.Dpi) ||
        MinimumOuterSize.X <= 0 || MinimumOuterSize.Y <= 0 ||
        MinimumOuterSize.X > CoordinateLimit || MinimumOuterSize.Y > CoordinateLimit)
    {
        return {};
    }

    const double Scale = static_cast<double>(Monitor.Dpi) / Saved.Dpi;
    const FIntRect& Work = Monitor.WorkArea;
    const auto RestoreSize = [Scale](int32 Old, int32 Minimum, int32 Available)
    {
        return FMath::Clamp(FMath::RoundToInt32(Old * Scale), Minimum, FMath::Max(Minimum, Available));
    };
    const int32 Width = RestoreSize(Saved.Normal.Width(), MinimumOuterSize.X, Work.Width());
    const int32 Height = RestoreSize(Saved.Normal.Height(), MinimumOuterSize.Y, Work.Height());
    const bool bCenter = Saved.MonitorId.IsEmpty()
        ? Overlap(Saved.WorkArea, Work) == 0
        : Saved.MonitorId != Monitor.Id;
    const int32 Left = RestoreAxis(Saved.Normal.Min.X, Saved.WorkArea.Min.X, Saved.WorkArea.Width(),
        Saved.Normal.Width(), Work.Min.X, Work.Width(), Width, bCenter);
    const int32 Top = RestoreAxis(Saved.Normal.Min.Y, Saved.WorkArea.Min.Y, Saved.WorkArea.Height(),
        Saved.Normal.Height(), Work.Min.Y, Work.Height(), Height, bCenter);
    return FIntRect(Left, Top, Left + Width, Top + Height);
}

bool WpPlacement::HasReachableTitleBar(const FIntRect& Bounds,
    TConstArrayView<FWpPlacementMonitor> Monitors)
{
    if (!ValidRect(Bounds))
    {
        return false;
    }
    for (const FWpPlacementMonitor& Monitor : Monitors)
    {
        const FIntRect& Work = Monitor.WorkArea;
        const int32 VisibleWidth = FMath::Min(Work.Max.X, Bounds.Max.X) -
            FMath::Max(Work.Min.X, Bounds.Min.X);
        if (ValidRect(Work) && Bounds.Min.Y >= Work.Min.Y &&
            Bounds.Min.Y + 32 <= Work.Max.Y && VisibleWidth >= FMath::Min(120, Bounds.Width()))
        {
            return true;
        }
    }
    return false;
}
```

### WpPlacement.cpp — Win64 capture and restore

```cpp
TArray<FWpPlacementMonitor> WpPlacement::ReadMonitors()
{
    FDisplayMetrics Metrics;
    FDisplayMetrics::RebuildDisplayMetrics(Metrics);
    TArray<FWpPlacementMonitor> Result;
    for (const FMonitorInfo& Source : Metrics.MonitorInfo)
    {
        const auto& Area = Source.WorkArea;
        const FIntRect Work(Area.Left, Area.Top, Area.Right, Area.Bottom);
        if (ValidRect(Work))
        {
            const float Scale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(
                static_cast<float>(Work.Min.X + Work.Width() / 2),
                static_cast<float>(Work.Min.Y + Work.Height() / 2));
            const int32 Dpi = FMath::IsFinite(Scale) && Scale >= 0.5f && Scale <= 8.0f
                ? FMath::RoundToInt(96.0f * Scale) : 96;
            Result.Add({Work, Source.ID, Dpi, Source.bIsPrimary});
        }
    }
    return Result;
}

bool WpPlacement::IsNormal(void* NativeHandle)
{
#if PLATFORM_WINDOWS
    HWND Window = static_cast<HWND>(NativeHandle);
    return Window && ::IsWindow(Window) && !::IsIconic(Window) && !::IsZoomed(Window);
#else
    return false;
#endif
}

bool WpPlacement::Capture(void* NativeHandle, FWpPlacementSnapshot& InOut)
{
#if PLATFORM_WINDOWS
    HWND Window = static_cast<HWND>(NativeHandle);
    if (!Window)
    {
        return false;
    }
    if (::IsIconic(Window) || ::IsZoomed(Window))
    {
        if (!ValidSnapshot(InOut))
        {
            return false;
        }
        WINDOWPLACEMENT State{};
        State.length = sizeof(State);
        InOut.bMaximized = ::IsZoomed(Window) ||
            (::GetWindowPlacement(Window, &State) && (State.flags & WPF_RESTORETOMAXIMIZED));
        return true;
    }

    RECT Native{};
    MONITORINFO Info{};
    Info.cbSize = sizeof(Info);
    const HMONITOR Monitor = ::MonitorFromWindow(Window, MONITOR_DEFAULTTONEAREST);
    if (!::GetWindowRect(Window, &Native) || !Monitor || !::GetMonitorInfoW(Monitor, &Info))
    {
        return false;
    }

    FWpPlacementSnapshot Candidate;
    Candidate.Normal = FIntRect(Native.left, Native.top, Native.right, Native.bottom);
    Candidate.WorkArea = FIntRect(Info.rcWork.left, Info.rcWork.top, Info.rcWork.right, Info.rcWork.bottom);
    Candidate.Dpi = static_cast<int32>(::GetDpiForWindow(Window));
    if (!ValidSnapshot(Candidate))
    {
        return false;
    }
    for (const FWpPlacementMonitor& Display : ReadMonitors())
    {
        if (Display.WorkArea == Candidate.WorkArea)
        {
            Candidate.MonitorId = Display.Id;
            break;
        }
    }
    InOut = MoveTemp(Candidate);
    return true;
#else
    return false;
#endif
}

bool WpPlacement::Restore(void* NativeHandle, const FWpPlacementSnapshot& Saved,
    FVector2D MinimumClientSize)
{
#if PLATFORM_WINDOWS
    HWND Window = static_cast<HWND>(NativeHandle);
    if (!Window || !ValidSnapshot(Saved))
    {
        return false;
    }
    const TArray<FWpPlacementMonitor> Monitors = ReadMonitors();
    const int32 Index = SelectMonitor(Saved, Monitors);
    if (!Monitors.IsValidIndex(Index))
    {
        return false;
    }

    const FWpPlacementMonitor& Monitor = Monitors[Index];
    const double Scale = Monitor.Dpi / 96.0;
    if (!FMath::IsFinite(MinimumClientSize.X) || !FMath::IsFinite(MinimumClientSize.Y) ||
        MinimumClientSize.X < 1 || MinimumClientSize.Y < 1 ||
        MinimumClientSize.X * Scale > CoordinateLimit || MinimumClientSize.Y * Scale > CoordinateLimit)
    {
        return false;
    }

    RECT Minimum{0, 0, FMath::CeilToInt32(MinimumClientSize.X * Scale),
        FMath::CeilToInt32(MinimumClientSize.Y * Scale)};
    if (!::AdjustWindowRectExForDpi(&Minimum, static_cast<DWORD>(::GetWindowLongPtrW(Window, GWL_STYLE)),
        0, static_cast<DWORD>(::GetWindowLongPtrW(Window, GWL_EXSTYLE)), Monitor.Dpi))
    {
        return false;
    }

    const TOptional<FIntRect> Bounds = ResolveBounds(Saved, Monitor,
        FIntPoint(Minimum.right - Minimum.left, Minimum.bottom - Minimum.top));
    return Bounds.IsSet() && ::SetWindowPos(Window, nullptr, Bounds->Min.X, Bounds->Min.Y,
        Bounds->Width(), Bounds->Height(), SWP_NOACTIVATE | SWP_NOZORDER) != 0;
#else
    return false;
#endif
}
```

### WpPlacement.cpp — JSON load and atomic merge

```cpp
namespace
{
    bool ReadRect(const FJsonObject& Object, const TCHAR* Field, FIntRect& Out)
    {
        const TSharedPtr<FJsonObject>* Value = nullptr;
        double X = 0, Y = 0, W = 0, H = 0;
        const auto IsInt = [](double Number)
        {
            return FMath::IsFinite(Number) && FMath::Abs(Number) <= 2 * CoordinateLimit &&
                Number == FMath::TruncToDouble(Number);
        };
        if (!Object.TryGetObjectField(Field, Value) || !Value || !Value->IsValid() ||
            !(*Value)->TryGetNumberField(TEXT("left"), X) ||
            !(*Value)->TryGetNumberField(TEXT("top"), Y) ||
            !(*Value)->TryGetNumberField(TEXT("width"), W) ||
            !(*Value)->TryGetNumberField(TEXT("height"), H) ||
            !IsInt(X) || !IsInt(Y) || !IsInt(W) || !IsInt(H))
        {
            return false;
        }
        Out = FIntRect(static_cast<int32>(X), static_cast<int32>(Y),
            static_cast<int32>(X + W), static_cast<int32>(Y + H));
        return ValidRect(Out);
    }

    TSharedRef<FJsonObject> WriteRect(const FIntRect& Rect)
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("left"), Rect.Min.X);
        Object->SetNumberField(TEXT("top"), Rect.Min.Y);
        Object->SetNumberField(TEXT("width"), Rect.Width());
        Object->SetNumberField(TEXT("height"), Rect.Height());
        return Object;
    }
}

FString WpPlacement::DefaultPath()
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WindowingPlugin/Placement.json")));
}

WpPlacement::ELoadResult WpPlacement::Load(const FString& Path, FWpPlacementMap& Out)
{
    Out.Reset();
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const int64 Size = Files.FileSize(*Path);
    if (Size < 0)
    {
        return Files.FileExists(*Path) ? ELoadResult::IoError : ELoadResult::Missing;
    }
    if (Size > 4 * 1024 * 1024)
    {
        return ELoadResult::Invalid;
    }

    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path))
    {
        return ELoadResult::IoError;
    }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
    {
        return ELoadResult::Invalid;
    }

    double Version = 0;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"), Version))
    {
        return ELoadResult::Invalid;
    }
    if (Version != SchemaVersion)
    {
        return ELoadResult::UnsupportedVersion;
    }
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Root->TryGetArrayField(TEXT("placements"), Rows) || !Rows)
    {
        return ELoadResult::Invalid;
    }

    for (const TSharedPtr<FJsonValue>& Row : *Rows)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!Row || !Row->TryGetObject(Object) || !Object || !Object->IsValid())
        {
            continue;
        }

        const FJsonObject& Value = *Object->Get();
        FWpPlacementIdentity Id;
        FWpPlacementSnapshot Saved;
        double Dpi = 0;
        if (!Value.TryGetStringField(TEXT("profile"), Id.Profile) ||
            !Value.TryGetStringField(TEXT("key"), Id.Key) || !IsValidIdentity(Id) ||
            !ReadRect(Value, TEXT("normal"), Saved.Normal) ||
            !ReadRect(Value, TEXT("workArea"), Saved.WorkArea) ||
            !Value.TryGetNumberField(TEXT("dpi"), Dpi) || !FMath::IsFinite(Dpi) ||
            Dpi < 48 || Dpi > 768 || Dpi != FMath::TruncToDouble(Dpi))
        {
            continue;
        }
        Saved.Dpi = static_cast<int32>(Dpi);
        Value.TryGetStringField(TEXT("monitorId"), Saved.MonitorId);
        Value.TryGetBoolField(TEXT("maximized"), Saved.bMaximized);
        Out.Add(MoveTemp(Id), MoveTemp(Saved));
    }
    return ELoadResult::Loaded;
}

bool WpPlacement::MergeAndSave(const FString& Path, const FWpPlacementMap& Dirty)
{
    if (Dirty.IsEmpty())
    {
        return true;
    }
#if PLATFORM_WINDOWS
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (!Files.CreateDirectoryTree(*FPaths::GetPath(Path)))
    {
        return false;
    }

    // A zero share mode serializes writers across game processes.
    const FString LockPath = Path + TEXT(".lock");
    HANDLE Lock = ::CreateFileW(*LockPath, GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Lock == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    ON_SCOPE_EXIT { ::CloseHandle(Lock); };

    FWpPlacementMap All;
    const ELoadResult State = Load(Path, All);
    if (State != ELoadResult::Loaded && State != ELoadResult::Missing)
    {
        return false;
    }
    for (const auto& Pair : Dirty)
    {
        if (IsValidIdentity(Pair.Key) && ValidSnapshot(Pair.Value))
        {
            All.Add(Pair.Key, Pair.Value);
        }
    }

    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const auto& Pair : All)
    {
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("profile"), Pair.Key.Profile);
        Row->SetStringField(TEXT("key"), Pair.Key.Key);
        Row->SetObjectField(TEXT("normal"), WriteRect(Pair.Value.Normal));
        Row->SetObjectField(TEXT("workArea"), WriteRect(Pair.Value.WorkArea));
        Row->SetStringField(TEXT("monitorId"), Pair.Value.MonitorId);
        Row->SetNumberField(TEXT("dpi"), Pair.Value.Dpi);
        Row->SetBoolField(TEXT("maximized"), Pair.Value.bMaximized);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
    Root->SetArrayField(TEXT("placements"), MoveTemp(Rows));
    FString Text;
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text)))
    {
        return false;
    }

    const FString Temp = Path + TEXT(".tmp");
    ON_SCOPE_EXIT { Files.DeleteFile(*Temp); };
    return FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
        ::MoveFileExW(*Temp, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return false;
#endif
}
```

The lock file can remain on disk. The open handle owns the lock, so a crashed process releases it. The replacement runs only after the temporary file closes. This implementation preserves unreadable, malformed, and unsupported whole files. After backing up a malformed file, remove it to enable fresh saves. Malformed individual rows are skipped.

### Wire the functions into the lifetime service

Append `InvalidPlacementIdentity` and `DuplicatePlacementIdentity` to `EWpWindowResultCode`. Add these members to the private runtime entry. Include `WpPlacement.h` there and in the lifetime service header.

```cpp
TOptional<FWpPlacementIdentity> PlacementId;
FWpPlacementSnapshot Placement;
FVector2D PlacementMinimumClientSize = FVector2D::ZeroVector;
bool bHasPlacement = false;
bool bRecoverPlacement = false;
```

Add these two public methods to `FWpNativeWindowHost` and put their bodies in `WpNativeWindowHost.cpp`. `GetNativeHandle` is for the private plugin layer.

```cpp
void* FWpNativeWindowHost::GetNativeHandle() const
{
    const TSharedPtr<FGenericWindow> Native = Window ? Window->GetNativeWindow() : nullptr;
    return Native ? Native->GetOSWindowHandle() : nullptr;
}

void FWpNativeWindowHost::MaximizeWindow()
{
    if (Window && IsTracked() && HasNativeHandle())
    {
        Window->Maximize();
    }
}
```

Add these private declarations to `FWpWindowLifetimeService`.

```cpp
void LoadPlacements();
void CapturePlacement(FWpWindowRuntimeEntry& Entry);
void TickPlacements();
void FlushPlacements();

FWpPlacementMap SavedPlacements;
FWpPlacementMap DirtyPlacements;
TOptional<FString> PlacementTopology;
double NextPlacementSample = 0.0;
double PlacementFlushAt = 0.0;
bool bPlacementLoaded = false;
bool bPlacementWritesEnabled = false;
```

Copy these methods into `WpWindowLifetimeService.cpp`. Include `HAL/PlatformTime.h`.

```cpp
void FWpWindowLifetimeService::LoadPlacements()
{
    if (bPlacementLoaded)
    {
        return;
    }
    bPlacementLoaded = true;
    const WpPlacement::ELoadResult State =
        WpPlacement::Load(WpPlacement::DefaultPath(), SavedPlacements);
    bPlacementWritesEnabled = State == WpPlacement::ELoadResult::Loaded ||
        State == WpPlacement::ELoadResult::Missing;
    if (!bPlacementWritesEnabled)
    {
        UE_LOG(LogTemp, Warning, TEXT("WindowingPlugin placement file could not be loaded (%d)."),
            static_cast<int32>(State));
    }
}

void FWpWindowLifetimeService::CapturePlacement(FWpWindowRuntimeEntry& Entry)
{
    if (!Entry.PlacementId.IsSet() || !Entry.Host || !Entry.Record->bOpenedEventEmitted)
    {
        return;
    }

    void* Handle = Entry.Host->GetNativeHandle();
    FWpPlacementSnapshot Current = Entry.Placement;
    if (!WpPlacement::Capture(Handle, Current) ||
        (WpPlacement::IsNormal(Handle) &&
            !WpPlacement::HasReachableTitleBar(Current.Normal, WpPlacement::ReadMonitors())))
    {
        return;
    }
    Entry.Placement = MoveTemp(Current);
    Entry.bHasPlacement = true;
    const FWpPlacementSnapshot* Previous = SavedPlacements.Find(Entry.PlacementId.GetValue());
    if (!Previous || !(*Previous == Entry.Placement))
    {
        SavedPlacements.Add(Entry.PlacementId.GetValue(), Entry.Placement);
        if (bPlacementWritesEnabled)
        {
            DirtyPlacements.Add(Entry.PlacementId.GetValue(), Entry.Placement);
            PlacementFlushAt = FPlatformTime::Seconds() + 0.75;
        }
    }
}

void FWpWindowLifetimeService::FlushPlacements()
{
    if (DirtyPlacements.IsEmpty())
    {
        return;
    }
    if (WpPlacement::MergeAndSave(WpPlacement::DefaultPath(), DirtyPlacements))
    {
        DirtyPlacements.Reset();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("WindowingPlugin could not save window placement."));
        PlacementFlushAt = FPlatformTime::Seconds() + 5.0;
    }
}

static FString GetPlacementTopology(TConstArrayView<FWpPlacementMonitor> Monitors)
{
    TArray<FString> Parts;
    for (const FWpPlacementMonitor& Monitor : Monitors)
    {
        Parts.Add(FString::Printf(TEXT("%s|%d,%d,%d,%d|%d|%d"),
            *Monitor.Id, Monitor.WorkArea.Min.X, Monitor.WorkArea.Min.Y,
            Monitor.WorkArea.Max.X, Monitor.WorkArea.Max.Y, Monitor.Dpi, Monitor.bPrimary));
    }
    Parts.Sort();
    return FString::Join(Parts, TEXT("\n"));
}

void FWpWindowLifetimeService::TickPlacements()
{
    if (OperationDepth != 0 || bShuttingDown)
    {
        return;
    }

    const double Now = FPlatformTime::Seconds();
    if (bPlacementLoaded && Now >= NextPlacementSample)
    {
        NextPlacementSample = Now + 0.25;
        const TArray<FWpPlacementMonitor> Monitors = WpPlacement::ReadMonitors();
        const FString Topology = GetPlacementTopology(Monitors);
        const bool bTopologyChanged = PlacementTopology.IsSet() && !Monitors.IsEmpty() &&
            PlacementTopology.GetValue() != Topology;
        if (!Monitors.IsEmpty())
        {
            PlacementTopology = Topology;
        }

        TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
        TArray<FGuid> Ids;
        Entries.GenerateKeyArray(Ids);
        for (const FGuid& Id : Ids)
        {
            const TUniquePtr<FWpWindowRuntimeEntry>* Found = Entries.Find(Id);
            if (!Found || (*Found)->Record->State != EWpWindowState::Open ||
                !(*Found)->PlacementId.IsSet() || !(*Found)->Host)
            {
                continue;
            }
            FWpWindowRuntimeEntry& Entry = *(*Found);
            Entry.bRecoverPlacement |= bTopologyChanged;
            if (Entry.bRecoverPlacement && Entry.bHasPlacement)
            {
                void* Handle = Entry.Host->GetNativeHandle();
                if (!WpPlacement::IsNormal(Handle) || Monitors.IsEmpty())
                {
                    continue;
                }
                Entry.bRecoverPlacement = false;
                FWpPlacementSnapshot Current = Entry.Placement;
                if (WpPlacement::Capture(Handle, Current) &&
                    !WpPlacement::HasReachableTitleBar(Current.Normal, Monitors))
                {
                    if (!WpPlacement::Restore(Handle, Entry.Placement, Entry.PlacementMinimumClientSize))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("WindowingPlugin could not recover window placement."));
                    }
                    // SetWindowPos can run callbacks. Sample the settled rectangle on the next tick.
                    continue;
                }
            }
            CapturePlacement(Entry);
        }
    }
    if (!DirtyPlacements.IsEmpty() && Now >= PlacementFlushAt)
    {
        FlushPlacements();
    }
}
```

In `CreateWindow`, paste this before `BindSlateHooks()`. The insertion happens before the first call into widget or native code, so a reentrant open sees the reservation as soon as the entry is added to `Entries`.

```cpp
TOptional<FWpPlacementIdentity> PlacementId;
if (!Options.PlacementKey.IsEmpty())
{
    FWpPlacementIdentity Identity{Options.PlacementProfile, Options.PlacementKey};
    if (!WpPlacement::IsValidIdentity(Identity))
    {
        return Result(EWpWindowResultCode::InvalidPlacementIdentity, TEXT("Invalid placement profile or key."));
    }
    for (const auto& Pair : Entries)
    {
        const auto& Existing = Pair.Value->PlacementId;
        if (Existing.IsSet() && Existing.GetValue() == Identity)
        {
            return Result(EWpWindowResultCode::DuplicatePlacementIdentity,
                TEXT("This placement identity already has a window."));
        }
    }
    LoadPlacements();
    PlacementId = MoveTemp(Identity);
}
```

Before `Entries.Add(Id, MoveTemp(NewEntry))`, set `Entry.PlacementId = MoveTemp(PlacementId);` and `Entry.PlacementMinimumClientSize = Options.MinimumClientSize;`. Replace the existing combined registration and show check with the block below. Leave the following session check, state transition, and opened event in place.

```cpp
if (!Entry.Host->RegisterWindow())
{
    return Fail(EWpWindowResultCode::NativeCreationFailed, TEXT("Native window registration failed."));
}
bool bRestoreMaximized = false;
if (Options.bRestorePlacement && Entry.PlacementId.IsSet())
{
    if (const FWpPlacementSnapshot* Saved = SavedPlacements.Find(Entry.PlacementId.GetValue()))
    {
        const FWpPlacementSnapshot Snapshot = *Saved;
        if (WpPlacement::Restore(Entry.Host->GetNativeHandle(), Snapshot, Options.MinimumClientSize))
        {
            bRestoreMaximized = Snapshot.bMaximized;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("WindowingPlugin could not apply saved bounds."));
        }
    }
}
if (!Entry.Record->bNativeCloseObserved && Entry.PlacementId.IsSet())
{
    Entry.bHasPlacement = WpPlacement::Capture(Entry.Host->GetNativeHandle(), Entry.Placement);
}
if (Entry.Record->bNativeCloseObserved || !Entry.Host->ShowWindow())
{
    return Fail(EWpWindowResultCode::NativeCreationFailed, TEXT("Native window failed to open."));
}
if (bRestoreMaximized && !Entry.Record->bNativeCloseObserved)
{
    Entry.Host->MaximizeWindow();
}
if (Entry.Record->bNativeCloseObserved)
{
    return Fail(EWpWindowResultCode::NativeCreationFailed, TEXT("Native window closed while opening."));
}
```

Before broadcasting `NotifyOpened`, set `Entry.Record->bOpenedEventEmitted = true;` as in M1, then call `CapturePlacement(Entry)`. The comparison against the stored snapshot saves the first valid placement even when it matches the pre-show capture.

In `OnNativePreClose`, call `CapturePlacement(*(*Found))` after finding the entry and before Slate destroys its handle. In `Drain()`, before removing a keyed entry with a dirty snapshot, set `PlacementFlushAt = FPlatformTime::Seconds();` to make it eligible for a flush. In `OnSlatePostTick`, call `TickPlacements()` after `Drain()`. After the terminal `Drain()` in `Shutdown()`, call `FlushPlacements()`. File I/O stays outside the pre-close callback.
