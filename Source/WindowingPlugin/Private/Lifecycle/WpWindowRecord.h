// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WpWindowTypes.h"
#include "WpWindowRecord.generated.h"

class UUserWidget;
class UGameInstance;

UCLASS(Transient)
class UWpWindowRecord final : public UObject
{
	GENERATED_BODY()
	friend class FWpWindowLifetimeService;

private:
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Widget = nullptr;

	TWeakObjectPtr<UGameInstance> SessionOwner;
	FGuid WindowId;
	FGuid SessionId;
	EWpWindowState State = EWpWindowState::Creating;
	EWpWindowCloseReason CloseReason = EWpWindowCloseReason::Unknown;

	bool bNativeDestroyRequested = false;
	bool bNativeCloseObserved = false;
	bool bFinalizationQueued = false;
	bool bOpenedEventEmitted = false;
	bool bClosedEventEmitted = false;
};