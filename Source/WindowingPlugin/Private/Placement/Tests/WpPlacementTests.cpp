#include "Placement/WpPlacement.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWpPlacementGeometryTest, "WindowingPlugin.M2.Geometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWpPlacementGeometryTest::RunTest(const FString& Parameters)
{
    const FWpPlacementSnapshot Saved{
        FIntRect(-1100, 100, -700, 400), FIntRect(-1200, 0, 0, 900), TEXT("left"), 96};
    const TArray<FWpPlacementMonitor> Displays{
        {FIntRect(0, 0, 1200, 900), TEXT("primary"), 144, true},
        {Saved.WorkArea, TEXT("left"), 96, false}};

    TestEqual(TEXT("Prefer the saved monitor"), WpPlacement::SelectMonitor(Saved, Displays), 1);
    const TOptional<FIntRect> SameDisplay =
        WpPlacement::ResolveBounds(Saved, Displays[1], FIntPoint(200, 150));
    TestTrue(TEXT("Preserve bounds on the same display"), SameDisplay.IsSet() &&
        SameDisplay.GetValue() == Saved.Normal);

    const TArray<FWpPlacementMonitor> Remaining{Displays[0]};
    TestEqual(TEXT("Use the primary if the saved monitor is missing"),
        WpPlacement::SelectMonitor(Saved, Remaining), 0);
    const TOptional<FIntRect> Recovered =
        WpPlacement::ResolveBounds(Saved, Remaining[0], FIntPoint(200, 150));
    TestTrue(TEXT("Scale and center the recovered window"), Recovered.IsSet() &&
        Recovered.GetValue() == FIntRect(300, 225, 900, 675));
    TestFalse(TEXT("Old title bar is unreachable"), WpPlacement::HasReachableTitleBar(Saved.Normal, Remaining));
    if (Recovered.IsSet())
    {
        TestTrue(TEXT("Recovered title bar is reachable"),
            WpPlacement::HasReachableTitleBar(Recovered.GetValue(), Remaining));
    }
    return true;
}

#if PLATFORM_WINDOWS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWpPlacementStorageTest, "WindowingPlugin.M2.Storage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWpPlacementStorageTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectIntermediateDir(), TEXT("WindowingPlacementTests"), FGuid::NewGuid().ToString()));
    const FString Path = FPaths::Combine(Directory, TEXT("Placement.json"));
    ON_SCOPE_EXIT
    {
        IFileManager& Files = IFileManager::Get();
        Files.Delete(*Path);
        Files.Delete(*(Path + TEXT(".lock")));
        Files.DeleteDirectory(*Directory);
    };

    const FWpPlacementIdentity First{TEXT("Default"), TEXT("Inspector")};
    const FWpPlacementIdentity Second{TEXT("Other"), TEXT("Inspector")};
    const FWpPlacementSnapshot Saved{
        FIntRect(100, 120, 500, 420), FIntRect(0, 0, 1200, 900), TEXT("primary"), 96, true};
    FWpPlacementMap Dirty;
    Dirty.Add(First, Saved);
    if (!TestTrue(TEXT("Write the first profile"), WpPlacement::MergeAndSave(Path, Dirty)))
    {
        return false;
    }
    Dirty.Reset();
    Dirty.Add(Second, Saved);
    TestTrue(TEXT("Merge the second profile"), WpPlacement::MergeAndSave(Path, Dirty));

    FWpPlacementMap Loaded;
    TestTrue(TEXT("Read the saved schema"),
        WpPlacement::Load(Path, Loaded) == WpPlacement::ELoadResult::Loaded);
    TestEqual(TEXT("Both profiles survive the merge"), Loaded.Num(), 2);
    for (const FWpPlacementIdentity& Identity : { First, Second })
    {
        const FWpPlacementSnapshot* Actual = Loaded.Find(Identity);
        TestTrue(TEXT("Bounds, display, DPI, and maximized state round trip"), Actual && *Actual == Saved);
    }

    const FString Future = TEXT("{\"schemaVersion\":2,\"placements\":[]}");
    if (!TestTrue(TEXT("Seed a newer schema"), FFileHelper::SaveStringToFile(Future, *Path)))
    {
        return false;
    }
    TestTrue(TEXT("Reject a newer schema"), WpPlacement::Load(Path, Loaded) ==
        WpPlacement::ELoadResult::UnsupportedVersion);
    TestFalse(TEXT("Preserve the newer file on write"), WpPlacement::MergeAndSave(Path, Dirty));
    FString Unchanged;
    TestTrue(TEXT("Original data is intact"), FFileHelper::LoadFileToString(Unchanged, *Path) &&
        Unchanged == Future);
    return true;
}
#endif
#endif
