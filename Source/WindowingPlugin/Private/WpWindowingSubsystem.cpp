// Developed by Neko Creative Technologies

#include "WpWindowingSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "WindowingPlugin.h"
#include "WpWindowLifetimeService.h"

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