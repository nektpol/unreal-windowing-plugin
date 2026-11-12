// Developed by Neko Creative Technologies

#include "WpWindowingSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "WindowingPlugin.h"
#include "Lifecycle/WpWindowLifetimeService.h"
#include "WpDockWorkspace.h"

static FWpWindowLifetimeService* WpGetWindowLifetimeService()
{
	FWindowingPluginModule* Module =
		FModuleManager::GetModulePtr<FWindowingPluginModule>(TEXT("WindowingPlugin"));
	return Module ? Module->GetWindowLifetimeService() : nullptr;
}

namespace WpWindowingSubsystemPrivate
{
	FWpWindowResult MakeResult(EWpWindowResultCode Code, const TCHAR* Message)
	{
		FWpWindowResult Result;
		Result.Code = Code;
		Result.Message = Message;
		return Result;
	}

	FWpWindowResult WrongThread()
	{
		return MakeResult(EWpWindowResultCode::WrongThread,TEXT("Windowing operations must be called on the game thread."));
	}

	FWpWindowResult Unavailable()
	{
		return MakeResult(EWpWindowResultCode::ShuttingDown,TEXT("The WindowingPlugin lifetime service is unavailable or shutting down."));
	}
}

void UWpWindowingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SessionId = FGuid::NewGuid();
	bDeinitializing = false;
	LastReportedFocus = {};
}

void UWpWindowingSubsystem::Deinitialize()
{
	// Mark inactive before requesting closure: callbacks must not broadcast into
	// a deinitializing GameInstance. A late native close is owned by the service.
	bDeinitializing = true;

	if (IsInGameThread() && SessionId.IsValid())
	{
		if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
		{
			Service->BeginSessionShutdown(SessionId);
		}
	}

	SessionId.Invalidate();
	Super::Deinitialize();
}

FWpWindowResult UWpWindowingSubsystem::OpenWidgetWindow(TSubclassOf<UUserWidget> WidgetClass, const FWpWindowCreateOptions& Options,
	APlayerController* OwningPlayer, FWpWindowHandle& OutHandle)
{
	// On every failure, callers get an invalid handle. The service publishes
	// a valid handle only after native registration succeeds.
	OutHandle = FWpWindowHandle{};

	if (!IsInGameThread())
	{
		return WpWindowingSubsystemPrivate::WrongThread();
	}

	if (bDeinitializing || !SessionId.IsValid())
	{
		return WpWindowingSubsystemPrivate::Unavailable();
	}

	UGameInstance* const GameInstance = GetGameInstance();
	if (!IsValid(GameInstance))
	{
		return WpWindowingSubsystemPrivate::MakeResult(
			EWpWindowResultCode::InvalidOwningContext,
			TEXT("The windowing subsystem has no valid GameInstance."));
	}

	// The null-controller case is valid: the service creates the widget using
	// the GameInstance. A non-null controller must belong to the same session.
	if (OwningPlayer != nullptr &&
		(!IsValid(OwningPlayer) || OwningPlayer->GetGameInstance() != GameInstance))
	{
		return WpWindowingSubsystemPrivate::MakeResult(
			EWpWindowResultCode::InvalidOwningContext,
			TEXT("OwningPlayer must belong to this subsystem's GameInstance."));
	}

	if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
	{
		return Service->CreateWindow(
			SessionId,
			GameInstance,
			this,
			WidgetClass,
			Options,
			OwningPlayer,
			OutHandle);
	}

	return WpWindowingSubsystemPrivate::Unavailable();
}

FWpWindowResult UWpWindowingSubsystem::RequestCloseWindow(FWpWindowHandle Handle)
{
	if (!IsInGameThread())
	{
		return WpWindowingSubsystemPrivate::WrongThread();
	}
	if (!bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
	{
		if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
		{
			return Service->RequestClose(SessionId, Handle);
		}
	}
	return WpWindowingSubsystemPrivate::Unavailable();
}

FWpWindowResult UWpWindowingSubsystem::SetWindowTitle(FWpWindowHandle Handle, FText NewTitle)
{
	if (!IsInGameThread())
	{
		return WpWindowingSubsystemPrivate::WrongThread();
	}
	if (!bDeinitializing && SessionId.IsValid() && !IsEngineExitRequested())
	{
		if (FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
		{
			return Service->SetTitle(SessionId, Handle, NewTitle);
		}
	}
	return WpWindowingSubsystemPrivate::Unavailable();
}

EWpWindowState UWpWindowingSubsystem::GetWindowState(FWpWindowHandle Handle) const
{
	if (IsInGameThread() && !bDeinitializing && SessionId.IsValid())
	{
		if (const FWpWindowLifetimeService* Service = WpGetWindowLifetimeService())
		{
			return Service->GetState(SessionId, Handle);
		}
	}
	return EWpWindowState::Invalid;
}

bool UWpWindowingSubsystem::IsWindowOpen(FWpWindowHandle Handle) const
{
	return GetWindowState(Handle) == EWpWindowState::Open;
}

void UWpWindowingSubsystem::NotifyOpened(FWpWindowHandle Handle)
{
	if (!bDeinitializing && SessionId.IsValid())
	{
		OnWindowOpened.Broadcast(Handle);
	}
}

void UWpWindowingSubsystem::NotifyClosed(const FWpWindowClosedEvent& Event)
{
	if (!bDeinitializing && SessionId.IsValid())
	{
		OnWindowClosed.Broadcast(Event);
	}
}

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