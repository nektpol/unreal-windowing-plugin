# M5.2 implementation guide — headers, collapse, and Settings

This guide records the M5.2 source layout. On M5.3.0 and later, locate the same files through the [feature folder map](WindowingPlugin_M5_Docking_Roadmap.md#m530--organize-the-plugin-before-adding-layout-code).

Start from the committed M5.1 transfer code. Implement this milestone in the runtime module. Keep the current flat split and the four-edge workspace. M5.3 supplies viewport size limits, splitter resizing, and corner ownership, including Expand Right.

## 1. Add widget access for Blueprint callers

In `Public/WpWindowingSubsystem.h`, add these public methods beside `OpenWidgetWindow`. The second node exposes both outputs while existing Blueprint and C++ callers can keep using their current node.

```cpp
UFUNCTION(BlueprintPure, Category="Windowing")
UUserWidget* GetWidgetFromHandle(FWpWindowHandle Handle) const;

UFUNCTION(BlueprintCallable, Category="Windowing",
    meta=(DisplayName="Open Panel Window", DeterminesOutputType="WidgetClass", DynamicOutputParam="OutWidget"))
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

In **Open A**, swap in **Open Panel Window**, store **Out Handle** on success, then validate **Out Widget** and call a setter such as **Set Panel Label** on that instance. Use **Get Widget From Handle** to retrieve it from a later button. The open node types **Out Widget** from **Widget Class**. The lookup returns a base User Widget reference, so cast it when you need class-specific functions. Both nodes give access after widget construction. Put caller-supplied initialization in a setter or a dedicated initialization event that the caller invokes. If an opened listener immediately requests close, the window may have already left the Open state when the node returns, so the widget output can be null.

## 2. Add a reusable Slate header

Create `Private/WpDockHeader.h`. The helper keeps the content attached while collapsed. Each menu entry carries its own action, so adding a setting amounts to adding a command for the relevant attachment.

```cpp
#pragma once

#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
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

inline FSlateFontInfo WpDockSymbolFont()
{
#if PLATFORM_WINDOWS
    static const FString SymbolFont = FPaths::Combine(
        FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")), TEXT("Fonts/seguisym.ttf"));
    if (FPaths::FileExists(SymbolFont))
    {
        return FSlateFontInfo(SymbolFont, 14);
    }
#endif
    return FCoreStyle::GetDefaultFontStyle("Regular", 14);
}

inline FWpDockFrame WpBuildDockFrame(
    TAttribute<FText> Title, const TSharedRef<SWidget>& Content,
    FSimpleDelegate Close, FSimpleDelegate Toggle,
    TFunction<TArray<FWpDockMenuAction>()> GetActions)
{
    // Native Slate windows can load an existing back buffer. Paint an opaque
    // base before blending the title glyph edges into it.
    static const FSlateColorBrush HeaderBackground(FLinearColor::Black);
    const FSlateFontInfo SymbolFont = WpDockSymbolFont();
    const FComboButtonStyle& SettingsStyle =
        FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton");
    const FVector2D GearSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
        ->Measure(INVTEXT("\u2699"), SymbolFont);
    const TSharedRef<STextBlock> TitleText = SNew(STextBlock)
        .Text(Title)
        .SimpleTextMode(true)
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
        .ShadowOffset(FVector2D::ZeroVector)
        .ShadowColorAndOpacity(FLinearColor::Transparent);
    TitleText->SetPixelSnapping(EWidgetPixelSnapping::SnapToPixel);
    FWpDockFrame Frame;
    Frame.Body = SNew(SBox)[Content];
    const TWeakPtr<SBox> WeakBody = Frame.Body;
    SAssignNew(Frame.Root, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBorder)
            .BorderImage(&HeaderBackground)
            .Padding(0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[TitleText]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SComboButton)
                    .HasDownArrow(false)
                    .ToolTipText(INVTEXT("Settings"))
                    .OnGetMenuContent_Lambda([Actions = MoveTemp(GetActions)]
                    {
                        return WpBuildDockMenu(Actions());
                    })
                    .ButtonContent()
                    [
                        SNew(STextBlock).Font(SymbolFont).Text(INVTEXT("\u2699"))
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SAssignNew(Frame.Toggle, SButton)
                    .ButtonStyle(&SettingsStyle.ButtonStyle)
                    .ContentPadding(SettingsStyle.ContentPadding)
                    .ToolTipText_Lambda([WeakBody]
                    {
                        const TSharedPtr<SBox> Body = WeakBody.Pin();
                        return Body && Body->GetVisibility() == EVisibility::Collapsed
                            ? INVTEXT("Expand") : INVTEXT("Collapse");
                    })
                    .OnClicked_Lambda([Toggle] { return WpRunDockAction(Toggle); })
                    [
                        SNew(SBox).WidthOverride(GearSize.X).HAlign(HAlign_Center).VAlign(VAlign_Center)
                        [
                            SNew(STextBlock).Font(SymbolFont)
                            .Text_Lambda([WeakBody]
                            {
                                const TSharedPtr<SBox> Body = WeakBody.Pin();
                                return Body && Body->GetVisibility() == EVisibility::Collapsed
                                    ? INVTEXT("\U0001F5D6") : INVTEXT("\U0001F5D5");
                            })
                        ]
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .ButtonStyle(&SettingsStyle.ButtonStyle)
                    .ContentPadding(SettingsStyle.ContentPadding)
                    .ToolTipText(INVTEXT("Close"))
                    .OnClicked_Lambda([Close] { return WpRunDockAction(Close); })
                    [
                        SNew(SBox).WidthOverride(GearSize.X).HAlign(HAlign_Center).VAlign(VAlign_Center)
                        [
                            SNew(STextBlock).Font(SymbolFont).Text(INVTEXT("\U0001F5D9"))
                        ]
                    ]
                ]
            ]
        ]
        + SVerticalBox::Slot().FillHeight(1.f)[Frame.Body.ToSharedRef()];
    return Frame;
}
```

## 3. Wire the header into the existing hosts

### 3.1 Add storage and declare the wiring helpers

Include `WpDockHeader.h` in both `WpWindowRuntimeEntry.h` and `WpDockGroup.h`. Add these fields to the runtime entry beside `DockContentSlot`.

```cpp
bool bDockCollapsed = false;
FWpDockFrame DockFrame;
FWpDockFrame FloatingFrame;
TSharedPtr<SBox> FloatingContentSlot;
SHorizontalBox::FSlot* DockHorizontalSlot = nullptr;
SVerticalBox::FSlot* DockVerticalSlot = nullptr;
```

Add `FWpDockFrame Frame;` next to `Content` in `FWpDockGroup`. In the lifetime service header, forward declare `struct FWpDockFrame;` and add the following under `private:`. Implement them in `WpWindowDocking.cpp`, beside `MakeTransferCloseCallback`.

```cpp
FWpDockFrame BuildPanelFrame(const FGuid& SessionId, FWpWindowHandle Handle,
    const TSharedRef<SWidget>& Content, bool bIndividualHost);
FWpDockFrame BuildGroupFrame(const FGuid& SessionId, FGuid GroupId,
    const TSharedRef<SWidget>& Content);
void ShowGroupRename(const FGuid& SessionId, FGuid GroupId);
void ApplyDockPanelCollapse(FWpWindowRuntimeEntry& Entry, bool bCollapsed);
```

### 3.2 Build the panel header and its Settings menu

The frame knows whether it belongs to an individual native host or a group. Resolve the panel again on every click because a menu can outlive the presentation that opened it. The title lambda resolves the entry by ID and session in both presentations. The managed title uses the single-line Slate text paint path with pixel snapping and no text shadow. Include `Widgets/Input/SEditableTextBox.h` in `WpWindowDocking.cpp` for the rename dialog in Step 3.3.

```cpp
FWpDockFrame FWpWindowLifetimeService::BuildPanelFrame(
    const FGuid& SessionId, FWpWindowHandle Handle,
    const TSharedRef<SWidget>& Content, bool bIndividualHost)
{
    const TWeakPtr<FCallbackTarget> WeakTarget = CallbackTarget;
    const auto GetService = [WeakTarget]() -> FWpWindowLifetimeService*
    {
        const auto Target = WeakTarget.Pin();
        return Target ? Target->Service : nullptr;
    };

    const auto Title = TAttribute<FText>::CreateLambda([GetService, SessionId, Handle]
    {
        const auto* Service = GetService();
        const auto* Found = Service ? Service->Entries.Find(Handle.Id) : nullptr;
        return Found && (*Found)->Record->SessionId == SessionId
            ? (*Found)->CreateOptions.Title : FText::GetEmpty();
    });

    FWpDockFrame Frame = WpBuildDockFrame(Title, Content,
        FSimpleDelegate::CreateLambda([GetService, SessionId, Handle]
        {
            if (auto* Service = GetService())
            {
                Service->RequestClose(SessionId, Handle);
            }
        }),
        FSimpleDelegate::CreateLambda([GetService, SessionId, Handle, bIndividualHost]
        {
            auto* Service = GetService();
            auto* Entry = Service ? Service->FindPublished(SessionId, Handle) : nullptr;
            if (!Entry || Service->bShuttingDown || Service->bMovingDockPresentation ||
                Entry->Record->State != EWpWindowState::Open)
            {
                return;
            }

            if (!bIndividualHost && Entry->DockFrame.Body)
            {
                const auto* Group = Service->DockGroups.Find(Entry->DockGroupId);
                if (Group && (*Group)->Host)
                {
                    TGuardValue<int32> Operation(Service->OperationDepth, Service->OperationDepth + 1);
                    TGuardValue<bool> Transfer(Service->bMovingDockPresentation, true);
                    Service->ApplyDockPanelCollapse(*Entry, !Entry->DockFrame.IsCollapsed());
                    Service->EmitPresentationChanged(SessionId, Handle.Id);
                }
                else
                {
                    Service->SetWindowCollapsed(SessionId, Handle, !Entry->bDockCollapsed);
                }
            }
        }),
        [GetService, SessionId, Handle, bIndividualHost]
        {
            TArray<FWpDockMenuAction> Actions;
            auto* Service = GetService();
            const auto Presentation = Service
                ? Service->GetWindowPresentation(SessionId, Handle) : FWpWindowPresentation{};
            if (!Presentation.bIsValid)
            {
                return Actions;
            }

            if (!bIndividualHost && Presentation.GroupId.IsValid())
            {
                Actions.Add({INVTEXT("Undock Panel"),
                    FSimpleDelegate::CreateLambda([GetService, SessionId, Handle]
                    {
                        if (auto* Current = GetService())
                        {
                            Current->UndockWindow(SessionId, Handle);
                        }
                    })});
            }
            else if (bIndividualHost && Presentation.Mode == EWpWindowPresentationMode::Floating &&
                Service->FindDockWorkspace(SessionId))
            {
                for (const auto Side : {EWpDockSide::Left, EWpDockSide::Right,
                    EWpDockSide::Top, EWpDockSide::Bottom})
                {
                    const FText SideName = StaticEnum<EWpDockSide>()->GetDisplayNameTextByValue(
                        static_cast<int64>(Side));
                    Actions.Add({FText::Format(INVTEXT("Dock {0}"), SideName),
                        FSimpleDelegate::CreateLambda([GetService, SessionId, Handle, Side]
                        {
                            if (auto* Current = GetService())
                            {
                                Current->DockWindow(SessionId, Handle, Side);
                            }
                        })});
                }
            }
            return Actions;
        });
    Frame.Toggle->SetEnabled(!bIndividualHost);
    return Frame;
}

void FWpWindowLifetimeService::ApplyDockPanelCollapse(
    FWpWindowRuntimeEntry& Entry, bool bCollapsed)
{
    Entry.DockFrame.SetCollapsed(bCollapsed);
    if (Entry.DockHorizontalSlot)
    {
        if (bCollapsed)
        {
            Entry.DockHorizontalSlot->SetAutoWidth();
        }
        else
        {
            Entry.DockHorizontalSlot->SetFillWidth(1.f);
        }
    }
    if (Entry.DockVerticalSlot)
    {
        if (bCollapsed)
        {
            Entry.DockVerticalSlot->SetAutoHeight();
        }
        else
        {
            Entry.DockVerticalSlot->SetFillHeight(1.f);
        }
    }
}
```

### 3.3 Build the group header and wire Rename

Declare `RenameDockGroup` as shown in Step 3.6. The group Settings menu chooses Float or Return when it opens. Every action resolves the group again when clicked. The rename dialog uses a weak window reference so its text callback can close it without forming an ownership cycle.

```cpp
FWpDockFrame FWpWindowLifetimeService::BuildGroupFrame(
    const FGuid& SessionId, FGuid GroupId, const TSharedRef<SWidget>& Content)
{
    const TWeakPtr<FCallbackTarget> WeakTarget = CallbackTarget;
    const auto WithGroup = [WeakTarget, SessionId, GroupId](
        TFunctionRef<void(FWpWindowLifetimeService&, FWpDockGroup&)> Action)
    {
        const auto Target = WeakTarget.Pin();
        auto* Service = Target ? Target->Service : nullptr;
        const auto* Found = Service ? Service->DockGroups.Find(GroupId) : nullptr;
        if (Found && (*Found)->SessionId == SessionId)
        {
            const auto Group = *Found;
            Action(*Service, *Group);
        }
    };

    FWpDockFrame Frame = WpBuildDockFrame(TAttribute<FText>::CreateLambda([WithGroup]
        {
            FText Title;
            WithGroup([&](auto&, const auto& Group) { Title = Group.Title; });
            return Title;
        }), Content,
        FSimpleDelegate::CreateLambda([WithGroup]
        {
            WithGroup([](auto& Service, auto& Group)
            {
                if (!Service.bMovingDockPresentation && Service.CanMoveGroup(Group))
                {
                    TGuardValue<int32> Operation(Service.OperationDepth, Service.OperationDepth + 1);
                    Service.CloseGroupChildren(Group, EWpWindowCloseReason::HostRequest);
                }
            });
        }),
        FSimpleDelegate::CreateLambda([WithGroup]
        {
            WithGroup([](auto& Service, auto& Group)
            {
                if (!Service.bMovingDockPresentation && Service.CanMoveGroup(Group) &&
                    (!Group.Host || !Group.Host->IsMaximized()))
                {
                    TGuardValue<int32> Operation(Service.OperationDepth, Service.OperationDepth + 1);
                    TGuardValue<bool> Transfer(Service.bMovingDockPresentation, true);
                    Group.Frame.SetCollapsed(!Group.Frame.IsCollapsed());
                }
            });
        }),
        [WithGroup, SessionId, GroupId]
        {
            TArray<FWpDockMenuAction> Actions;
            WithGroup([&](auto& Service, const auto& Group)
            {
                const bool bFloat = !Group.Host.IsValid();
                if (bFloat || Service.FindDockWorkspace(SessionId))
                {
                    Actions.Add({bFloat ? INVTEXT("Float Group") : INVTEXT("Return Group"),
                        FSimpleDelegate::CreateLambda([WithGroup, SessionId, GroupId, bFloat]
                        {
                            WithGroup([&](auto& Current, auto&)
                            {
                                Current.MoveGroup(SessionId, GroupId, bFloat);
                            });
                        })});
                }
                Actions.Add({INVTEXT("Rename"),
                    FSimpleDelegate::CreateLambda([WithGroup, SessionId, GroupId]
                    {
                        WithGroup([&](auto& Current, auto&)
                        {
                            Current.ShowGroupRename(SessionId, GroupId);
                        });
                    })});
            });
            return Actions;
        });
    Frame.Toggle->SetEnabled(TAttribute<bool>::CreateLambda([WithGroup]
    {
        bool bCanCollapse = false;
        WithGroup([&](auto&, const auto& Group)
        {
            bCanCollapse = !Group.Host || !Group.Host->IsMaximized();
        });
        return bCanCollapse;
    }));
    return Frame;
}

void FWpWindowLifetimeService::ShowGroupRename(const FGuid& SessionId, FGuid GroupId)
{
    const auto* Found = DockGroups.Find(GroupId);
    if (bMovingDockPresentation || !Found || (*Found)->SessionId != SessionId ||
        !CanMoveGroup(*(*Found)))
    {
        return;
    }

    const auto Dialog = SNew(SWindow)
        .Title(INVTEXT("Rename Dock Group"))
        .ClientSize(FVector2D(320, 52))
        .SupportsMaximize(false)
        .SupportsMinimize(false);
    const TWeakPtr<SWindow> WeakDialog = Dialog;
    const TWeakPtr<FCallbackTarget> WeakTarget = CallbackTarget;
    const auto TextBox = SNew(SEditableTextBox)
        .Text((*Found)->Title)
        .SelectAllTextWhenFocused(true)
        .OnTextCommitted_Lambda([WeakDialog, WeakTarget, SessionId, GroupId](
            const FText& Value, ETextCommit::Type Commit)
        {
            const auto Target = WeakTarget.Pin();
            if (Commit == ETextCommit::OnEnter && Target && Target->Service &&
                Target->Service->RenameDockGroup(SessionId, GroupId, Value).IsSuccess())
            {
                if (const auto Window = WeakDialog.Pin())
                {
                    Window->RequestDestroyWindow();
                }
            }
        });
    Dialog->SetContent(TextBox);
    Dialog->SetWidgetToFocusOnActivate(TextBox);
    FSlateApplication::Get().AddWindow(Dialog);
}
```

Press Enter to save a valid name. The dialog's native close button cancels. A group that closes while the dialog is open fails the lookup when Enter is pressed.

### 3.4 Mount the frames on initial open and dock

This step wires three layers. On open, the native window displays the panel frame. On dock, the viewport slot displays the group frame, and the split inside that frame holds the docked panel frames.

#### Replace CreateWindow in WpWindowLifetimeService.cpp

Replace the entire existing `FWpWindowLifetimeService::CreateWindow` definition. The floating window mounts the panel frame using the internal handle ID. The service publishes `OutHandle` after registration and show. Include `Widgets/Layout/SBox.h` in this file for the floating content slot.

```cpp
FWpWindowResult FWpWindowLifetimeService::CreateWindow(const FGuid& SessionId,
    UGameInstance* Owner, UWpWindowingSubsystem* Sink,
    TSubclassOf<UUserWidget> WidgetClass, const FWpWindowCreateOptions& Options,
    APlayerController* Player, FWpWindowHandle& OutHandle)
{
    OutHandle = {};
    check(IsInGameThread());
    const FGuid RequestSession = SessionId;
    const TWeakObjectPtr<UWpWindowingSubsystem> WeakSink(Sink);
    const auto IsSessionActive = [this, RequestSession, WeakSink]()
    {
        const UWpWindowingSubsystem* Current = WeakSink.Get();
        return !bShuttingDown && !IsEngineExitRequested() && Current &&
            !Current->bDeinitializing && Current->SessionId == RequestSession &&
            RequestSession.IsValid();
    };
    if (!IsSessionActive())
    {
        return Result(EWpWindowResultCode::ShuttingDown, TEXT("Window session is ending."));
    }
#if !PLATFORM_WINDOWS
    return Result(EWpWindowResultCode::UnsupportedPlatform, TEXT("M0 supports Windows."));
#endif
    if (IsRunningCommandlet() || !FApp::CanEverRender() ||
        !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
    {
        return Result(EWpWindowResultCode::SlateUnavailable, TEXT("Desktop Slate is unavailable."));
    }
    if (!IsValid(Owner) ||
        (Player && (!IsValid(Player) || !Player->IsLocalController() ||
            Player->GetGameInstance() != Owner)))
    {
        return Result(EWpWindowResultCode::InvalidOwningContext, TEXT("Invalid widget owner."));
    }
    UClass* Class = WidgetClass.Get();
    if (!IsValid(Class) || !Class->IsChildOf(UUserWidget::StaticClass()) ||
        Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        return Result(EWpWindowResultCode::InvalidWidgetClass, TEXT("Use a concrete UUserWidget class."));
    }
    if (!IsValidSize(Options.InitialClientSize) || !IsValidSize(Options.MinimumClientSize) ||
        Options.MinimumClientSize.X > Options.InitialClientSize.X ||
        Options.MinimumClientSize.Y > Options.InitialClientSize.Y)
    {
        return Result(EWpWindowResultCode::InvalidSize, TEXT("Use positive client sizes with minimum <= initial."));
    }

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

    BindSlateHooks();
    TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
    FGuid Id;
    do
    {
        Id = FGuid::NewGuid();
    }
    while (Entries.Contains(Id));

    auto NewEntry = MakeUnique<FWpWindowRuntimeEntry>();
    FWpWindowRuntimeEntry& Entry = *NewEntry;
    Entry.Record = NewObject<UWpWindowRecord>(GetTransientPackage());
    Entry.Record->WindowId = Id;
    Entry.Record->SessionId = RequestSession;
    Entry.Record->SessionOwner = Owner;
    Entry.EventSink = WeakSink;
    Entry.PlacementId = MoveTemp(PlacementId);
    Entry.PlacementMinimumClientSize = Options.MinimumClientSize;
    Entries.Add(Id, MoveTemp(NewEntry));

    const auto Fail = [this, Id, &Entry](EWpWindowResultCode Code, const TCHAR* Message)
    {
        Rollback(Id, Entry);
        return Result(Code, Message);
    };
    Entry.Record->Widget = Player
        ? CreateWidget<UUserWidget>(Player, WidgetClass)
        : CreateWidget<UUserWidget>(Owner, WidgetClass);
    if (!IsValid(Entry.Record->Widget.Get()))
    {
        return Fail(EWpWindowResultCode::WidgetCreationFailed, TEXT("Widget creation failed."));
    }
    if (!IsSessionActive())
    {
        return Fail(EWpWindowResultCode::ShuttingDown, TEXT("Session ended during widget creation."));
    }

    const TSharedRef<SWidget> Content = Entry.Record->Widget->TakeWidget();
    if (!IsSessionActive())
    {
        return Fail(EWpWindowResultCode::ShuttingDown, TEXT("Session ended during widget construction."));
    }
    Entry.RetainedContent = Content;
    Entry.CreateOptions = Options;
    Entry.FloatingContentSlot = SNew(SBox)[Content];
    FWpWindowHandle FrameHandle;
    FrameHandle.Id = Id;
    Entry.FloatingFrame = BuildPanelFrame(SessionId, FrameHandle, Entry.FloatingContentSlot.ToSharedRef(), true);
    Entry.Host = MakeUnique<FWpNativeWindowHost>();
    Entry.Host->Construct(Options, Entry.FloatingFrame.Root.ToSharedRef(), MakeTransferCloseCallback(Id, false));

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
    if (!Entry.Record->bNativeCloseObserved)
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
    if (!IsSessionActive())
    {
        return Fail(EWpWindowResultCode::ShuttingDown, TEXT("Session ended while opening the window."));
    }

    Entry.Record->State = EWpWindowState::Open;
    Entry.Record->bOpenedEventEmitted = true;
    CapturePlacement(Entry);
    OutHandle.Id = Id;
    FocusObservers.Add(RequestSession, WeakSink);
    WeakSink.Get()->NotifyOpened(OutHandle);
    return Result(EWpWindowResultCode::Success);
}
```

#### Replace DockWindow in WpWindowDocking.cpp

Replace the entire existing `FWpWindowLifetimeService::DockWindow` definition. The workspace mounts the group frame, the split inside that frame holds each docked panel frame, and each panel frame holds the existing widget. Follow Step 3.5 for the matching undock and cleanup code before running the project.

```cpp
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
            NewGroup->Frame = BuildGroupFrame(SessionId, NewGroup->Id, NewGroup->Content.ToSharedRef());

            UNativeWidgetHost* Slot = Workspace->AddDockSlot(Side);
            if (!Slot || FindDockWorkspace(SessionId) != Workspace ||
                Entry->Record->State != EWpWindowState::Open)
            {
                ClearSlot(Slot);
                return DockResult(EWpWindowResultCode::DockAttachmentFailed);
            }
            NewGroup->Slot = Slot;
            Slot->SetContent(NewGroup->Frame.Root.ToSharedRef());
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
        Entry->FloatingContentSlot->SetContent(SNullWidget::NullWidget);
        Entry->Host->SetContent(SNullWidget::NullWidget);
        Entry->FloatingFrame = {};
        Entry->FloatingContentSlot.Reset();

        const TSharedRef<SBox> Slot = SNew(SBox)[Entry->RetainedContent.ToSharedRef()];
        Entry->DockContentSlot = Slot;
        Entry->DockFrame = BuildPanelFrame(SessionId, Handle, Slot, false);
        if (IsHorizontal(Side))
        {
            StaticCastSharedPtr<SHorizontalBox>(Group->Content)->AddSlot()
                .Expose(Entry->DockHorizontalSlot)
                .FillWidth(1.f)[Entry->DockFrame.Root.ToSharedRef()];
        }
        else
        {
            StaticCastSharedPtr<SVerticalBox>(Group->Content)->AddSlot()
                .Expose(Entry->DockVerticalSlot)
                .FillHeight(1.f)[Entry->DockFrame.Root.ToSharedRef()];
        }
        ApplyDockPanelCollapse(*Entry, Entry->bDockCollapsed);
        Entry->DockGroupId = Group->Id;
        Group->Children.Add(Handle.Id);
        RetireDockHost(MoveTemp(Entry->Host));
    }
    EmitPresentationChanged(SessionId, Handle.Id);
    return DockResult(EWpWindowResultCode::Success);
}
```

### 3.5 Replace the undock and cleanup functions

Replace the full definitions below. These versions start from the committed M5.1 lifecycle and include the M5.2 frame changes. Complete Step 4 before building.

#### Replace RemoveGroupMember in WpWindowDocking.cpp

Clear the original content box, remove the panel frame from the split, and clear the slot pointer. The saved docked collapse preference stays with the panel.

```cpp
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
                StaticCastSharedPtr<SHorizontalBox>(Group->Content)->RemoveSlot(Entry.DockFrame.Root.ToSharedRef());
            }
            else
            {
                StaticCastSharedPtr<SVerticalBox>(Group->Content)->RemoveSlot(Entry.DockFrame.Root.ToSharedRef());
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
    Entry.DockHorizontalSlot = nullptr;
    Entry.DockVerticalSlot = nullptr;
    Entry.DockFrame = {};
    Entry.DockContentSlot.Reset();
    Entry.DockGroupId.Invalidate();
}
```

#### Replace UndockWindow in WpWindowDocking.cpp

Mount a new floating frame after native registration. If show fails, detach the widget from the candidate and put it back in the original dock box.

```cpp
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
        const TSharedRef<SBox> FloatingSlot = SNew(SBox)[Entry->RetainedContent.ToSharedRef()];
        FWpDockFrame FloatingFrame = BuildPanelFrame(SessionId, Handle, FloatingSlot, true);
        Candidate->SetContent(FloatingFrame.Root.ToSharedRef());
        const bool bShown = Candidate->ShowWindow();
        if (bShown && bRestored && Entry->Placement.bMaximized && !*Closed)
        {
            Candidate->MaximizeWindow();
        }
        if (!bShown || *Closed || Entry->Record->State != EWpWindowState::Open)
        {
            RetireDockHost(MoveTemp(Candidate), *Closed);
            FloatingSlot->SetContent(SNullWidget::NullWidget);
            Entry->DockContentSlot->SetContent(Entry->RetainedContent.ToSharedRef());
            return DockResult(EWpWindowResultCode::NativeCreationFailed);
        }
        Entry->Host = MoveTemp(Candidate);
        Entry->FloatingContentSlot = FloatingSlot;
        Entry->FloatingFrame = MoveTemp(FloatingFrame);
        Entry->Record->bNativeCloseObserved = false;
        Entry->Record->bNativeDestroyRequested = false;
        RemoveGroupMember(*Entry);
        CapturePlacement(*Entry);
    }
    EmitPresentationChanged(SessionId, Handle.Id);
    return DockResult(EWpWindowResultCode::Success);
}
```

#### Replace MoveGroup in WpWindowDocking.cpp

Move the whole group frame. Update every child frame before notifying listeners after a successful transfer.

```cpp
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
        Candidate->SetContent(Group.Frame.Root.ToSharedRef());
        if (!Candidate->ShowWindow() || *Closed || !CanMoveGroup(Group) ||
            FindDockWorkspace(SessionId) != Workspace)
        {
            RetireDockHost(MoveTemp(Candidate), *Closed);
            if (UNativeWidgetHost* Slot = Group.Slot.Get())
            {
                Slot->SetContent(Group.Frame.Root.ToSharedRef());
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
        Slot->SetContent(Group.Frame.Root.ToSharedRef());
        Group.Slot = Slot;
        RetireDockHost(MoveTemp(Group.Host));
    }

    for (const FGuid& Child : Group.Children)
    {
        if (auto* ChildEntry = Entries.Find(Child))
        {
            ApplyDockPanelCollapse(*(*ChildEntry), !bFloat && (*ChildEntry)->bDockCollapsed);
        }
    }

    // Keep transfers blocked until every member has received the committed presentation.
    for (const FGuid& Child : Group.Children)
    {
        EmitPresentationChanged(SessionId, Child);
    }
    return DockResult(EWpWindowResultCode::Success);
}
```

#### Replace Drain in WpWindowLifetimeService.cpp

Release the floating frame and its content box before clearing the retained widget references.

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
        Entry.FloatingFrame = {};
        Entry.FloatingContentSlot.Reset();
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

### 3.6 Apply collapse and rename

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

Use `ApplyDockPanelCollapse` from Step 3.2 for every panel body change, including restoring preferences after a whole-group transfer. It switches collapsed vertical slots to Auto Height and horizontal slots to Auto Width. Expanding restores Fill on the split axis.

## 4. Expose panel collapse

In `Public/WpWindowTypes.h`, add this field inside `FWpWindowPresentation`.

```cpp
UPROPERTY(BlueprintReadOnly, Category="Windowing")
bool bCollapsed = false;
```

Replace the full `FWpWindowLifetimeService::GetWindowPresentation` definition in `WpWindowDocking.cpp`. It reports the panel body's own state. The group frame controls the visibility of the whole group independently.

```cpp
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
    const FWpDockFrame& Frame = Entry->DockGroupId.IsValid() ? Entry->DockFrame : Entry->FloatingFrame;
    Presentation.bCollapsed = Frame.Body && Frame.IsCollapsed();
    if (const auto* Group = DockGroups.Find(Entry->DockGroupId))
    {
        Presentation.Mode = (*Group)->Host ? EWpWindowPresentationMode::InFloatingGroup
            : EWpWindowPresentationMode::Docked;
        Presentation.Side = (*Group)->Side;
        Presentation.GroupId = (*Group)->Id;
    }
    return Presentation;
}
```

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
        ApplyDockPanelCollapse(*Entry, bCollapsed);
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

The public setter changes the docked preference. Individually floating panels have Collapse disabled. A child within a floating group can still collapse through its own header. The group transfer restores the docked preferences when the group returns to the workspace.

### Native window behavior

The native host wraps its client content in an opaque background so hidden panel bodies leave no stale pixels in the floating window. See `FWpNativeWindowHost::Construct` and `SetContent` in `WpNativeWindowHost.cpp`. `OnSlatePostTick` in `WpWindowLifetimeService.cpp` expands a floating group when its native host reports it is maximized. Its group Collapse button stays disabled until the native window is restored.

## 5. Verify in Standalone Game

Use the Editor walkthrough for exact Blueprint wiring and click-by-click checks. [M5.2 Editor test guide](WindowingPlugin_M5_2_Editor_Test_Guide.md)

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
