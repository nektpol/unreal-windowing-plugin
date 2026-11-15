// Developed by Neko Creative Technologies

#pragma once

#include "Placement/WpPlacement.h"

namespace WpUndockDrop
{
	void* GetCaptureWindow();
	void* GetRootWindowAt(FVector2D Screen);
	bool IsDesktopSurface(void* Window);
	TOptional<FWpPlacementSnapshot> MakePlacement(FVector2D Screen, const FWpPlacementMonitor& Monitor,
		const FWpPlacementSnapshot* Previous, FVector2D InitialClientSize);
}