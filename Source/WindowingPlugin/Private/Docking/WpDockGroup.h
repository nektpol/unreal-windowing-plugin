// Developed by Neko Creative Technologies

#pragma once
#include "Presentation/WpDockHeader.h"
#include "Presentation/WpNativeWindowHost.h"
#include "Components/NativeWidgetHost.h"
#include "Widgets/SBoxPanel.h"

struct FWpDockGroup
{
	FGuid Id = FGuid::NewGuid();
	FGuid SessionId;
	EWpDockSide Side = EWpDockSide::Left;
	FText Title;
	TSharedPtr<SBoxPanel> Content;
	TArray<FGuid> Children;
	TWeakObjectPtr<UNativeWidgetHost> Slot;
	TUniquePtr<FWpNativeWindowHost> Host;
	bool bNativeClosing = false;
	FWpDockFrame Frame;
};