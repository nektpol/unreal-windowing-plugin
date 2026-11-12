// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "WpPlacement.h"
#include "UObject/GCObject.h"
#include "WpWindowTypes.h"
#include "Widgets/SWindow.h"

class APlayerController;
class UGameInstance;
class UUserWidget;
class UWpWindowingSubsystem;
class FWpNativeWindowHost;
class UWpDockWorkspace;
struct FWpWindowRuntimeEntry;
struct FWpDockGroup;

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
	FWpWindowFocusSnapshot GetWindowFocus(const FGuid& SessionId) const;
	FWpWindowResult RegisterDockWorkspace(const FGuid& SessionId, UWpDockWorkspace* Workspace);
	FWpWindowResult UnregisterDockWorkspace(const FGuid& SessionId, UWpDockWorkspace* Workspace);
	FWpWindowResult DockWindow(const FGuid& SessionId, FWpWindowHandle Handle, EWpDockSide Side);
	FWpWindowResult UndockWindow(const FGuid& SessionId, FWpWindowHandle Handle);
	FWpWindowResult UndockGroup(const FGuid& SessionId, FGuid GroupId);
	FWpWindowResult DockGroup(const FGuid& SessionId, FGuid GroupId);
	FWpWindowPresentation GetWindowPresentation(const FGuid& SessionId, FWpWindowHandle Handle) const;
	void BeginSessionShutdown(const FGuid& SessionId);
	void Shutdown();

	void AddReferencedObjects(FReferenceCollector& Collector) override;
	FString GetReferencerName() const override;

private:
	struct FCallbackTarget
	{
		FWpWindowLifetimeService* Service = nullptr;
	};

	FWpWindowRuntimeEntry* FindPublished(const FGuid& SessionId, FWpWindowHandle Handle) const;
	void BindSlateHooks();
	void OnNativePreClose(FGuid Id, const SWindow& Window);
	void OnGroupNativePreClose(FGuid Id, const SWindow& Window);
	FOnWindowClosed MakeTransferCloseCallback(FGuid Id, bool bGroup, TSharedPtr<bool> CandidateClosed = nullptr);
	void RetireDockHost(TUniquePtr<FWpNativeWindowHost> Host, bool bCloseObserved = false);
	void DrainDockHosts();
	UWpDockWorkspace* FindDockWorkspace(const FGuid& SessionId) const;
	FWpDockGroup* FindEdgeGroup(const FGuid& SessionId, EWpDockSide Side) const;
	void CloseGroupChildren(FWpDockGroup& Group, EWpWindowCloseReason Reason);
	void RemoveGroupMember(FWpWindowRuntimeEntry& Entry);
	bool CanMoveGroup(const FWpDockGroup& Group) const;
	FWpWindowResult MoveGroup(const FGuid& SessionId, FGuid GroupId, bool bFloat);
	void EmitPresentationChanged(const FGuid& SessionId, FGuid Id);

	TMap<FGuid, TWeakObjectPtr<UWpDockWorkspace>> DockWorkspaces;
	TMap<FGuid, TSharedPtr<FWpDockGroup>> DockGroups;
	TArray<TUniquePtr<FWpNativeWindowHost>> RetiringDockHosts;
	bool bMovingDockPresentation = false;
	void CloseEntry(FWpWindowRuntimeEntry& Entry, EWpWindowCloseReason Reason);
	void Rollback(FGuid Id, FWpWindowRuntimeEntry& Entry);
	void OnSlatePostTick(float DeltaTime);
	void Drain();
	void TickWindowFocus();
	TMap<FGuid, TWeakObjectPtr<UWpWindowingSubsystem>> FocusObservers;

	TMap<FGuid, TUniquePtr<FWpWindowRuntimeEntry>> Entries;
	TSharedPtr<FCallbackTarget> CallbackTarget;
	FDelegateHandle PostTickHandle;
	FDelegateHandle PreShutdownHandle;
	int32 OperationDepth = 0;
	bool bShuttingDown = false;

	void LoadPlacements();
	void CapturePlacement(FWpWindowRuntimeEntry& Entry);
	void TickPlacements();
	void FlushPlacements();

	FWpPlacementMap SavedPlacements;
	FWpPlacementMap DirtyPlacements;
	TOptional<FString> PlacementTopology;
	double NextPlacementSample = 0.0;
	double PlacementFlushAt = 0.0;
	bool bPlacementLoaded = false;
	bool bPlacementWritesEnabled = false;
};
