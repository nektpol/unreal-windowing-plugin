// Developed by Neko Creative Technologies

#include "Layout/WpWorkspaceGeometry.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWpWorkspaceGeometryTest, "WindowingPlugin.M53.WorkspaceGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWpWorkspaceGeometryTest::RunTest(const FString&)
{
	using namespace WpWorkspaceLayout;
	FInput Input;
	Input.Viewport = FVector2D(960, 640);
	Input.Left = 240;
	Input.Right = 240;
	Input.Top = 160;
	Input.Bottom = 160;

	const FLayout Large = Resolve(Input);
	TestEqual(TEXT("Default center"), Large[ERegion::Center].Size, FVector2D(480, 320));
	TestEqual(TEXT("Right owns the corner"), Large[ERegion::Right].Size, FVector2D(240, 640));

	Input.Viewport = FVector2D(500, 280);
	const FLayout Small = Resolve(Input);
	TestEqual(TEXT("Center minimum survives"), Small[ERegion::Center].Size, Input.CenterMinimum);
	TestEqual(TEXT("Opposing rows shrink proportionally"), Small[ERegion::Top].Size.Y, 50.0);
	TestEqual(TEXT("Bottom stays within the viewport"),
		Small[ERegion::Bottom].Position.Y + Small[ERegion::Bottom].Size.Y, 280.0);

	Input.Viewport = FVector2D(960, 640);
	TestEqual(TEXT("Expanding restores the preferred row size"), Resolve(Input)[ERegion::Top].Size.Y, 160.0);

	Input.Left = 0;
	Input.Top = 0;
	const FLayout EmptyEdges = Resolve(Input);
	TestEqual(TEXT("Empty left has no extent"), EmptyEdges[ERegion::Left].Size.X, 0.0);
	TestEqual(TEXT("Empty top has no extent"), EmptyEdges[ERegion::Top].Size.Y, 0.0);

	Input.Viewport = FVector2D::ZeroVector;
	const FLayout Zero = Resolve(Input);
	for (const FRegion& Region : Zero.Regions)
	{
		TestEqual(TEXT("Zero viewport has no positive region"), Region.Size, FVector2D::ZeroVector);
	}
	return true;
}
#endif
