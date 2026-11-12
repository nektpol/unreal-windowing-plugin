// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "WpDockHeader.h"
#include "WpPlacement.h"
#include "WpWindowTypes.h"
#include "UObject/WeakObjectPtr.h"

class UWpWindowRecord;
class UWpWindowingSubsystem;
class FWpNativeWindowHost;
class SBox;
class SWidget;

struct FWpWindowRuntimeEntry final
{
	FWpWindowRuntimeEntry() = default;
	~FWpWindowRuntimeEntry();

	FWpWindowRuntimeEntry(const FWpWindowRuntimeEntry&) = delete;
	FWpWindowRuntimeEntry& operator=(const FWpWindowRuntimeEntry&) = delete;

	TObjectPtr<UWpWindowRecord> Record = nullptr;
	TUniquePtr<FWpNativeWindowHost> Host;
	TWeakObjectPtr<UWpWindowingSubsystem> EventSink;
	bool bFailedCreation = false;
	TOptional<FWpPlacementIdentity> PlacementId;
	FWpPlacementSnapshot Placement;
	FVector2D PlacementMinimumClientSize = FVector2D::ZeroVector;
	bool bHasPlacement = false;
	bool bRecoverPlacement = false;
	TSharedPtr<SWidget> RetainedContent;
	FWpWindowCreateOptions CreateOptions;
	FGuid DockGroupId;
	TSharedPtr<SBox> DockContentSlot;
	bool bDockCollapsed = false;
	FWpDockFrame DockFrame;
	FWpDockFrame FloatingFrame;
	TSharedPtr<SBox> FloatingContentSlot;
	SHorizontalBox::FSlot* DockHorizontalSlot = nullptr;
	SVerticalBox::FSlot* DockVerticalSlot = nullptr;
};