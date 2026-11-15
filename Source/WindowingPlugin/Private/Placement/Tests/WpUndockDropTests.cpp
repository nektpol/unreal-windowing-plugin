// Developed by Neko Creative Technologies

#include "Docking/WpUndockDrop.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWpUndockPlacementTest, "WindowingPlugin.M544.UndockPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWpUndockPlacementTest::RunTest(const FString&)
{
	const FWpPlacementMonitor Destination{FIntRect(-1600, 0, 0, 1000), TEXT("Left"), 144, false};
	FWpPlacementSnapshot Previous;
	Previous.Normal = FIntRect(100, 100, 700, 500);
	Previous.Dpi = 96;
	Previous.bMaximized = true;

	const auto Placement = WpUndockDrop::MakePlacement({-100, 990}, Destination, &Previous, {640, 480});
	if (TestTrue(TEXT("Release on a monitor with negative X"), Placement.IsSet()))
	{
		TestEqual(TEXT("Prior width scales to destination DPI"), Placement->Normal.Width(), 900);
		TestEqual(TEXT("Prior height scales to destination DPI"), Placement->Normal.Height(), 600);
		TestEqual(TEXT("Right boundary is clamped"), Placement->Normal.Max.X, 0);
		TestEqual(TEXT("Bottom boundary is clamped"), Placement->Normal.Max.Y, 1000);
		TestEqual(TEXT("New placement uses the destination monitor"), Placement->MonitorId, FString(TEXT("Left")));
		TestFalse(TEXT("Drag creates a normal window"), Placement->bMaximized);
	}

	const auto First = WpUndockDrop::MakePlacement({-1200, 100}, Destination, nullptr, {400, 300});
	if (TestTrue(TEXT("Initial size works without a saved snapshot"), First.IsSet()))
	{
		TestEqual(TEXT("Initial width uses the destination DPI"), First->Normal.Width(), 600);
	}
	TestFalse(TEXT("A release outside the work area is rejected"),
		WpUndockDrop::MakePlacement({-100, 1000}, Destination, &Previous, {640, 480}).IsSet());
	return true;
}
#endif