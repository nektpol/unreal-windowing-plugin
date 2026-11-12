// Developed by Neko Creative Technologies

#include "WpWindowLifetimeService.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "WpNativeWindowHost.h"
#include "WpWindowRecord.h"
#include "WpWindowRuntimeEntry.h"
#include "WpWindowingSubsystem.h"

namespace
{
    FWpWindowResult Result(EWpWindowResultCode Code, const TCHAR* Message = TEXT(""))
    {
        FWpWindowResult Value;
        Value.Code = Code;
        Value.Message = Message;
        return Value;
    }

    bool IsValidSize(FVector2D Size)
    {
        return FMath::IsFinite(Size.X) && FMath::IsFinite(Size.Y) &&
            Size.X > 0.0 && Size.Y > 0.0 &&
            Size.X <= MAX_int32 && Size.Y <= MAX_int32;
    }
}

FWpWindowLifetimeService::FWpWindowLifetimeService()
    : CallbackTarget(MakeShared<FCallbackTarget>())
{
    CallbackTarget->Service = this;
}

FWpWindowLifetimeService::~FWpWindowLifetimeService()
{
    check(bShuttingDown && Entries.IsEmpty());
}

void FWpWindowLifetimeService::BindSlateHooks()
{
    if (!PostTickHandle.IsValid())
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        PostTickHandle = Slate.OnPostTick().AddRaw(
            this, &FWpWindowLifetimeService::OnSlatePostTick);
        PreShutdownHandle = Slate.OnPreShutdown().AddRaw(
            this, &FWpWindowLifetimeService::Shutdown);
    }
}

void FWpWindowLifetimeService::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (auto& Pair : Entries)
    {
        Collector.AddReferencedObject(Pair.Value->Record);
    }
}

FString FWpWindowLifetimeService::GetReferencerName() const
{
    return TEXT("FWpWindowLifetimeService");
}

FWpWindowRuntimeEntry* FWpWindowLifetimeService::FindPublished(
    const FGuid& SessionId, FWpWindowHandle Handle) const
{
    const TUniquePtr<FWpWindowRuntimeEntry>* Found = Entries.Find(Handle.Id);
    if (Found && (*Found)->Record->SessionId == SessionId &&
        (*Found)->Record->bOpenedEventEmitted)
    {
        return Found->Get();
    }
    return nullptr;
}

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
    const TWeakPtr<FCallbackTarget> WeakTarget = CallbackTarget;
    Entry.Host = MakeUnique<FWpNativeWindowHost>();
    Entry.Host->Construct(Options, Content,
        FOnWindowClosed::CreateLambda([WeakTarget, Id](const TSharedRef<SWindow>&)
        {
            const TSharedPtr<FCallbackTarget> Target = WeakTarget.Pin();
            if (Target && Target->Service)
            {
                Target->Service->OnNativePreClose(Id);
            }
        }));

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

void FWpWindowLifetimeService::OnNativePreClose(FGuid Id)
{
    check(IsInGameThread());
    if (const TUniquePtr<FWpWindowRuntimeEntry>* Found = Entries.Find(Id))
    {
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

void FWpWindowLifetimeService::CloseEntry(FWpWindowRuntimeEntry& Entry,
    EWpWindowCloseReason Reason)
{
    UWpWindowRecord& Record = *Entry.Record;
    if (Record.State != EWpWindowState::Closing)
    {
        Record.State = EWpWindowState::Closing;
        Record.CloseReason = Reason;
    }
    if (Entry.Host && Entry.Host->IsTracked() &&
        !Record.bNativeDestroyRequested && !Record.bNativeCloseObserved)
    {
        Record.bNativeDestroyRequested = true;
        Entry.Host->RequestDestroy();
    }
}

void FWpWindowLifetimeService::Rollback(FGuid Id, FWpWindowRuntimeEntry& Entry)
{
    check(Entry.Record->WindowId == Id && !Entry.Record->bOpenedEventEmitted);
    Entry.bFailedCreation = true;
    Entry.Record->CloseReason = EWpWindowCloseReason::CreationRollback;
    CloseEntry(Entry, EWpWindowCloseReason::CreationRollback);
}

FWpWindowResult FWpWindowLifetimeService::RequestClose(const FGuid& SessionId,
    FWpWindowHandle Handle)
{
    if (bShuttingDown)
    {
        return Result(EWpWindowResultCode::ShuttingDown, TEXT("WindowingPlugin is shutting down."));
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
    Entry->Host->SetTitle(Title);
    return Result(EWpWindowResultCode::Success);
}

EWpWindowState FWpWindowLifetimeService::GetState(const FGuid& SessionId,
    FWpWindowHandle Handle) const
{
    const FWpWindowRuntimeEntry* Entry = FindPublished(SessionId, Handle);
    return Entry ? Entry->Record->State : EWpWindowState::Invalid;
}

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

void FWpWindowLifetimeService::OnSlatePostTick(float)
{
    Drain();
    TickPlacements();
    TickWindowFocus();
}

void FWpWindowLifetimeService::Drain()
{
    check(IsInGameThread());
    if (OperationDepth != 0)
    {
        return;
    }
    TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);

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
}

void FWpWindowLifetimeService::BeginSessionShutdown(const FGuid& SessionId)
{
    check(IsInGameThread());
    FocusObservers.Remove(SessionId);
    if (bShuttingDown)
    {
        return;
    }
    TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
    TArray<FGuid> Ids;
    Entries.GenerateKeyArray(Ids);
    for (const FGuid& Id : Ids)
    {
        FWpWindowRuntimeEntry& Entry = *Entries.FindChecked(Id);
        if (Entry.Record->SessionId == SessionId && Entry.Record->State == EWpWindowState::Open)
        {
            CloseEntry(Entry, EWpWindowCloseReason::GameInstanceShutdown);
        }
    }
}

void FWpWindowLifetimeService::Shutdown()
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
    if (OperationDepth != 0 || (!Entries.IsEmpty() && !FSlateApplication::IsInitialized()))
    {
        UE_LOG(LogTemp, Fatal, TEXT("WindowingPlugin reached shutdown at an unsafe lifetime boundary."));
    }
    bShuttingDown = true;
    FocusObservers.Reset();

    {
        TGuardValue<int32> Operation(OperationDepth, OperationDepth + 1);
        TArray<FGuid> Ids;
        Entries.GenerateKeyArray(Ids);
        for (const FGuid& Id : Ids)
        {
            CloseEntry(*Entries.FindChecked(Id), EWpWindowCloseReason::PluginShutdown);
        }
    }
    Drain();
    FlushPlacements();
    if (!Entries.IsEmpty())
    {
        UE_LOG(LogTemp, Fatal, TEXT("WindowingPlugin still owns windows after the terminal Slate drain."));
    }
    CallbackTarget->Service = nullptr;

    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        Slate.OnPostTick().Remove(PostTickHandle);
        Slate.OnPreShutdown().Remove(PreShutdownHandle);
    }
    PostTickHandle.Reset();
    PreShutdownHandle.Reset();
}

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
