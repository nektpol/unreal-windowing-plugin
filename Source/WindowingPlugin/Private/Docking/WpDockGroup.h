// Developed by Neko Creative Technologies

#pragma once
#include "Presentation/WpDockHeader.h"
#include "Presentation/WpNativeWindowHost.h"
#include "Components/NativeWidgetHost.h"
#include "Layout/SWpGroupSplit.h"

struct FWpDockGroup
{
	FGuid Id = FGuid::NewGuid();
	FGuid SessionId;
	EWpDockSide Side = EWpDockSide::Left;
	bool bHasDockSide = true;
	FText Title;
	TSharedPtr<SWpGroupSplit> Content;
	TArray<FGuid> Children;
	TWeakObjectPtr<UNativeWidgetHost> Slot;
	TUniquePtr<FWpNativeWindowHost> Host;
	bool bNativeClosing = false;
	FWpDockFrame Frame;
};