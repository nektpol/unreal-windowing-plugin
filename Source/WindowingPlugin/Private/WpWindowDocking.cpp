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