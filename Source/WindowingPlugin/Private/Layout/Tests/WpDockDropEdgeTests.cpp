// Developed by Neko Creative Technologies

#include "Layout/WpWorkspaceGeometry.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWpDockDropEdgeTest, "WindowingPlugin.M541.DockDropEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWpDockDropEdgeTest::RunTest(const FString&)
{
	using namespace WpWorkspaceLayout;
	const FVector2D Viewport(800, 600);
	TestTrue(TEXT("Left perimeter"),
		FindDropEdge({12, 300}, Viewport) == TOptional<ERegion>(ERegion::Left));
	TestTrue(TEXT("Right perimeter"),
		FindDropEdge({790, 300}, Viewport) == TOptional<ERegion>(ERegion::Right));
	TestTrue(TEXT("Top perimeter"),
		FindDropEdge({400, 10}, Viewport) == TOptional<ERegion>(ERegion::Top));
	TestTrue(TEXT("Bottom perimeter"),
		FindDropEdge({400, 590}, Viewport) == TOptional<ERegion>(ERegion::Bottom));
	TestTrue(TEXT("Top wins at the closer corner"),
		FindDropEdge({15, 5}, Viewport) == TOptional<ERegion>(ERegion::Top));
	TestTrue(TEXT("Corner ties are stable"),
		FindDropEdge({12, 12}, Viewport) == TOptional<ERegion>(ERegion::Left));
	TestFalse(TEXT("Center has no target"), FindDropEdge({400, 300}, Viewport).IsSet());
	TestFalse(TEXT("Outside has no target"), FindDropEdge({-1, 300}, Viewport).IsSet());
	TestFalse(TEXT("The outer boundary is exclusive"), FindDropEdge({800, 300}, Viewport).IsSet());
	TestFalse(TEXT("A tiny viewport retains a center area"),
		FindDropEdge({20, 15}, {40, 30}).IsSet());
	TestFalse(TEXT("Zero-sized workspace has no target"),
		FindDropEdge(FVector2D::ZeroVector, FVector2D::ZeroVector).IsSet());
	return true;
}
#endif