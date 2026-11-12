# M3 implementation guide — native activation and Slate keyboard focus

M3 lets the host react when a managed window gains or loses foreground activation. It also reports whether Slate's keyboard focus belongs to that window. Use the information in the host if the main viewport or camera should pause its input. The plugin observes focus and leaves input policy to the host.

The query returns a live snapshot for the caller's GameInstance. The event reports changes observed on Slate post-tick. An invalid handle means the Windows foreground window is outside the caller's managed windows. This includes the main game window, another application, and a managed window owned by another GameInstance. Keyboard focus is reported only when the same managed window is in the foreground. M3 observes managed top-level windows. A separate popup HWND can briefly report no managed foreground window.

## 1. Add the Blueprint types and API

In `Public/WpWindowTypes.h`, append this struct after the existing types.

```cpp
USTRUCT(BlueprintType)
struct WINDOWINGPLUGIN_API FWpWindowFocusSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Windowing")
    FWpWindowHandle ActiveWindow;

    UPROPERTY(BlueprintReadOnly, Category="Windowing")
    bool bHasKeyboardFocus = false;
};
```

In `Public/WpWindowingSubsystem.h`, put this declaration next to `FWpOnWindowClosed`, before the `UCLASS()`.

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FWpOnWindowFocusChanged,
    FWpWindowFocusSnapshot, Previous,
    FWpWindowFocusSnapshot, Current);
```

Add this block to the public part of `UWpWindowingSubsystem`, after `OnWindowClosed`.

```cpp
UFUNCTION(BlueprintPure, Category="Windowing")
FWpWindowFocusSnapshot GetWindowFocus() const;

UPROPERTY(BlueprintAssignable, Category="Windowing")
FWpOnWindowFocusChanged OnWindowFocusChanged;
```

Add these two lines to the private part of the same class, after `NotifyClosed`.

```cpp
void NotifyFocusChanged(const FWpWindowFocusSnapshot& Current);
FWpWindowFocusSnapshot LastReportedFocus;
```

In `Private/WpWindowingSubsystem.cpp`, add the following functions after `NotifyClosed`. In `Initialize`, set `LastReportedFocus = {};` after setting `bDeinitializing = false;`.

```cpp
FWpWindowFocusSnapshot UWpWindowingSubsystem::GetWindowFocus() const
{
    if (IsInGameThread() && !bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
    {
        if (const FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
        {
            return Service->GetWindowFocus(SessionId);
        }
    }
    return {};
}

void UWpWindowingSubsystem::NotifyFocusChanged(const FWpWindowFocusSnapshot& Current)
{
    if (bDeinitializing || IsEngineExitRequested() ||
        (LastReportedFocus.ActiveWindow.Id == Current.ActiveWindow.Id &&
            LastReportedFocus.bHasKeyboardFocus == Current.bHasKeyboardFocus))
    {
        return;
    }

    const FWpWindowFocusSnapshot Previous = LastReportedFocus;
    LastReportedFocus = Current;
    OnWindowFocusChanged.Broadcast(Previous, Current);
}
```

## 2. Connect focus detection to the lifetime service

In the public part of `Private/WpNativeWindowHost.h`, add this method after `GetNativeHandle`. It compares borrowed Slate window pointers without extending a window's lifetime.

```cpp
bool IsSlateWindow(const SWindow* Candidate) const
{
    return Candidate && Window.Get() == Candidate;
}

static void* GetForegroundHandle();
```

In the public part of `Private/WpWindowLifetimeService.h`, add the query after `GetState`.

```cpp
FWpWindowFocusSnapshot GetWindowFocus(const FGuid& SessionId) const;
```

In the private part of the same header, add these declarations after `Drain`. Keep a weak observer for a session even after its last window closes so that it can receive the final transition to an invalid handle.

```cpp
void TickWindowFocus();
TMap<FGuid, TWeakObjectPtr<UWpWindowingSubsystem>> FocusObservers;
```

In `Private/WpNativeWindowHost.cpp`, add the Windows include after the existing includes. M2 already adds the Win64 link dependency on `user32.lib`.

```cpp
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
```

Add the following function at the end of `WpNativeWindowHost.cpp`. `GetForegroundWindow()` checks actual OS activation, which can differ from Slate's last active window after switching to another application.

```cpp
void* FWpNativeWindowHost::GetForegroundHandle()
{
#if PLATFORM_WINDOWS
    return ::GetForegroundWindow();
#else
    return nullptr;
#endif
}
```

In `Private/WpWindowLifetimeService.cpp`, add these implementations after `GetState`.

```cpp
FWpWindowFocusSnapshot FWpWindowLifetimeService::GetWindowFocus(const FGuid& SessionId) const
{
    FWpWindowFocusSnapshot Result;
    if (bShuttingDown || !SessionId.IsValid() || IsEngineExitRequested() ||
        !FSlateApplication::IsInitialized())
    {
        return Result;
    }

    const void* Foreground = FWpNativeWindowHost::GetForegroundHandle();
    if (!Foreground)
    {
        return Result;
    }

    for (const auto& Pair : Entries)
    {
        const FWpWindowRuntimeEntry& Entry = *Pair.Value;
        if (Entry.Record->SessionId != SessionId ||
            Entry.Record->State != EWpWindowState::Open || !Entry.Host ||
            Entry.Host->GetNativeHandle() != Foreground)
        {
            continue;
        }

        Result.ActiveWindow.Id = Pair.Key;
        FSlateApplication& Slate = FSlateApplication::Get();
        if (const TSharedPtr<SWidget> Focused = Slate.GetKeyboardFocusedWidget())
        {
            const TSharedPtr<SWindow> FocusedWindow = Slate.FindWidgetWindow(Focused.ToSharedRef());
            Result.bHasKeyboardFocus = Entry.Host->IsSlateWindow(FocusedWindow.Get());
        }
        break;
    }
    return Result;
}

void FWpWindowLifetimeService::TickWindowFocus()
{
    if (OperationDepth != 0 || bShuttingDown || IsEngineExitRequested() || FocusObservers.IsEmpty())
    {
        return;
    }

    TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
    TArray<FGuid> Sessions;
    FocusObservers.GenerateKeyArray(Sessions);
    for (const FGuid& Session : Sessions)
    {
        const TWeakObjectPtr<UWpWindowingSubsystem>* Found = FocusObservers.Find(Session);
        if (UWpWindowingSubsystem* Sink = Found ? Found->Get() : nullptr)
        {
            Sink->NotifyFocusChanged(GetWindowFocus(Session));
        }
        else
        {
            FocusObservers.Remove(Session);
        }
    }
}
```

Wire the service methods into the following existing functions in that same file.

1. In `CreateWindow`, add the following immediately after `OutHandle.Id = Id;` and before `WeakSink.Get()->NotifyOpened(OutHandle);`. At this point the record is published and the owning session is still valid.

   ```cpp
   FocusObservers.Add(RequestSession, WeakSink);
   ```

2. Replace the body of `OnSlatePostTick` with these three calls. Draining first lets the event report the loss of activation after the last managed window retires.

   ```cpp
   Drain();
   TickPlacements();
   TickWindowFocus();
   ```

3. In `BeginSessionShutdown`, insert `FocusObservers.Remove(SessionId);` immediately after `check(IsInGameThread());`. In `Shutdown`, insert `FocusObservers.Reset();` immediately after setting `bShuttingDown = true;`.

The event reflects observations once per Slate post-tick. A window that gains and loses foreground activation within one tick may produce no event. Moving keyboard focus between two controls in the same managed window leaves the snapshot unchanged. The direct query reads the current state without waiting for the next event.

These code blocks compiled and linked against the M2 source in a disposable UE 5.8 Win64 Development Editor build.

## 3. Test with real windows in Unreal Editor

The M3 live check was confirmed by the user. The packaged release checks are part of M4. [M4 release acceptance test](WindowingPlugin_M4_Guide.md#4-final-release-acceptance-test)

Build **Win64 Development Editor**, then open the existing **WBP_WindowingControl**. Use the existing Open A and Open B buttons and the editable text boxes in **WBP_WindowingSmokeTest**. If both buttons use placement keys, give them different keys.

1. Add two Text Blocks to the main panel and enable **Is Variable** for each. Name them `FocusStatus` and `FocusChanges`. Add an Integer Blueprint variable named `FocusChangeCount`, defaulted to zero.
2. In the control panel's existing **On Initialized** graph, use its typed **Wp Windowing Subsystem** reference to bind **On Window Focus Changed**. Create the matching event from the red **Event** pin. In the event, split **Current**, then split **Active Window** to access **Id**. Use **Format Text** and **Set Text** to show the GUID and **Has Keyboard Focus** in `FocusStatus`. Increment `FocusChangeCount` and show it in `FocusChanges`. You can also connect the same formatted text to **Print String** with **Print to Log** enabled, so events remain inspectable while another app is foreground.
3. Use **Get Window Focus** in the **Refresh State** button graph and display its result in `FocusStatus` as well. This button reads the snapshot after you have clicked the main game window, so expect the invalid handle at that point.
4. Choose **Standalone Game** from the editor's Play menu. Open A and B, arrange them so you can see the main panel, and run the checks below. Use the handle GUIDs displayed by the existing open/close logging, or log each handle from its Open node output, to tell A and B apart.

| Action | Expected observation |
|---|---|
| Click A's title bar, then click its editable text box and type | The active GUID matches A. **Has Keyboard Focus** is true while typing in A. |
| Click B and type in its text box | The active GUID changes to B. Text goes into B's widget. |
| Keep B active and type several characters | The focus count stays fixed while the snapshot stays the same. |
| Click the main game window | The active GUID becomes invalid and **Has Keyboard Focus** is false. |
| Click A, then switch to another app with Alt+Tab | The focus log shows an invalid GUID and false after the other app becomes foreground. Returning to A reports A again. |
| Close the active managed window with its X | The next snapshot reports whichever managed window Windows activates, or an invalid GUID. The closed window's GUID never remains active. |
| Close all managed windows and reopen one | The reopened window has a fresh handle and reports that handle when it becomes foreground. |

For an extra session-isolation check, run two local PIE clients in one process and use a managed window from each. When you activate the second client's managed window, the first client's snapshot must have an invalid handle. Keep the Standalone checks for real Windows activation and Alt+Tab behavior.
