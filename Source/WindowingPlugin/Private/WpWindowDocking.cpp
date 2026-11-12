// Developed by Neko Creative Technologies

#include "WpWindowLifetimeService.h"

#include "WpDockGroup.h"
#include "WpDockWorkspace.h"
#include "WpWindowingSubsystem.h"
#include "WpWindowRecord.h"
#include "WpWindowRuntimeEntry.h"
#include "Templates/UnrealTemplate.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/SEditableTextBox.h"

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

FWpWindowResult FWpWindowLifetimeService::RenameDockGroup(const FGuid& SessionId, FGuid GroupId, const FText& Title)
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