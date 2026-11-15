// Developed by Neko Creative Technologies

#include "Docking/WpDockPanelDropTarget.h"
#include "Layout/SWpGroupSplit.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWpPanelRelativeDropTest, "WindowingPlugin.M542.PanelRelativeDrop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWpPanelRelativeDropTest::RunTest(const FString&)
{
    const FVector2D Size(800, 600);
    TestTrue(TEXT("Select left"), WpFindPanelDropSide({100, 300}, Size) ==
        TOptional<EWpDockSide>(EWpDockSide::Left));
    TestTrue(TEXT("Select right"), WpFindPanelDropSide({700, 300}, Size) ==
        TOptional<EWpDockSide>(EWpDockSide::Right));
    TestTrue(TEXT("Select above"), WpFindPanelDropSide({400, 100}, Size) ==
        TOptional<EWpDockSide>(EWpDockSide::Top));
    TestTrue(TEXT("Select below"), WpFindPanelDropSide({400, 500}, Size) ==
        TOptional<EWpDockSide>(EWpDockSide::Bottom));
    TestFalse(TEXT("Center cancels"), WpFindPanelDropSide({400, 300}, Size).IsSet());

    const auto Split = SNew(SWpGroupSplit).Horizontal(true);
    const auto A = SNew(SBox);
    const auto B = SNew(SBox);
    const auto C = SNew(SBox);
    const auto D = SNew(SBox);
    const FGuid AId = FGuid::NewGuid();
    const FGuid BId = FGuid::NewGuid();
    const FGuid CId = FGuid::NewGuid();
    const FGuid DId = FGuid::NewGuid();
    Split->AddPanel(AId, A, false);
    Split->AddPanel(BId, B, false);
    TestTrue(TEXT("Place C to the left of B"), Split->InsertPanelBeside(BId, CId, C, false, true, true));
    TestFalse(TEXT("Reject duplicate leaves"), Split->InsertPanelBeside(AId, CId, C, false, true, true));
    TestFalse(TEXT("Reject missing target"), Split->InsertPanelBeside(FGuid::NewGuid(), DId, D, false, false, true));

    Split->SlatePrepass(1.f);
    FArrangedChildren BeforeNest(EVisibility::Visible);
    Split->OnArrangeChildren(FGeometry::MakeRoot(Size, FSlateLayoutTransform()), BeforeNest);
    if (TestEqual(TEXT("Three leaves and two grips"), BeforeNest.Num(), 5))
    {
        TestTrue(TEXT("C is the middle leaf"), &BeforeNest[2].Widget->GetChildren()->GetChildAt(0).Get() == &C.Get());
        TestTrue(TEXT("B is the right leaf"), &BeforeNest[4].Widget->GetChildren()->GetChildAt(0).Get() == &B.Get());
    }

    Split->SetPanelCollapsed(BId, true);
    TestFalse(TEXT("Reject a collapsed target"), Split->InsertPanelBeside(BId, DId, D, false, false, false));
    Split->SetPanelCollapsed(BId, false);
    TestTrue(TEXT("Place D below B in a branch"), Split->InsertPanelBeside(BId, DId, D, false, false, false));
    TestTrue(TEXT("Both nested leaves keep their IDs"), Split->ContainsPanel(BId) && Split->ContainsPanel(DId));
    const FGuid EId = FGuid::NewGuid();
    TestTrue(TEXT("Find a target inside an existing branch"),
        Split->InsertPanelBeside(DId, EId, SNew(SBox), false, false, true));
    Split->RemovePanel(EId);
    Split->RemovePanel(DId);
    TestTrue(TEXT("Removing D preserves B and C"), Split->ContainsPanel(BId) && Split->ContainsPanel(CId));
    TestFalse(TEXT("D is removed"), Split->ContainsPanel(DId));
    return true;
}
#endif