#include "Layout/SWpGroupSplit.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"
#include "Widgets/Layout/SBox.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWpGroupSplitTest, "WindowingPlugin.M53.GroupSplit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWpGroupSplitTest::RunTest(const FString&)
{
    for (const bool bHorizontal : {true, false})
    {
        const auto Split = SNew(SWpGroupSplit).Horizontal(bHorizontal);
        const FGuid First = FGuid::NewGuid();
        const FGuid Second = FGuid::NewGuid();
        Split->AddPanel(First, SNew(SBox).WidthOverride(40).HeightOverride(40), false);
        Split->AddPanel(Second, SNew(SBox).WidthOverride(40).HeightOverride(40), false);
        Split->SlatePrepass(1.f);

        const auto Axis = [bHorizontal](FVector2D Size) { return bHorizontal ? Size.X : Size.Y; };
        const FVector2D Room = bHorizontal ? FVector2D(300, 100) : FVector2D(100, 300);
        const FGeometry Geometry = FGeometry::MakeRoot(Room, FSlateLayoutTransform());
        FArrangedChildren Expanded(EVisibility::Visible);
        Split->OnArrangeChildren(Geometry, Expanded);
        if (TestEqual(TEXT("Two panels and a divider"), Expanded.Num(), 3))
        {
            TestEqual(TEXT("Panels share available space"), Axis(Expanded[0].Geometry.GetLocalSize()), 147.0);
        }

        Split->SetPanelCollapsed(First, true);
        FArrangedChildren Collapsed(EVisibility::Visible);
        Split->OnArrangeChildren(Geometry, Collapsed);
        if (TestEqual(TEXT("Collapse removes the divider"), Collapsed.Num(), 2))
        {
            TestEqual(TEXT("Collapsed header extent"), Axis(Collapsed[0].Geometry.GetLocalSize()), 40.0);
            TestEqual(TEXT("Neighbor takes released space"), Axis(Collapsed[1].Geometry.GetLocalSize()), 260.0);
        }

        Split->SetPanelCollapsed(First, false);
        const FVector2D Tight = bHorizontal ? FVector2D(120, 100) : FVector2D(100, 120);
        FArrangedChildren Overflow(EVisibility::Visible);
        Split->OnArrangeChildren(FGeometry::MakeRoot(Tight, FSlateLayoutTransform()), Overflow);
        if (TestEqual(TEXT("Crowded group still arranges both panels"), Overflow.Num(), 3))
        {
            TestEqual(TEXT("Minimum extent survives overflow"), Axis(Overflow[2].Geometry.GetLocalSize()), 96.0);
        }
    }
    return true;
}
#endif
