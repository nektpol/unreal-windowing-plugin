# M5.1 implementation guide — dock and undock panels and groups

M5.1 transfer is implemented and panel dock and undock passed the host project's manual Standalone check. Use the Blueprint steps to reproduce that check. The source patch is a reference for the pre-M5.1 checkout. [Next milestone](WindowingPlugin_M5_2_Headers_Collapse_Settings_Guide.md)

M5.1 transfers an existing panel between a native window and a viewport dock. It also moves a whole docker group into one native window and back to its original edge. The same widget instances, handles, child order, and individual floating placement keys survive both kinds of transfer. [Docking interaction design](WindowingPlugin_M5_Docking_Interaction_Design.md)

This guide starts from the current working copy. It already has `UWpDockWorkspace`, all four `EWpDockSide` values, the Blueprint declarations for individual panel transfer, and the presentation delegate.

## 1. Apply the matching source changes

The patch contains the complete C++ implementation, including the edits to the existing source files. The rest of this guide explains the order and provides copy-ready blocks for the new functions. From the plugin repository in PowerShell:

```powershell
Set-Location 'C:\Unreal Projects\dnd-vtt\Plugins\WindowingPlugin'
git apply --check --ignore-space-change .\docs\WindowingPlugin_M5_1_Source.patch
if ($LASTEXITCODE -eq 0) {
    git apply --ignore-space-change .\docs\WindowingPlugin_M5_1_Source.patch
}
```

If the check reports a mismatch, use the manual steps below and compare any declarations or functions you already copied with the guide. The patch targets the source revision used for the reference build. The patch applies changes to the working tree. Use either the patch or the manual steps once. [M5.1 source patch](WindowingPlugin_M5_1_Source.patch)

## 2. Update the public API and workspace helper

1. In `Public/WpWindowTypes.h`, append `DockEdgeOccupied` to the result enum and `InFloatingGroup` to the presentation mode. Add a Blueprint-readable `FGuid GroupId` to `FWpWindowPresentation`. Read `Side` only when `GroupId` is valid. It identifies the group's viewport edge, including the edge it last occupied when floating.
2. In `Public/WpWindowingSubsystem.h`, add Blueprint-callable `FWpWindowResult UndockGroup(FGuid GroupId)` and `FWpWindowResult DockGroup(FGuid GroupId)` in the public section. Add `void NotifyPresentationChanged(FWpWindowHandle Handle, const FWpWindowPresentation& Presentation)` in the private section. Keep the existing panel APIs and delegate.
3. In `Private/WpWindowingSubsystem.cpp`, include `WpDockWorkspace.h` and append the functions below. Registration checks that the widget belongs to the same GameInstance. Unregistration matches the registered widget even if its world is already going away.
4. In `Private/WpDockWorkspace.cpp`, replace `AddDockSlot` with the helper below and add `Components/PanelWidget.h`. The four `BindWidget` fields in your existing header supply the containers. Each occupied edge gets one native-widget slot for the whole group's Slate layout.

### WpWindowingSubsystem.cpp

```cpp
FWpWindowResult UWpWindowingSubsystem::RegisterDockWorkspace(UWpDockWorkspace* Workspace)
{
    if (!IsInGameThread())
    {
        return WpWindowingSubsystemPrivate::WrongThread();
    }
    if (bDeinitializing || !SessionId.IsValid() || IsEngineExitRequested())
    {
        return WpWindowingSubsystemPrivate::Unavailable();
    }
    if (!IsValid(Workspace) || Workspace->GetGameInstance() != GetGameInstance())
    {
        return WpWindowingSubsystemPrivate::MakeResult(
            EWpWindowResultCode::InvalidOwningContext,
            TEXT("The dock workspace must belong to this GameInstance."));
    }
    if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
    {
        return Service->RegisterDockWorkspace(SessionId, Workspace);
    }
    return WpWindowingSubsystemPrivate::Unavailable();
}

FWpWindowResult UWpWindowingSubsystem::UnregisterDockWorkspace(UWpDockWorkspace* Workspace)
{
    if (!IsInGameThread())
    {
        return WpWindowingSubsystemPrivate::WrongThread();
    }
    if (bDeinitializing || !SessionId.IsValid() || IsEngineExitRequested())
    {
        return WpWindowingSubsystemPrivate::Unavailable();
    }
    if (!Workspace)
    {
        return WpWindowingSubsystemPrivate::MakeResult(
            EWpWindowResultCode::DockWorkspaceUnavailable, TEXT("A dock workspace is required."));
    }
    if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
    {
        return Service->UnregisterDockWorkspace(SessionId, Workspace);
    }
    return WpWindowingSubsystemPrivate::Unavailable();
}

FWpWindowResult UWpWindowingSubsystem::DockWindow(FWpWindowHandle Handle, EWpDockSide Side)
{
    if (!IsInGameThread())
    {
        return WpWindowingSubsystemPrivate::WrongThread();
    }
    if (!bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
    {
        if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
        {
            return Service->DockWindow(SessionId, Handle, Side);
        }
    }
    return WpWindowingSubsystemPrivate::Unavailable();
}

FWpWindowResult UWpWindowingSubsystem::UndockWindow(FWpWindowHandle Handle)
{
    if (!IsInGameThread())
    {
        return WpWindowingSubsystemPrivate::WrongThread();
    }
    if (!bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
    {
        if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
        {
            return Service->UndockWindow(SessionId, Handle);
        }
    }
    return WpWindowingSubsystemPrivate::Unavailable();
}

FWpWindowPresentation UWpWindowingSubsystem::GetWindowPresentation(FWpWindowHandle Handle) const
{
    if (IsInGameThread() && !bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
    {
        if (const FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
        {
            return Service->GetWindowPresentation(SessionId, Handle);
        }
    }
    return {};
}

void UWpWindowingSubsystem::NotifyPresentationChanged(
    FWpWindowHandle Handle, const FWpWindowPresentation& Presentation)
{
    if (!bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
    {
        OnWindowPresentationChanged.Broadcast(Handle, Presentation);
    }
}

FWpWindowResult UWpWindowingSubsystem::UndockGroup(FGuid GroupId)
{
    if (!IsInGameThread())
    {
        return WpWindowingSubsystemPrivate::WrongThread();
    }
    if (!bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
    {
        if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
        {
            return Service->UndockGroup(SessionId, GroupId);
        }
    }
    return WpWindowingSubsystemPrivate::Unavailable();
}

FWpWindowResult UWpWindowingSubsystem::DockGroup(FGuid GroupId)
{
    if (!IsInGameThread())
    {
        return WpWindowingSubsystemPrivate::WrongThread();
    }
    if (!bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
    {
        if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
        {
            return Service->DockGroup(SessionId, GroupId);
        }
    }
    return WpWindowingSubsystemPrivate::Unavailable();
}
```

### WpDockWorkspace.cpp

```cpp
UNativeWidgetHost* UWpDockWorkspace::AddDockSlot(EWpDockSide Side)
{
    UPanelWidget* Stack = nullptr;
    switch (Side)
    {
    case EWpDockSide::Left:   Stack = LeftDock.Get(); break;
    case EWpDockSide::Right:  Stack = RightDock.Get(); break;
    case EWpDockSide::Top:    Stack = TopDock.Get(); break;
    case EWpDockSide::Bottom: Stack = BottomDock.Get(); break;
    }
    if (!IsValid(Stack))
    {
        return nullptr;
    }

    UNativeWidgetHost* DockHost = NewObject<UNativeWidgetHost>(this);
    return Stack->AddChild(DockHost) ? DockHost : nullptr;
}
```

## 3. Connect the transfer code to the existing lifecycle

Use these edits when applying the guide manually. Paths in this step are relative to `Source/WindowingPlugin/Private`. The source patch also contains these changes. If you already copied some declarations, compare them against the blocks below and keep one copy of each. Steps 3 and 4 build together because the lifecycle code calls the transfer helpers defined in Step 4.

### 3.1 Let a native host replace its Slate content

In `WpNativeWindowHost.h`, find `class FWpNativeWindowHost`. Add this declaration in its `public:` section immediately after `Construct(...)`.

```cpp
    void SetContent(const TSharedRef<SWidget>& Content);
```

In `WpNativeWindowHost.cpp`, append this function after `GetNativeHandle`. Passing the retained widget attaches it to the host. The transfer code passes an empty Slate widget when it detaches content.

```cpp
void FWpNativeWindowHost::SetContent(const TSharedRef<SWidget>& Content)
{
    if (Window)
    {
        Window->SetContent(Content);
    }
}
```

### 3.2 Store the state that moves with a panel

Open `WpWindowRuntimeEntry.h`. Add `#include "WpWindowTypes.h"` with the other includes, and add `class SBox;` and `class SWidget;` above `struct FWpWindowRuntimeEntry`. Inside that struct, paste these members immediately after `bFailedCreation`, before `PlacementId`.

```cpp
    TSharedPtr<SWidget> RetainedContent;
    FWpWindowCreateOptions CreateOptions;
    FGuid DockGroupId;
    TSharedPtr<SBox> DockContentSlot;
```

`RetainedContent` is the original result of `TakeWidget()`. `CreateOptions` is used to construct a replacement individual native host, including the latest panel title. `DockGroupId` identifies the group that currently owns the panel, and `DockContentSlot` is the Slate box within that group that holds its content. Both group fields start empty.

### 3.3 Declare the docking operations on the lifetime service

Open `WpWindowLifetimeService.h`. Add `#include "Widgets/SWindow.h"` for `FOnWindowClosed`. In the forward declaration list add `class FWpNativeWindowHost;`, `class UWpDockWorkspace;`, and `struct FWpDockGroup;`.

Inside `class FWpWindowLifetimeService`, add the following in `public:`, between `GetWindowFocus` and `BeginSessionShutdown`. These are the service methods called by the subsystem functions in Step 2.

```cpp
    FWpWindowResult RegisterDockWorkspace(const FGuid& SessionId, UWpDockWorkspace* Workspace);
    FWpWindowResult UnregisterDockWorkspace(const FGuid& SessionId, UWpDockWorkspace* Workspace);
    FWpWindowResult DockWindow(const FGuid& SessionId, FWpWindowHandle Handle, EWpDockSide Side);
    FWpWindowResult UndockWindow(const FGuid& SessionId, FWpWindowHandle Handle);
    FWpWindowResult UndockGroup(const FGuid& SessionId, FGuid GroupId);
    FWpWindowResult DockGroup(const FGuid& SessionId, FGuid GroupId);
    FWpWindowPresentation GetWindowPresentation(const FGuid& SessionId, FWpWindowHandle Handle) const;
```

In the same class, find the existing private declaration `void OnNativePreClose(FGuid Id);` after `BindSlateHooks()`. Replace that declaration with the following block. Leave `FindPublished`, `BindSlateHooks`, and the declarations from `CloseEntry` onward in place.

```cpp
    void OnNativePreClose(FGuid Id, const SWindow& Window);
    void OnGroupNativePreClose(FGuid Id, const SWindow& Window);
    FOnWindowClosed MakeTransferCloseCallback(FGuid Id, bool bGroup, TSharedPtr<bool> CandidateClosed = nullptr);
    void RetireDockHost(TUniquePtr<FWpNativeWindowHost> Host, bool bCloseObserved = false);
    void DrainDockHosts();
    UWpDockWorkspace* FindDockWorkspace(const FGuid& SessionId) const;
    FWpDockGroup* FindEdgeGroup(const FGuid& SessionId, EWpDockSide Side) const;
    void CloseGroupChildren(FWpDockGroup& Group, EWpWindowCloseReason Reason);
    void RemoveGroupMember(FWpWindowRuntimeEntry& Entry);
    bool CanMoveGroup(const FWpDockGroup& Group) const;
    FWpWindowResult MoveGroup(const FGuid& SessionId, FGuid GroupId, bool bFloat);
    void EmitPresentationChanged(const FGuid& SessionId, FGuid Id);

    TMap<FGuid, TWeakObjectPtr<UWpDockWorkspace>> DockWorkspaces;
    TMap<FGuid, TSharedPtr<FWpDockGroup>> DockGroups;
    TArray<TUniquePtr<FWpNativeWindowHost>> RetiringDockHosts;
    bool bMovingDockPresentation = false;
```

The maps track the registered viewport workspace for each session and the live groups by ID. The retiring array owns native hosts while Slate and Windows finish closing them. The transfer flag guards against a second mutation while Slate callbacks or presentation listeners are running.

### 3.4 Retain the original widget when a panel opens

In `WpWindowLifetimeService.cpp`, add `#include "WpDockGroup.h"` and `#include "WpDockWorkspace.h"` beside `#include "WpNativeWindowHost.h"`. Step 4 supplies `WpDockGroup.h`.

Inside `CreateWindow`, find the existing `TakeWidget()` call and the session check that follows. Immediately after that check, replace the old block starting at `const TWeakPtr<FCallbackTarget> WeakTarget = CallbackTarget;` and ending at the closing `));` of the `Entry.Host->Construct` call. Stop before `if (!Entry.Host->RegisterWindow())`. Paste this block in its place.

```cpp
    Entry.RetainedContent = Content;
    Entry.CreateOptions = Options;
    Entry.Host = MakeUnique<FWpNativeWindowHost>();
    Entry.Host->Construct(Options, Content, MakeTransferCloseCallback(Id, false));
```

Farther down in the same function, find the `WpPlacement::Capture` call after the window has been shown. Replace its surrounding `if (!Entry.Record->bNativeCloseObserved && Entry.PlacementId.IsSet())` block with this version. The earlier keyed placement restore code stays in place.

```cpp
    if (!Entry.Record->bNativeCloseObserved)
    {
        Entry.bHasPlacement = WpPlacement::Capture(Entry.Host->GetNativeHandle(), Entry.Placement);
    }
```

### 3.5 Route native close to the current host

In the same `.cpp`, replace the whole existing `OnNativePreClose` function between `CreateWindow` and `CloseEntry`. A panel ID survives host replacement, so the callback also checks the source `SWindow` before recording a logical close. This keeps an old host's close callback from closing a panel that already moved.

```cpp
void FWpWindowLifetimeService::OnNativePreClose(FGuid Id, const SWindow& Window)
{
    check(IsInGameThread());
    if (const TUniquePtr<FWpWindowRuntimeEntry>* Found = Entries.Find(Id))
    {
        if (!(*Found)->Host || !(*Found)->Host->IsSlateWindow(&Window))
        {
            return;
        }
        CapturePlacement(*(*Found));
        UWpWindowRecord& Record = *(*Found)->Record;
        Record.bNativeCloseObserved = true;
        if (Record.State == EWpWindowState::Creating || Record.State == EWpWindowState::Open)
        {
            Record.State = EWpWindowState::Closing;
            Record.CloseReason = EWpWindowCloseReason::UserOrPlatform;
        }
    }
}
```

### 3.6 Guard close and title changes during a transfer

Replace the existing `RequestClose` and `SetTitle` functions between `Rollback` and `GetState`. While a transfer is running, both return `PresentationTransferInProgress`. A docked panel has no individual native host, so a title change updates the options used the next time that panel floats.

```cpp
FWpWindowResult FWpWindowLifetimeService::RequestClose(const FGuid& SessionId,
    FWpWindowHandle Handle)
{
    if (bShuttingDown)
    {
        return Result(EWpWindowResultCode::ShuttingDown, TEXT("WindowingPlugin is shutting down."));
    }
    if (bMovingDockPresentation)
    {
        return Result(EWpWindowResultCode::PresentationTransferInProgress, TEXT("A window presentation is changing."));
    }
    FWpWindowRuntimeEntry* Entry = FindPublished(SessionId, Handle);
    if (!Entry)
    {
        return Result(EWpWindowResultCode::NotFound, TEXT("Window handle is unknown to this session."));
    }
    if (Entry->Record->State == EWpWindowState::Closing)
    {
        return Result(EWpWindowResultCode::AlreadyClosing, TEXT("Window is already closing."));
    }
    if (Entry->Record->State != EWpWindowState::Open)
    {
        return Result(EWpWindowResultCode::InvalidState, TEXT("Window is not open."));
    }
    TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
    CloseEntry(*Entry, EWpWindowCloseReason::HostRequest);
    return Result(EWpWindowResultCode::Success);
}

FWpWindowResult FWpWindowLifetimeService::SetTitle(const FGuid& SessionId,
    FWpWindowHandle Handle, const FText& Title)
{
    if (bShuttingDown)
    {
        return Result(EWpWindowResultCode::ShuttingDown, TEXT("WindowingPlugin is shutting down."));
    }
    if (bMovingDockPresentation)
    {
        return Result(EWpWindowResultCode::PresentationTransferInProgress, TEXT("A window presentation is changing."));
    }
    FWpWindowRuntimeEntry* Entry = FindPublished(SessionId, Handle);
    if (!Entry)
    {
        return Result(EWpWindowResultCode::NotFound, TEXT("Window handle is unknown to this session."));
    }
    if (Entry->Record->State != EWpWindowState::Open)
    {
        return Result(EWpWindowResultCode::InvalidState, TEXT("Window is not open."));
    }
    TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
    Entry->CreateOptions.Title = Title;
    if (Entry->Host)
    {
        Entry->Host->SetTitle(Title);
    }
    return Result(EWpWindowResultCode::Success);
}
```

### 3.7 Release group membership and hosts through the existing drain

Replace the existing `Drain` function between `OnSlatePostTick` and `BeginSessionShutdown`. The first loop requests closure for docked groups whose slot or registered workspace disappeared. The entry loop releases an eligible panel's host and group slot before releasing its retained Slate content and the record's widget reference. `DrainDockHosts()` then checks whether detached native hosts can be released.

```cpp
void FWpWindowLifetimeService::Drain()
{
    check(IsInGameThread());
    if (OperationDepth != 0)
    {
        return;
    }
    TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);

    for (const auto& Pair : DockGroups)
    {
        FWpDockGroup& Group = *Pair.Value;
        if (!Group.Host && (!Group.Slot.IsValid() || !FindDockWorkspace(Group.SessionId)))
        {
            CloseGroupChildren(Group, EWpWindowCloseReason::HostRequest);
        }
    }
    TArray<FGuid> Ids;
    Entries.GenerateKeyArray(Ids);
    for (const FGuid& Id : Ids)
    {
        FWpWindowRuntimeEntry& Entry = *Entries.FindChecked(Id);
        UWpWindowRecord& Record = *Entry.Record;
        if (Record.State != EWpWindowState::Closing ||
            (Entry.Host && (Entry.Host->IsTracked() || Entry.Host->HasNativeHandle())))
        {
            continue;
        }

        if (Entry.Host)
        {
            Entry.Host->ReleaseResources();
            Entry.Host.Reset();
        }
        RemoveGroupMember(Entry);
        Entry.RetainedContent.Reset();
        Record.State = Entry.bFailedCreation ? EWpWindowState::Failed : EWpWindowState::Closed;
        if (!bShuttingDown && !IsEngineExitRequested() && !Entry.bFailedCreation &&
            Record.bOpenedEventEmitted && !Record.bClosedEventEmitted)
        {
            if (UWpWindowingSubsystem* Sink = Entry.EventSink.Get())
            {
                Record.bClosedEventEmitted = true;
                FWpWindowClosedEvent Event;
                Event.Handle.Id = Id;
                Event.Reason = Record.CloseReason;
                Sink->NotifyClosed(Event);
            }
        }
        Record.Widget = nullptr;
        if (Entry.PlacementId.IsSet() && DirtyPlacements.Contains(Entry.PlacementId.GetValue()))
        {
            PlacementFlushAt = FPlatformTime::Seconds();
        }
        Entries.Remove(Id);
    }
    DrainDockHosts();
}
```

In `BeginSessionShutdown`, add `DockWorkspaces.Remove(SessionId);` immediately after `FocusObservers.Remove(SessionId);`. In `Shutdown`, add `DockWorkspaces.Reset();` immediately after `FocusObservers.Reset();`. In `Shutdown` after the existing `Drain();` and `FlushPlacements();` calls, replace the final `if (!Entries.IsEmpty())` check with the following.

```cpp
    if (!Entries.IsEmpty() || !RetiringDockHosts.IsEmpty() || !DockGroups.IsEmpty())
    {
        UE_LOG(LogTemp, Fatal, TEXT("WindowingPlugin still owns windows after the terminal Slate drain."));
    }
```

### 3.8 Keep floating bounds for panels without a placement key

Replace `TickPlacements` near the end of `WpWindowLifetimeService.cpp`. An open panel with an individual native host can now refresh its in-memory bounds even without a placement key. That branch skips the keyed recovery and save path. Docked panels and panels in a floating group have no individual host, so the sampler skips them and keeps their last individual bounds.

```cpp
void FWpWindowLifetimeService::TickPlacements()
{
    if (OperationDepth != 0 || bShuttingDown)
    {
        return;
    }

    const double Now = FPlatformTime::Seconds();
    if (!Entries.IsEmpty() && Now >= NextPlacementSample)
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
                !(*Found)->Host)
            {
                continue;
            }
            FWpWindowRuntimeEntry& Entry = *(*Found);
            if (!Entry.PlacementId.IsSet())
            {
                Entry.bHasPlacement |= WpPlacement::Capture(Entry.Host->GetNativeHandle(), Entry.Placement);
                continue;
            }
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

Before moving to Step 4, check that the header and implementation both use `OnNativePreClose(FGuid Id, const SWindow& Window)`, and that `CreateWindow` has one `TakeWidget()` call and one `Entry.Host->Construct` call. The remaining helper implementations are in Step 4.

## 4. Add the group model and transfer functions

Create the two private files below. Unreal Build Tool discovers the new C++ file in the existing module.

The first panel docked on an edge creates a group. Later panels on that edge join in order. Each group starts as a flat split that follows the edge's axis. A whole-group transfer moves that same split and its children between a single viewport slot and a single native window. Dock Group returns it to its original edge. If that edge is now occupied, it returns `DockEdgeOccupied` and keeps the group floating. Individual panel undock works from either group presentation. When the last child leaves or closes, its empty group host is retired.

During a whole-group move, the service sends one presentation event for each still-open child. Mutating transfer, title, and close calls return the busy result during that batch, so listeners can queue any follow-up until the next tick. A group's native title-bar close closes all its children. Managed headers, group-close confirmation, Rename, dynamic Settings, corner ownership, and nested splits follow in the next M5 stages. The M3 foreground API continues to report individually floating windows. A floating group's shared native window has no single active panel handle in this step.

### Private/WpDockGroup.h

```cpp
#pragma once
#include "WpNativeWindowHost.h"
#include "Components/NativeWidgetHost.h"
#include "Widgets/SBoxPanel.h"

struct FWpDockGroup
{
    FGuid Id = FGuid::NewGuid();
    FGuid SessionId;
    EWpDockSide Side = EWpDockSide::Left;
    FText Title;
    TSharedPtr<SBoxPanel> Content;
    TArray<FGuid> Children;
    TWeakObjectPtr<UNativeWidgetHost> Slot;
    TUniquePtr<FWpNativeWindowHost> Host;
    bool bNativeClosing = false;
};
```

### Private/WpWindowDocking.cpp

```cpp
#include "WpWindowLifetimeService.h"

#include "WpDockGroup.h"
#include "WpDockWorkspace.h"
#include "WpWindowingSubsystem.h"
#include "WpWindowRecord.h"
#include "WpWindowRuntimeEntry.h"
#include "Templates/UnrealTemplate.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"

namespace
{
    FWpWindowResult DockResult(EWpWindowResultCode Code, const TCHAR* Message = TEXT(""))
    {
        FWpWindowResult Result;
        Result.Code = Code;
        Result.Message = Message;
        return Result;
    }

    bool IsHorizontal(EWpDockSide Side)
    {
        return Side == EWpDockSide::Top || Side == EWpDockSide::Bottom;
    }

    void ClearSlot(UNativeWidgetHost* Slot)
    {
        if (IsValid(Slot))
        {
            Slot->SetContent(SNullWidget::NullWidget);
            Slot->RemoveFromParent();
        }
    }
}

FWpWindowResult FWpWindowLifetimeService::RegisterDockWorkspace(
    const FGuid& SessionId, UWpDockWorkspace* Workspace)
{
    if (bShuttingDown || IsEngineExitRequested())
    {
        return DockResult(EWpWindowResultCode::ShuttingDown);
    }
    if (!IsValid(Workspace) || !Workspace->IsInViewport())
    {
        return DockResult(EWpWindowResultCode::DockWorkspaceUnavailable, TEXT("Add the workspace to the viewport first."));
    }
    if (const auto* Existing = DockWorkspaces.Find(SessionId))
    {
        if (Existing->IsValid() && Existing->Get() != Workspace)
        {
            return DockResult(EWpWindowResultCode::InvalidState, TEXT("Unregister the existing workspace first."));
        }
    }
    DockWorkspaces.Add(SessionId, Workspace);
    return DockResult(EWpWindowResultCode::Success);
}

FWpWindowResult FWpWindowLifetimeService::UnregisterDockWorkspace(
    const FGuid& SessionId, UWpDockWorkspace* Workspace)
{
    const auto* Existing = DockWorkspaces.Find(SessionId);
    if (Existing && Existing->Get() == Workspace)
    {
        DockWorkspaces.Remove(SessionId);
        TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
        for (const auto& Pair : DockGroups)
        {
            FWpDockGroup& Group = *Pair.Value;
            if (Group.SessionId == SessionId && !Group.Host)
            {
                CloseGroupChildren(Group, EWpWindowCloseReason::HostRequest);
            }
        }
    }
    return DockResult(EWpWindowResultCode::Success);
}

UWpDockWorkspace* FWpWindowLifetimeService::FindDockWorkspace(const FGuid& SessionId) const
{
    const auto* Found = DockWorkspaces.Find(SessionId);
    UWpDockWorkspace* Workspace = Found ? Found->Get() : nullptr;
    return Workspace && Workspace->IsInViewport() ? Workspace : nullptr;
}

FWpDockGroup* FWpWindowLifetimeService::FindEdgeGroup(const FGuid& SessionId, EWpDockSide Side) const
{
    for (const auto& Pair : DockGroups)
    {
        FWpDockGroup& Group = *Pair.Value;
        if (Group.SessionId == SessionId && Group.Side == Side && !Group.Host)
        {
            return &Group;
        }
    }
    return nullptr;
}

FWpWindowPresentation FWpWindowLifetimeService::GetWindowPresentation(
    const FGuid& SessionId, FWpWindowHandle Handle) const
{
    const FWpWindowRuntimeEntry* Entry = FindPublished(SessionId, Handle);
    FWpWindowPresentation Presentation;
    if (!Entry || Entry->Record->State != EWpWindowState::Open)
    {
        return Presentation;
    }
    Presentation.bIsValid = true;
    if (const auto* Group = DockGroups.Find(Entry->DockGroupId))
    {
        Presentation.Mode = (*Group)->Host ? EWpWindowPresentationMode::InFloatingGroup
            : EWpWindowPresentationMode::Docked;
        Presentation.Side = (*Group)->Side;
        Presentation.GroupId = (*Group)->Id;
    }
    return Presentation;
}

void FWpWindowLifetimeService::EmitPresentationChanged(const FGuid& SessionId, FGuid Id)
{
    FWpWindowHandle Handle;
    Handle.Id = Id;
    const FWpWindowPresentation Snapshot = GetWindowPresentation(SessionId, Handle);
    const FWpWindowRuntimeEntry* Entry = Snapshot.bIsValid ? FindPublished(SessionId, Handle) : nullptr;
    if (UWpWindowingSubsystem* Sink = Entry ? Entry->EventSink.Get() : nullptr)
    {
        Sink->NotifyPresentationChanged(Handle, Snapshot);
    }
}

FOnWindowClosed FWpWindowLifetimeService::MakeTransferCloseCallback(
    FGuid Id, bool bGroup, TSharedPtr<bool> CandidateClosed)
{
    const TWeakPtr<FCallbackTarget> WeakTarget = CallbackTarget;
    return FOnWindowClosed::CreateLambda(
        [WeakTarget, Id, bGroup, CandidateClosed](const TSharedRef<SWindow>& Window)
        {
            if (CandidateClosed)
            {
                *CandidateClosed = true;
            }
            const TSharedPtr<FCallbackTarget> Target = WeakTarget.Pin();
            if (!Target || !Target->Service)
            {
                return;
            }
            if (bGroup)
            {
                Target->Service->OnGroupNativePreClose(Id, Window.Get());
            }
            else
            {
                Target->Service->OnNativePreClose(Id, Window.Get());
            }
        });
}

void FWpWindowLifetimeService::RetireDockHost(TUniquePtr<FWpNativeWindowHost> Host, bool bCloseObserved)
{
    if (!Host)
    {
        return;
    }
    Host->SetContent(SNullWidget::NullWidget);
    FWpNativeWindowHost* Retiring = Host.Get();
    RetiringDockHosts.Add(MoveTemp(Host));
    if (!bCloseObserved && Retiring->IsTracked())
    {
        Retiring->RequestDestroy();
    }
}

void FWpWindowLifetimeService::DrainDockHosts()
{
    for (int32 Index = RetiringDockHosts.Num() - 1; Index >= 0; --Index)
    {
        FWpNativeWindowHost& Host = *RetiringDockHosts[Index];
        if (!Host.IsTracked() && !Host.HasNativeHandle())
        {
            Host.ReleaseResources();
            RetiringDockHosts.RemoveAtSwap(Index);
        }
    }
}

void FWpWindowLifetimeService::CloseGroupChildren(FWpDockGroup& Group, EWpWindowCloseReason Reason)
{
    for (const FGuid& Id : Group.Children)
    {
        if (const auto* Entry = Entries.Find(Id))
        {
            CloseEntry(*(*Entry), Reason);
        }
    }
}

void FWpWindowLifetimeService::OnGroupNativePreClose(FGuid Id, const SWindow& Window)
{
    check(IsInGameThread());
    if (const auto* Found = DockGroups.Find(Id))
    {
        FWpDockGroup& Group = *(*Found);
        if (Group.Host && Group.Host->IsSlateWindow(&Window))
        {
            Group.bNativeClosing = true;
            CloseGroupChildren(Group, EWpWindowCloseReason::UserOrPlatform);
        }
    }
}

void FWpWindowLifetimeService::RemoveGroupMember(FWpWindowRuntimeEntry& Entry)
{
    if (auto* Found = DockGroups.Find(Entry.DockGroupId))
    {
        const TSharedPtr<FWpDockGroup> Group = *Found;
        if (Entry.DockContentSlot)
        {
            Entry.DockContentSlot->SetContent(SNullWidget::NullWidget);
            if (IsHorizontal(Group->Side))
            {
                StaticCastSharedPtr<SHorizontalBox>(Group->Content)->RemoveSlot(Entry.DockContentSlot.ToSharedRef());
            }
            else
            {
                StaticCastSharedPtr<SVerticalBox>(Group->Content)->RemoveSlot(Entry.DockContentSlot.ToSharedRef());
            }
        }
        Group->Children.Remove(Entry.Record->WindowId);
        if (Group->Children.IsEmpty())
        {
            DockGroups.Remove(Group->Id);
            ClearSlot(Group->Slot.Get());
            RetireDockHost(MoveTemp(Group->Host), Group->bNativeClosing);
        }
    }
    Entry.DockContentSlot.Reset();
    Entry.DockGroupId.Invalidate();
}

FWpWindowResult FWpWindowLifetimeService::DockWindow(
    const FGuid& SessionId, FWpWindowHandle Handle, EWpDockSide Side)
{
    if (bMovingDockPresentation)
    {
        return DockResult(EWpWindowResultCode::PresentationTransferInProgress);
    }
    FWpWindowRuntimeEntry* Entry = FindPublished(SessionId, Handle);
    if (!Entry)
    {
        return DockResult(EWpWindowResultCode::NotFound);
    }
    if (Entry->Record->State != EWpWindowState::Open || bShuttingDown)
    {
        return DockResult(EWpWindowResultCode::InvalidState);
    }
    if (const auto* Group = DockGroups.Find(Entry->DockGroupId))
    {
        return DockResult(!(*Group)->Host && (*Group)->Side == Side
            ? EWpWindowResultCode::Success : EWpWindowResultCode::InvalidState);
    }
    UWpDockWorkspace* Workspace = FindDockWorkspace(SessionId);
    if (!Workspace)
    {
        return DockResult(EWpWindowResultCode::DockWorkspaceUnavailable);
    }
    if (!Entry->Host || !Entry->RetainedContent)
    {
        return DockResult(EWpWindowResultCode::InvalidState);
    }

    {
        TGuardValue<bool> Transfer(bMovingDockPresentation, true);
        TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
        FWpDockGroup* Group = FindEdgeGroup(SessionId, Side);
        if (!Group)
        {
            const TSharedRef<FWpDockGroup> NewGroup = MakeShared<FWpDockGroup>();
            NewGroup->SessionId = SessionId;
            NewGroup->Side = Side;
            NewGroup->Title = FText::Format(NSLOCTEXT("WindowingPlugin", "DockTitle", "{0} Docker"),
                StaticEnum<EWpDockSide>()->GetDisplayNameTextByValue(static_cast<int64>(Side)));
            if (IsHorizontal(Side))
            {
                NewGroup->Content = SNew(SHorizontalBox);
            }
            else
            {
                NewGroup->Content = SNew(SVerticalBox);
            }
            UNativeWidgetHost* Slot = Workspace->AddDockSlot(Side);
            if (!Slot || FindDockWorkspace(SessionId) != Workspace ||
                Entry->Record->State != EWpWindowState::Open)
            {
                ClearSlot(Slot);
                return DockResult(EWpWindowResultCode::DockAttachmentFailed);
            }
            NewGroup->Slot = Slot;
            Slot->SetContent(NewGroup->Content.ToSharedRef());
            DockGroups.Add(NewGroup->Id, NewGroup);
            Group = &NewGroup.Get();
        }
        else if (!Group->Slot.IsValid())
        {
            return DockResult(EWpWindowResultCode::DockAttachmentFailed);
        }

        if (Entry->PlacementId.IsSet())
        {
            CapturePlacement(*Entry);
        }
        else
        {
            Entry->bHasPlacement |= WpPlacement::Capture(Entry->Host->GetNativeHandle(), Entry->Placement);
        }
        Entry->Host->SetContent(SNullWidget::NullWidget);
        const TSharedRef<SBox> Slot = SNew(SBox)[Entry->RetainedContent.ToSharedRef()];
        if (IsHorizontal(Side))
        {
            StaticCastSharedPtr<SHorizontalBox>(Group->Content)->AddSlot().FillWidth(1.f)[Slot];
        }
        else
        {
            StaticCastSharedPtr<SVerticalBox>(Group->Content)->AddSlot().FillHeight(1.f)[Slot];
        }
        Entry->DockContentSlot = Slot;
        Entry->DockGroupId = Group->Id;
        Group->Children.Add(Handle.Id);
        RetireDockHost(MoveTemp(Entry->Host));
    }
    EmitPresentationChanged(SessionId, Handle.Id);
    return DockResult(EWpWindowResultCode::Success);
}

FWpWindowResult FWpWindowLifetimeService::UndockWindow(const FGuid& SessionId, FWpWindowHandle Handle)
{
    if (bMovingDockPresentation)
    {
        return DockResult(EWpWindowResultCode::PresentationTransferInProgress);
    }
    FWpWindowRuntimeEntry* Entry = FindPublished(SessionId, Handle);
    if (!Entry)
    {
        return DockResult(EWpWindowResultCode::NotFound);
    }
    if (Entry->Record->State != EWpWindowState::Open || bShuttingDown)
    {
        return DockResult(EWpWindowResultCode::InvalidState);
    }
    if (!Entry->DockGroupId.IsValid())
    {
        return DockResult(EWpWindowResultCode::Success);
    }
    if (!Entry->DockContentSlot || !Entry->RetainedContent)
    {
        return DockResult(EWpWindowResultCode::InvalidState);
    }

    {
        TGuardValue<bool> Transfer(bMovingDockPresentation, true);
        TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
        const TSharedRef<bool> Closed = MakeShared<bool>(false);
        TUniquePtr<FWpNativeWindowHost> Candidate = MakeUnique<FWpNativeWindowHost>();
        Candidate->Construct(Entry->CreateOptions, SNullWidget::NullWidget,
            MakeTransferCloseCallback(Handle.Id, false, Closed));
        if (!Candidate->RegisterWindow() || *Closed || Entry->Record->State != EWpWindowState::Open)
        {
            RetireDockHost(MoveTemp(Candidate), *Closed);
            return DockResult(EWpWindowResultCode::NativeCreationFailed);
        }
        const bool bRestored = Entry->bHasPlacement &&
            WpPlacement::Restore(Candidate->GetNativeHandle(), Entry->Placement, Entry->PlacementMinimumClientSize);
        if (*Closed || Entry->Record->State != EWpWindowState::Open)
        {
            RetireDockHost(MoveTemp(Candidate), *Closed);
            return DockResult(EWpWindowResultCode::NativeCreationFailed);
        }

        Entry->DockContentSlot->SetContent(SNullWidget::NullWidget);
        Candidate->SetContent(Entry->RetainedContent.ToSharedRef());
        const bool bShown = Candidate->ShowWindow();
        if (bShown && bRestored && Entry->Placement.bMaximized && !*Closed)
        {
            Candidate->MaximizeWindow();
        }
        if (!bShown || *Closed || Entry->Record->State != EWpWindowState::Open)
        {
            RetireDockHost(MoveTemp(Candidate), *Closed);
            Entry->DockContentSlot->SetContent(Entry->RetainedContent.ToSharedRef());
            return DockResult(EWpWindowResultCode::NativeCreationFailed);
        }
        Entry->Host = MoveTemp(Candidate);
        Entry->Record->bNativeCloseObserved = false;
        Entry->Record->bNativeDestroyRequested = false;
        RemoveGroupMember(*Entry);
        CapturePlacement(*Entry);
    }
    EmitPresentationChanged(SessionId, Handle.Id);
    return DockResult(EWpWindowResultCode::Success);
}

bool FWpWindowLifetimeService::CanMoveGroup(const FWpDockGroup& Group) const
{
    if (bShuttingDown || Group.bNativeClosing || Group.Children.IsEmpty())
    {
        return false;
    }
    for (const FGuid& Id : Group.Children)
    {
        const auto* Entry = Entries.Find(Id);
        if (!Entry || (*Entry)->Record->State != EWpWindowState::Open)
        {
            return false;
        }
    }
    return true;
}

FWpWindowResult FWpWindowLifetimeService::UndockGroup(const FGuid& SessionId, FGuid GroupId)
{
    return MoveGroup(SessionId, GroupId, true);
}

FWpWindowResult FWpWindowLifetimeService::DockGroup(const FGuid& SessionId, FGuid GroupId)
{
    return MoveGroup(SessionId, GroupId, false);
}

FWpWindowResult FWpWindowLifetimeService::MoveGroup(const FGuid& SessionId, FGuid GroupId, bool bFloat)
{
    if (bMovingDockPresentation)
    {
        return DockResult(EWpWindowResultCode::PresentationTransferInProgress);
    }
    const auto* Found = DockGroups.Find(GroupId);
    if (!Found || (*Found)->SessionId != SessionId)
    {
        return DockResult(EWpWindowResultCode::NotFound);
    }
    FWpDockGroup& Group = *(*Found);
    if (!CanMoveGroup(Group))
    {
        return DockResult(EWpWindowResultCode::InvalidState);
    }
    if (bFloat == Group.Host.IsValid())
    {
        return DockResult(EWpWindowResultCode::Success);
    }
    UWpDockWorkspace* Workspace = FindDockWorkspace(SessionId);
    if (!Workspace || (bFloat && !Group.Slot.IsValid()))
    {
        return DockResult(EWpWindowResultCode::DockWorkspaceUnavailable);
    }
    if (!bFloat && FindEdgeGroup(SessionId, Group.Side))
    {
        return DockResult(EWpWindowResultCode::DockEdgeOccupied);
    }

    TGuardValue<bool> Transfer(bMovingDockPresentation, true);
    TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
    if (bFloat)
    {
        const TSharedRef<bool> Closed = MakeShared<bool>(false);
        TUniquePtr<FWpNativeWindowHost> Candidate = MakeUnique<FWpNativeWindowHost>();
        FWpWindowCreateOptions Options;
        Options.Title = Group.Title;
        Candidate->Construct(Options, SNullWidget::NullWidget, MakeTransferCloseCallback(GroupId, true, Closed));
        if (!Candidate->RegisterWindow() || *Closed || !CanMoveGroup(Group) || !Group.Slot.IsValid())
        {
            RetireDockHost(MoveTemp(Candidate), *Closed);
            return DockResult(EWpWindowResultCode::NativeCreationFailed);
        }
        Group.Slot->SetContent(SNullWidget::NullWidget);
        Candidate->SetContent(Group.Content.ToSharedRef());
        if (!Candidate->ShowWindow() || *Closed || !CanMoveGroup(Group) ||
            FindDockWorkspace(SessionId) != Workspace)
        {
            RetireDockHost(MoveTemp(Candidate), *Closed);
            if (UNativeWidgetHost* Slot = Group.Slot.Get())
            {
                Slot->SetContent(Group.Content.ToSharedRef());
            }
            return DockResult(EWpWindowResultCode::NativeCreationFailed);
        }
        Group.Host = MoveTemp(Candidate);
        ClearSlot(Group.Slot.Get());
        Group.Slot.Reset();
    }
    else
    {
        UNativeWidgetHost* Slot = Workspace->AddDockSlot(Group.Side);
        if (!Slot || !CanMoveGroup(Group) || FindDockWorkspace(SessionId) != Workspace)
        {
            ClearSlot(Slot);
            return DockResult(EWpWindowResultCode::DockAttachmentFailed);
        }
        Group.Host->SetContent(SNullWidget::NullWidget);
        Slot->SetContent(Group.Content.ToSharedRef());
        Group.Slot = Slot;
        RetireDockHost(MoveTemp(Group.Host));
    }

    // Keep transfers blocked until every member has received the committed presentation.
    for (const FGuid& Child : Group.Children)
    {
        EmitPresentationChanged(SessionId, Child);
    }
    return DockResult(EWpWindowResultCode::Success);
}
```

## 5. Build the Blueprint test harness

### 5.1 Set up the workspace in the Designer

Open the existing `WBP_DockWorkspace` in `/Game/Tests/Windowing`. In **Class Settings**, verify that its parent class is `WpDockWorkspace`. Build or check this hierarchy in the Designer.

```text
Root (Horizontal Box)
  LeftDock (Vertical Box, Auto)
  Middle (Vertical Box, Fill 1)
    TopDock (Horizontal Box, Auto)
    Center (Vertical Box, Fill 1)
      Test buttons and status text
    BottomDock (Horizontal Box, Auto)
  RightDock (Vertical Box, Auto)
```

Use the exact names `LeftDock`, `RightDock`, `TopDock`, and `BottomDock`. Check **Is Variable** on each. The C++ parent binds all four by name and type. Leave the four boxes empty and Visible in the Designer. The subsystem adds the group content at runtime. The **Auto** slots let empty docks take up no space. Use a readable fixed desired size on the test panel itself, for example a root **Size Box** with Width Override 240 and Height Override 120. Compile and save the workspace.

Use `WBP_WindowingSmokeTest` as the panel class if it already has a counter or an editable field. Otherwise, create a plain User Widget for the panel with an **Editable Text Box** and a label. Open the same panel class for A, B, and C, and type different text into each instance. This makes it easy to spot a recreated widget or swapped child.

### 5.2 Add and register the workspace once

For this test, use the Level Blueprint of `/Game/empty-level`, the project's configured startup map. If that map already adds `WBP_DockWorkspace`, extend the existing creation chain. There should be one registered workspace in the session. Keep its controls visible alongside the existing floating-window controls, or put all test buttons in the workspace's **Center** box.

1. From **Event BeginPlay**, use **Get Player Controller** with Player Index `0`. On **Create Widget**, set Class to `WBP_DockWorkspace` and connect the controller to **Owning Player**.
2. Promote **Return Value** to a `DockWorkspace` variable in the Level Blueprint. On the execution chain, call **Add to Viewport** on that widget. If the controller hides the cursor, set **Show Mouse Cursor** to true and use **Set Input Mode Game And UI** so you can click the viewport controls.
3. Get **Wp Windowing Subsystem** (the Game Instance subsystem) and connect it to **Target** on **Register Dock Workspace**. Connect `DockWorkspace` to **Workspace**. On **Break Wp Window Result**, print **Code** and **Message**. Expect `Success`. The execution order is **Create Widget → Add to Viewport → Register Dock Workspace**.
4. For an explicit teardown test, call **Unregister Dock Workspace** with the same widget reference before **Remove From Parent**. End the Standalone session to exercise normal shutdown.

### 5.3 Add open buttons and store the three handles

In the workspace Graph, create `HandleA`, `HandleB`, and `HandleC` variables of type **Wp Window Handle**. Add **Open A**, **Open B**, and **Open C** buttons in **Center**. For **Open A**, wire its **On Clicked** event to **Open Widget Window** on the subsystem. Use **Get Owning Player** from the workspace for **Owning Player**, your test panel class for **Widget Class**, and **Make Wp Window Create Options** for **Options**. Set the title to `M5.1 A`, leave the placement key empty, and use a client size such as `640 × 480`. Break **Return Value** and compare **Code** with `Success`. On the true branch, set `HandleA` from **Out Handle**. Print the result on both branches. Repeat with different titles and the matching handle variables for B and C.

When debugging a handle, use **Break Wp Window Handle** to print its **Id**. Capture each ID after opening so you can compare it with the IDs reported during transfers. Open each letter once per run unless you have already closed it.

### 5.4 Wire the panel and group commands

Add these buttons in **Center** and connect each **On Clicked** event as shown. The target on every Windowing node is the same **Wp Windowing Subsystem**. Break the result of every command and print its code and message. The four A docking buttons share the same graph except for the **Side** pin.

| Button | Node and inputs |
|---|---|
| **Dock A Left**, **Dock A Right**, **Dock A Top**, **Dock A Bottom** | **Dock Window** with **Handle** = `HandleA` and **Side** = the button's edge |
| **Dock B Right** | **Dock Window** with **Handle** = `HandleB`, **Side** = Right |
| **Dock C Right** | **Dock Window** with **Handle** = `HandleC`, **Side** = Right |
| **Undock A**, **Undock B** | **Undock Window** with the corresponding handle |
| **Float A's Group** | **Undock Group** with the current **Group Id** from A |
| **Return A's Group** | **Dock Group** with the current **Group Id** from A |
| **Close A**, **Close B**, **Close C** | **Request Close Window** with the corresponding handle |

For the two group buttons, connect `HandleA` to **Get Window Presentation**, then break its output using **Break Wp Window Presentation**. For **Float A's Group**, branch on **Is Valid** and **Mode == Docked**. For **Return A's Group**, branch on **Is Valid** and **Mode == In Floating Group**. Wire **Group Id** into the group command's **Group Id** pin on the true branch. Print the current mode if the branch fails. Read the group ID when the button is clicked so the graph follows A's current membership. For the test that removes children from a floating group, use the individual **Undock A** and **Undock B** buttons.

### 5.5 Display the state and count the events

Add **Inspect A**, **Inspect B**, and **Inspect C** buttons. For each one, query **Is Window Open** and **Get Window Presentation** with the matching handle. Print the handle ID, open flag, and the presentation's **Is Valid**, **Mode**, **Side**, and **Group Id**. Read **Side** only when the presentation has a valid group ID. An individually floating panel has an empty group ID. After close, **Is Window Open** is false and the presentation is invalid.

In the workspace's **Event On Initialized**, get the subsystem and use **Bind Event to On Window Opened**, **Bind Event to On Window Closed**, and **Bind Event to On Window Presentation Changed**. Drag off each red **Event** pin to create its matching custom event. Give each event its own integer counter and increment it before printing. Use the **Handle** pin for Opened and Presentation Changed. For Closed, break **Wp Window Closed Event** to get the handle and reason. For Presentation Changed, print **Mode** and **Group Id** from the supplied **Presentation**. Start counting before opening A or B and use the buttons for mutations after the events return.

### 5.6 Run the first pass in Standalone Game

Select **Standalone Game** in the Play menu and open A and B. Type different text in both windows. Use these checkpoints before running the remaining cases in Step 6. Counts assume a fresh run in which only A and B were opened.

| Action | What to check |
|---|---|
| **Dock A Left**, then **Undock A** | Inspect A after each click. It goes from Docked on Left to Floating with an empty group ID. A's text and handle survive. The opened count is 2, the closed count is 0, and the presentation count is 2. |
| **Dock A Right**, then **Dock B Right** | Both appear in the right dock in A-then-B order. Inspect A and B and compare their matching group IDs. The presentation count is 4. |
| **Float A's Group** | Both panels move into one native **Right Docker** window. Their text and group ID survive. They report In Floating Group and the presentation count is 6. |
| **Return A's Group** | Both return to the right dock in the same order and report Docked. The opened count stays at 2, the closed count stays at 0, and the presentation count is 8. |
| **Float A's Group** again, **Open C**, **Dock C Right**, then **Return A's Group** | Return prints `DockEdgeOccupied`. A and B stay in their floating group, while C stays docked. After **Close C**, inspect C until it is closed, then retry the group return and expect `Success`. |

If **Register Dock Workspace** reports `DockWorkspaceUnavailable`, check that the widget was added to the viewport before registration. If registration succeeds but a dock command returns `DockAttachmentFailed`, inspect the named boxes in the actual workspace class used by **Create Widget**. If a new Blueprint node or struct field is missing, compile the editor target and reopen the editor so the reflected C++ changes are loaded.

## 6. Live acceptance in Standalone Game

| Action | Expected |
|---|---|
| Open A and B, move A, type into A, then dock and undock A on each edge | A keeps the same handle and text. Its individual window returns to its captured floating bounds, and B stays open. Each successful move emits a presentation event without an opened or closed event. |
| Dock A and B to Right, then undock their group | One native **Right Docker** appears. A and B keep their handles, text, group ID, and order. Each reports `InFloatingGroup`. |
| Dock the group back to Right | Both panels return together and report `Docked`. No child is recreated. |
| Float the group, dock C to Right, then try to return the original group | The group stays floating and the result is `DockEdgeOccupied`. |
| Undock a single child from a floating group | That child gets its own native window. Its siblings stay in the group. When the final child leaves, the empty group closes. |
| Close a docked panel or close the floating group's native X | Each affected logical panel receives one close event. Other groups and independently floating panels remain open. |
| Try a transfer with no workspace, or inject a candidate-registration failure in a disposable copy | The original presentation and input remain usable. No successful presentation event fires. |
| Stop Standalone with mixed docked and floating groups | All owned native hosts retire and the session shuts down cleanly. |

Build Win64 Development Editor before the live check. Afterward, rerun the [M1 acceptance suite](WindowingPlugin_M1_Tests.md) to cover the unchanged floating workflows.

To exercise a candidate rollback in a disposable copy, add these lines to `UndockWindow` immediately after the first candidate registration check and before `const bool bRestored`. Compile, dock a panel, and invoke **Undock Panel** once. Remove the two lines afterward.

```cpp
RetireDockHost(MoveTemp(Candidate), *Closed);
return DockResult(EWpWindowResultCode::NativeCreationFailed);
```
