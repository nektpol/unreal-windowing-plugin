// Developed by Neko Creative Technologies

#include "WpDockLayoutPanel.h"

#include "Components/PanelSlot.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/Children.h"
#include "Layout/WpWorkspaceGeometry.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SPanel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WpDockLayoutPanel)

using namespace WpWorkspaceLayout;

namespace
{
    TOptional<ERegion> FindRegion(FName Name)
    {
        if (Name == TEXT("LeftDock")) { return ERegion::Left; }
        if (Name == TEXT("RightDock")) { return ERegion::Right; }
        if (Name == TEXT("TopDock")) { return ERegion::Top; }
        if (Name == TEXT("BottomDock")) { return ERegion::Bottom; }
        if (Name == TEXT("Center")) { return ERegion::Center; }
        return {};
    }
}

class SWpDockLayoutPanel final : public SPanel
{
public:
    SLATE_BEGIN_ARGS(SWpDockLayoutPanel) {}
    SLATE_END_ARGS()

    SWpDockLayoutPanel() : Children(this) {}

    void Construct(const FArguments&) {}

    void ResetRegions()
    {
        Children.Empty();
        Regions.Reset();
        Sources.Reset();
    }

    void AddRegion(ERegion Region, UWidget& Source)
    {
        if (Regions.Contains(Region))
        {
            return;
        }

        Children.Add(SNew(SBox)
            .Clipping(EWidgetClipping::ClipToBoundsAlways)
            [Source.TakeWidget()]);
        Regions.Add(Region);
        Sources.Add(&Source);
    }

    void SetSizes(FVector2D CenterMinimum, FVector2D SideWidths, FVector2D RowHeights)
    {
        Preferred.CenterMinimum = CenterMinimum;
        Preferred.Left = SideWidths.X;
        Preferred.Right = SideWidths.Y;
        Preferred.Top = RowHeights.X;
        Preferred.Bottom = RowHeights.Y;
        Invalidate(EInvalidateWidgetReason::Layout);
    }

    void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Arranged) const override
    {
        FInput Input = Preferred;
        Input.Viewport = Geometry.GetLocalSize();
        if (!HasDockContent(ERegion::Left)) { Input.Left = 0; }
        if (!HasDockContent(ERegion::Right)) { Input.Right = 0; }
        if (!HasDockContent(ERegion::Top)) { Input.Top = 0; }
        if (!HasDockContent(ERegion::Bottom)) { Input.Bottom = 0; }
        const FLayout Layout = Resolve(Input);
        for (int32 Index = 0; Index < Children.Num(); ++Index)
        {
            const FRegion& Region = Layout[Regions[Index]];
            const auto Child = ConstCastSharedRef<SWidget>(Children.GetChildAt(Index));
            if (Region.Size.X > 0 && Region.Size.Y > 0)
            {
                Arranged.AddWidget(Geometry.MakeChild(Child, Region.Size,
                    FSlateLayoutTransform(FVector2f(Region.Position))));
            }
        }
    }

    FVector2D ComputeDesiredSize(float) const override
    {
        // The workspace is stretched by Add to Viewport.
        return FVector2D(NonNegative(Preferred.CenterMinimum.X),
            NonNegative(Preferred.CenterMinimum.Y));
    }

    FChildren* GetChildren() override { return &Children; }

private:
    bool HasDockContent(ERegion Region) const
    {
        const int32 Index = Regions.IndexOfByKey(Region);
        const auto* Panel = Sources.IsValidIndex(Index) ? Cast<UPanelWidget>(Sources[Index].Get()) : nullptr;
        return Panel && Panel->GetChildrenCount() > 0;
    }

    TSlotlessChildren<SWidget> Children;
    TArray<ERegion> Regions;
    TArray<TWeakObjectPtr<UWidget>> Sources;
    FInput Preferred;
};

UWpDockLayoutPanel::UWpDockLayoutPanel(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SetVisibilityInternal(ESlateVisibility::SelfHitTestInvisible);
}

void UWpDockLayoutPanel::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    LayoutPanel.Reset();
}

TSharedRef<SWidget> UWpDockLayoutPanel::RebuildWidget()
{
    LayoutPanel = SNew(SWpDockLayoutPanel);
    LayoutPanel->SetSizes(CenterMinimum, PreferredSideWidths, PreferredRowHeights);
    RefreshRegions();
    return LayoutPanel.ToSharedRef();
}

void UWpDockLayoutPanel::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    if (LayoutPanel)
    {
        LayoutPanel->SetSizes(CenterMinimum, PreferredSideWidths, PreferredRowHeights);
    }
}

void UWpDockLayoutPanel::OnSlotAdded(UPanelSlot*)
{
    RefreshRegions();
}

void UWpDockLayoutPanel::OnSlotRemoved(UPanelSlot*)
{
    RefreshRegions();
}

void UWpDockLayoutPanel::RefreshRegions()
{
    if (!LayoutPanel)
    {
        return;
    }

    LayoutPanel->ResetRegions();
    for (int32 Index = 0; Index < GetChildrenCount(); ++Index)
    {
        if (UWidget* Child = GetChildAt(Index))
        {
            if (const auto Region = FindRegion(Child->GetFName()))
            {
                LayoutPanel->AddRegion(Region.GetValue(), *Child);
            }
        }
    }
}

#if WITH_EDITOR
const FText UWpDockLayoutPanel::GetPaletteCategory()
{
    return NSLOCTEXT("WindowingPlugin", "DockLayoutPalette", "Windowing");
}
#endif
