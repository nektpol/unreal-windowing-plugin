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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWpWorkspaceResizeTest, "WindowingPlugin.M53.EdgeResize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWpWorkspaceResizeTest::RunTest(const FString&)
{
    using namespace WpWorkspaceLayout;
    TestFalse(TEXT("No budget preserves the previous preference"),
        ResizePreference(0, 100, 70, 6, 32).IsSet());
    TestEqual(TEXT("A lone edge stops at the center reserve"),
        ResizePreference(100, 0, 140, 6, 32).Get(0.0), 100.0);

    FInput Input;
    Input.Viewport = FVector2D(420, 300);
    Input.Right = 100;
    Input.Left = ResizePreference(100, Input.Right, 75, 6, 32).Get(0.0);
    const auto Layout = Resolve(Input);
    TestTrue(TEXT("Opposite edge retains its reserve"),
        FMath::IsNearlyEqual(Layout[ERegion::Right].Size.X, 32.0));
    TestTrue(TEXT("Dragged extent reaches its constrained target"),
        FMath::IsNearlyEqual(Layout[ERegion::Left].Size.X, 68.0));
    TestTrue(TEXT("Center keeps its reserve"),
        FMath::IsNearlyEqual(Layout[ERegion::Center].Size.X, 320.0));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWpWorkspaceCornerTest, "WindowingPlugin.M53.Corners",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWpWorkspaceCornerTest::RunTest(const FString&)
{
    using namespace WpWorkspaceLayout;
    FInput Input;
    Input.Viewport = FVector2D(960, 640);
    Input.Top = 160;
    Input.Right = 240;
    const FLayout Default = Resolve(Input);
    TestEqual(TEXT("Right owns the top corner by default"), Default[ERegion::Right].Position.Y, 0.0);
    TestEqual(TEXT("Top stops at Right by default"), Default[ERegion::Top].Size.X, 720.0);

    Input.bTopOwnsRight = true;
    const FLayout Expanded = Resolve(Input);
    TestEqual(TEXT("Top reaches the right boundary"), Expanded[ERegion::Top].Size.X, 960.0);
    TestEqual(TEXT("Right starts below Top"), Expanded[ERegion::Right].Position.Y, 160.0);
    TestEqual(TEXT("Center remains bounded"), Expanded[ERegion::Center].Size, Default[ERegion::Center].Size);

    Input.bTopOwnsRight = false;
    TestEqual(TEXT("Restoring ownership restores Right's position"),
        Resolve(Input)[ERegion::Right].Position, Default[ERegion::Right].Position);
    Input.Top = 0;
    TestEqual(TEXT("Right reclaims the corner when Top disappears"), Resolve(Input)[ERegion::Right].Position.Y, 0.0);

    for (int32 Occupancy = 0; Occupancy < 16; ++Occupancy)
    {
        Input.Left = Occupancy & 1 ? 240 : 0;
        Input.Right = Occupancy & 2 ? 240 : 0;
        Input.Top = Occupancy & 4 ? 160 : 0;
        Input.Bottom = Occupancy & 8 ? 160 : 0;
        for (int32 Corners = 0; Corners < 16; ++Corners)
        {
            Input.bTopOwnsLeft = (Corners & 1) != 0;
            Input.bTopOwnsRight = (Corners & 2) != 0;
            Input.bBottomOwnsLeft = (Corners & 4) != 0;
            Input.bBottomOwnsRight = (Corners & 8) != 0;
            const FLayout Layout = Resolve(Input);
            for (int32 A = 0; A < static_cast<int32>(ERegion::Count); ++A)
            {
                const FRegion& First = Layout.Regions[A];
                for (int32 B = A + 1; B < static_cast<int32>(ERegion::Count); ++B)
                {
                    const FRegion& Second = Layout.Regions[B];
                    const FVector2D EndA = First.Position + First.Size;
                    const FVector2D EndB = Second.Position + Second.Size;
                    const bool bOverlap = FMath::Min(EndA.X, EndB.X) > FMath::Max(First.Position.X, Second.Position.X)
                        && FMath::Min(EndA.Y, EndB.Y) > FMath::Max(First.Position.Y, Second.Position.Y);
                    if (bOverlap)
                    {
                        AddError(FString::Printf(TEXT("Regions overlap for occupancy %d, corners %d"), Occupancy, Corners));
                        return false;
                    }
                }
            }
        }
    }
    return true;
}
#endif