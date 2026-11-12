# M5.2 implementation guide — headers, collapse, and Settings

Start from the committed M5.1 transfer code. Implement this milestone in the runtime module. Keep the current flat split and the four-edge workspace. M5.3 supplies viewport size limits, splitter resizing, and corner ownership, including Expand Right.

## 1. Add widget access for Blueprint callers

In `Public/WpWindowingSubsystem.h`, add these public methods beside `OpenWidgetWindow`. The second node exposes both outputs while existing Blueprint and C++ callers can keep using their current node.

```cpp
UFUNCTION(BlueprintPure, Category="Windowing")
UUserWidget* GetWidgetFromHandle(FWpWindowHandle Handle) const;

UFUNCTION(BlueprintCallable, Category="Windowing")
FWpWindowResult OpenWidgetWindowWithWidget(
    TSubclassOf<UUserWidget> WidgetClass, const FWpWindowCreateOptions& Options,
    APlayerController* OwningPlayer, FWpWindowHandle& OutHandle, UUserWidget*& OutWidget);
```

Add this declaration to the public section of `Private/WpWindowLifetimeService.h`.

```cpp
UUserWidget* GetWidgetFromHandle(const FGuid& SessionId, FWpWindowHandle Handle) const;
```

Append this to `Private/WpWindowLifetimeService.cpp`.

```cpp
UUserWidget* FWpWindowLifetimeService::GetWidgetFromHandle(
    const FGuid& SessionId, FWpWindowHandle Handle) const
{
    const FWpWindowRuntimeEntry* Entry = FindPublished(SessionId, Handle);
    if (Entry && Entry->Record->State == EWpWindowState::Open)
    {
        return Entry->Record->Widget.Get();
    }
    return nullptr;
}
```

Append these to `Private/WpWindowingSubsystem.cpp`.

```cpp
UUserWidget* UWpWindowingSubsystem::GetWidgetFromHandle(FWpWindowHandle Handle) const
{
    if (IsInGameThread() && !bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
    {
        if (const FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
        {
            return Service->GetWidgetFromHandle(SessionId, Handle);
        }
    }
    return nullptr;
}

FWpWindowResult UWpWindowingSubsystem::OpenWidgetWindowWithWidget(
    TSubclassOf<UUserWidget> WidgetClass, const FWpWindowCreateOptions& Options,
    APlayerController* OwningPlayer, FWpWindowHandle& OutHandle, UUserWidget*& OutWidget)
{
    OutWidget = nullptr;
    const FWpWindowResult Result = OpenWidgetWindow(WidgetClass, Options, OwningPlayer, OutHandle);
    if (Result.IsSuccess())
    {
        OutWidget = GetWidgetFromHandle(OutHandle);
    }
    return Result;
}
```

In **Open A**, swap in **Open Widget Window With Widget**, store **Out Handle** on success, then validate **Out Widget** and cast it to your panel Blueprint. Call a setter such as **Set Panel Label** on that instance. Use **Get Widget From Handle** to retrieve it from a later button. Both nodes give access after widget construction. Put caller-supplied initialization in a setter or a dedicated initialization event that the caller invokes. If an opened listener immediately requests close, the window may have already left the Open state when the node returns, so the widget output can be null.

## 2. Add a reusable Slate header

Create `Private/WpDockHeader.h`. The helper keeps the content attached while collapsed. Each menu entry carries its own action, so adding a setting amounts to adding a command for the relevant attachment.

```cpp
#pragma once

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

struct FWpDockMenuAction
{
    FText Label;
    FSimpleDelegate Execute;
};

inline FReply WpRunDockAction(FSimpleDelegate Action)
{
    Action.ExecuteIfBound();
    return FReply::Handled();
}

inline TSharedRef<SWidget> WpBuildDockMenu(const TArray<FWpDockMenuAction>& Actions)
{
    const TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
    for (const FWpDockMenuAction& Action : Actions)
    {
        Menu->AddSlot().AutoHeight()
        [
            SNew(SButton).Text(Action.Label)
            .OnClicked_Lambda([Execute = Action.Execute]
            {
                FSlateApplication::Get().DismissAllMenus();
                return WpRunDockAction(Execute);
            })
        ];
    }
    return Menu;
}

struct FWpDockFrame
{
    TSharedPtr<SVerticalBox> Root;
    TSharedPtr<SBox> Body;
    TSharedPtr<SButton> Toggle;

    bool IsCollapsed() const
    {
        return Body->GetVisibility() == EVisibility::Collapsed;
    }

    void SetCollapsed(bool bCollapsed) const
    {
        if (bCollapsed && (Body->HasKeyboardFocus() || Body->HasFocusedDescendants()))
        {
            FSlateApplication::Get().SetKeyboardFocus(Toggle, EFocusCause::SetDirectly);
        }
        Body->SetVisibility(bCollapsed ? EVisibility::Collapsed : EVisibility::Visible);
    }
};

inline FWpDockFrame WpBuildDockFrame(
    TAttribute<FText> Title, const TSharedRef<SWidget>& Content,
    FSimpleDelegate Close, FSimpleDelegate Toggle,
    TFunction<TArray<FWpDockMenuAction>()> GetActions)
{
    FWpDockFrame Frame;
    Frame.Body = SNew(SBox)[Content];
    const TWeakPtr<SBox> WeakBody = Frame.Body;
    SAssignNew(Frame.Root, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)[SNew(STextBlock).Text(Title)]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SButton).Text(INVTEXT("Close"))
                .OnClicked_Lambda([Close] { return WpRunDockAction(Close); })
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SAssignNew(Frame.Toggle, SButton)
                .Text_Lambda([WeakBody]
                {
                    const TSharedPtr<SBox> Body = WeakBody.Pin();
                    return Body && Body->GetVisibility() == EVisibility::Collapsed
                        ? INVTEXT("Expand") : INVTEXT("Collapse");
                })
                .OnClicked_Lambda([Toggle] { return WpRunDockAction(Toggle); })
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SComboButton)
                .OnGetMenuContent_Lambda([Actions = MoveTemp(GetActions)]
                {
                    return WpBuildDockMenu(Actions());
                })
                .ButtonContent()[SNew(STextBlock).Text(INVTEXT("Settings"))]
            ]
        ]
        + SVerticalBox::Slot().FillHeight(1.f)[Frame.Body.ToSharedRef()];
    return Frame;
}
```

## 3. Wire the header into the existing hosts

In `WpWindowRuntimeEntry.h`, add `bool bDockCollapsed = false;` and a `FWpDockFrame DockFrame;`. Include `WpDockHeader.h`. In `WpDockGroup.h`, include the same header and add `FWpDockFrame Frame;`. Keep `Group.Content` as the split and `DockContentSlot` as the transfer box inside each panel frame.

1. In `DockWindow`, after creating the panel's `SBox`, build its frame with the box as the content. Insert `DockFrame.Root` into the split where the original code inserted `Slot`. Apply the saved collapse state after the frame is built. In `RemoveGroupMember`, remove `DockFrame.Root` from the split after clearing the transfer box and reset `DockFrame = {};`. Leave `bDockCollapsed` on the entry so individual redocking can restore it.
2. In `DockWindow` where a group is created, build its frame after creating `NewGroup->Content`. Use that split as the frame's content. Attach `NewGroup->Frame.Root` to the new dock slot. In `MoveGroup`, use `Group.Frame.Root` for every attach, including the rollback path. Keep the split as the insertion and removal target for children. The group frame travels with the group.
3. On each panel frame, bind **Close** to `RequestClose(SessionId, Handle)` and **Collapse/Expand** to `SetWindowCollapsed(SessionId, Handle, !Entry->bDockCollapsed)` after re-looking up the live entry. In each group frame, use `CloseGroupChildren(Group, EWpWindowCloseReason::HostRequest)` after checking its session and `CanMoveGroup`. Toggle the group frame through `Group.Frame.SetCollapsed(!Group.Frame.IsCollapsed())`. If focus is inside the body, the frame helper moves it to the corresponding toggle.
4. Bind the frame title to the current stored title with a Slate attribute. Use `Entry.CreateOptions.Title` for a panel and `Group.Title` for a group. Build button and menu delegates with the same weak `FCallbackTarget` pattern used by `MakeTransferCloseCallback`. Capture IDs by value. Pin the weak target and look up the current entry or group at invocation time. Give title attributes an empty text fallback after the target retires.
5. In the panel Settings provider, query `GetWindowPresentation` at menu open. Return **Undock Panel** for grouped panels, routed through `UndockWindow`. For individually floating panels, expose **Dock Left**, **Dock Right**, **Dock Top**, and **Dock Bottom** when a workspace is registered. To mount a header there, add a separate frame owned by the entry for the individual native host. Create it around an empty transfer box before showing the native host, move `RetainedContent` into that box, and clear that box on transfer and rollback. The native host can continue to provide Windows chrome. Allow the floating panel's **Collapse** control to expand or collapse the floating body locally. Keep that state separate from `bDockCollapsed`.
6. In the group Settings provider, use `Group.Host` to choose **Float Group** (`UndockGroup`) or **Return Group** (`DockGroup`). Add **Rename** in both states. Use an `SEditableTextBox` in a Slate popup for the group name. On Enter, call the function below through the weak callback target. Dismiss the popup after success. The title attribute refreshes the managed header.

Declare `FWpWindowResult RenameDockGroup(const FGuid& SessionId, FGuid GroupId, const FText& Title);` in the service header and add this to `WpWindowDocking.cpp`.

```cpp
FWpWindowResult FWpWindowLifetimeService::RenameDockGroup(
    const FGuid& SessionId, FGuid GroupId, const FText& Title)
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
    const FString Name = Title.ToString().TrimStartAndEnd();
    if (!CanMoveGroup(Group) || Name.IsEmpty())
    {
        return DockResult(EWpWindowResultCode::InvalidState);
    }
    TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
    Group.Title = FText::FromString(Name);
    if (Group.Host)
    {
        Group.Host->SetTitle(Group.Title);
    }
    return DockResult(EWpWindowResultCode::Success);
}
```

For a vertical group, switch each collapsed child's outer vertical slot to Auto Height so it shrinks to its header. Restore Fill Height when it expands. A child in a horizontal group keeps its column width so the header remains reachable. Apply the same slot updates when restoring preferences after a whole-group transfer.

## 4. Expose panel collapse

Append `UPROPERTY(BlueprintReadOnly, Category="Windowing") bool bCollapsed = false;` to `FWpWindowPresentation`. In `GetWindowPresentation`, inside the group branch after setting `Presentation.GroupId`, set `Presentation.bCollapsed = Entry->bDockCollapsed;`. This describes the panel's own body state. The group frame controls visibility of the entire group independently.

Declare `FWpWindowResult SetWindowCollapsed(const FGuid& SessionId, FWpWindowHandle Handle, bool bCollapsed);` in the public section of the lifetime service. Add this implementation to `WpWindowDocking.cpp`, where `DockResult` is already defined.

```cpp
FWpWindowResult FWpWindowLifetimeService::SetWindowCollapsed(
    const FGuid& SessionId, FWpWindowHandle Handle, bool bCollapsed)
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
    const auto* Group = DockGroups.Find(Entry->DockGroupId);
    if (bShuttingDown || Entry->Record->State != EWpWindowState::Open ||
        !Group || (*Group)->Host || !Entry->DockFrame.Body)
    {
        return DockResult(EWpWindowResultCode::InvalidState);
    }
    if (Entry->bDockCollapsed != bCollapsed)
    {
        TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
        TGuardValue<bool> Transfer(bMovingDockPresentation, true);
        Entry->bDockCollapsed = bCollapsed;
        Entry->DockFrame.SetCollapsed(bCollapsed);
        EmitPresentationChanged(SessionId, Handle.Id);
    }
    return DockResult(EWpWindowResultCode::Success);
}
```

In the public section of the subsystem header, add the declaration below.

```cpp
UFUNCTION(BlueprintCallable, Category="Windowing")
FWpWindowResult SetWindowCollapsed(FWpWindowHandle Handle, bool bCollapsed);
```

Append this to the subsystem implementation.

```cpp
FWpWindowResult UWpWindowingSubsystem::SetWindowCollapsed(
    FWpWindowHandle Handle, bool bCollapsed)
{
    if (!IsInGameThread())
    {
        return WpWindowingSubsystemPrivate::WrongThread();
    }
    if (!bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
    {
        if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
        {
            return Service->SetWindowCollapsed(SessionId, Handle, bCollapsed);
        }
    }
    return WpWindowingSubsystemPrivate::Unavailable();
}
```

When floating a whole group, show its child bodies and retain their docked preferences in the entries. Restore those preferences when the group docks again. Apply the change only after `MoveGroup` commits, before emitting the children's presentation events. For panel header toggles in a floating group, change the frame body directly through a guarded service operation and keep the docked preference intact. When `GetWindowPresentation` reports a floating-group child, read the live frame body visibility for `bCollapsed`. Keep the existing native close and rollback flows intact.

## 5. Verify in Standalone Game

| Action | Expected result |
|---|---|
| Open A with the new node and set its label through Out Widget | The panel displays the label. A lookup by its handle returns the same instance. |
| Dock A, type into its field, focus that field and collapse A | The header remains usable, the body consumes no space, and keyboard input leaves the hidden field. |
| Expand A, then repeat the same collapse state through Blueprint | Original text returns. Each real change emits one presentation event. A repeated value emits zero. |
| Undock collapsed A and redock it | The individual floating body is visible. The original docked collapse preference returns when redocked. |
| Dock A and B together, collapse and expand the group, rename it, then float and return it | Both handles and widget instances survive, the group title follows its host, and the child order stays the same. |
| Open Settings before and after moving a group | The menu switches between Float Group and Return Group. Rename remains available. |
| Close A and inspect its old handle | Widget lookup returns null. B remains open. |

The code blocks are implementation starting points for M5.2. Compile and run the completed host and Settings wiring before marking the milestone complete.
