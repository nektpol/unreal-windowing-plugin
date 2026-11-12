// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "WpWindowTypes.h"

class APlayerController;
class UGameInstance;
class UUserWidget;
class UWpWindowingSubsystem;
struct FWpWindowRuntimeEntry;

class FWpWindowLifetimeService final : public FGCObject
{
public:
	FWpWindowLifetimeService();
	~FWpWindowLifetimeService() override;

	FWpWindowResult CreateWindow(const FGuid& SessionId, UGameInstance* Owner,
		UWpWindowingSubsystem* Sink, TSubclassOf<UUserWidget> WidgetClass,
		const FWpWindowCreateOptions& Options, APlayerController* Player,
		FWpWindowHandle& OutHandle);
	FWpWindowResult RequestClose(const FGuid& SessionId, FWpWindowHandle Handle);
	FWpWindowResult SetTitle(const FGuid& SessionId, FWpWindowHandle Handle,
		const FText& Title);
	EWpWindowState GetState(const FGuid& SessionId, FWpWindowHandle Handle) const;
	void BeginSessionShutdown(const FGuid& SessionId);
	void Shutdown();

	void AddReferencedObjects(FReferenceCollector& Collector) override;
	FString GetReferencerName() const override;

private:
	struct FCallbackTarget
	{
		FWpWindowLifetimeService* Service = nullptr;
	};

	FWpWindowRuntimeEntry* FindPublished(const FGuid& SessionId,
		FWpWindowHandle Handle) const;
	void BindSlateHooks();
	void OnNativePreClose(FGuid Id);
	void CloseEntry(FWpWindowRuntimeEntry& Entry, EWpWindowCloseReason Reason);
	void Rollback(FGuid Id, FWpWindowRuntimeEntry& Entry);
	void OnSlatePostTick(float DeltaTime);
	void Drain();

	TMap<FGuid, TUniquePtr<FWpWindowRuntimeEntry>> Entries;
	TSharedPtr<FCallbackTarget> CallbackTarget;
	FDelegateHandle PostTickHandle;
	FDelegateHandle PreShutdownHandle;
	int32 OperationDepth = 0;
	bool bShuttingDown = false;
};