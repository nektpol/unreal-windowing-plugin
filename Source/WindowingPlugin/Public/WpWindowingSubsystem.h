// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WpWindowTypes.h"
#include "WpWindowingSubsystem.generated.h"

class UUserWidget;
class APlayerController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FWpOnWindowOpened, FWpWindowHandle, Handle);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FWpOnWindowClosed, FWpWindowClosedEvent, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FWpOnWindowFocusChanged,
	FWpWindowFocusSnapshot, Previous,
	FWpWindowFocusSnapshot, Current);

UCLASS()
class WINDOWINGPLUGIN_API UWpWindowingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="Windowing")
	FWpWindowResult OpenWidgetWindow(
		TSubclassOf<UUserWidget> WidgetClass,
		const FWpWindowCreateOptions& Options,
		APlayerController* OwningPlayer,
		FWpWindowHandle& OutHandle);

	UFUNCTION(BlueprintCallable, Category="Windowing")
	FWpWindowResult RequestCloseWindow(FWpWindowHandle Handle);

	UFUNCTION(BlueprintPure, Category="Windowing")
	bool IsWindowOpen(FWpWindowHandle Handle) const;

	UFUNCTION(BlueprintPure, Category="Windowing")
	EWpWindowState GetWindowState(FWpWindowHandle Handle) const;

	UFUNCTION(BlueprintCallable, Category="Windowing")
	FWpWindowResult SetWindowTitle(FWpWindowHandle Handle, FText NewTitle);

	UPROPERTY(BlueprintAssignable, Category="Windowing")
	FWpOnWindowOpened OnWindowOpened;

	UPROPERTY(BlueprintAssignable, Category="Windowing")
	FWpOnWindowClosed OnWindowClosed;

	UFUNCTION(BlueprintPure, Category="Windowing")
	FWpWindowFocusSnapshot GetWindowFocus() const;

	UPROPERTY(BlueprintAssignable, Category="Windowing")
	FWpOnWindowFocusChanged OnWindowFocusChanged;

private:
	friend class FWpWindowLifetimeService;

	FGuid SessionId;
	bool bDeinitializing = false;

	void NotifyOpened(FWpWindowHandle Handle);
	void NotifyClosed(const FWpWindowClosedEvent& Event);
	void NotifyFocusChanged(const FWpWindowFocusSnapshot& Current);
	FWpWindowFocusSnapshot LastReportedFocus;
};
