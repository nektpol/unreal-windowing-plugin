// Developed by Neko Creative Technologies

#include "WpDockLayoutPanel.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "InputCoreTypes.h"
#include "Brushes/SlateColorBrush.h"
#include "Widgets/Layout/SBorder.h"
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

    void Construct(const FArguments&)
    {
        SetCanTick(true);
        BuildGrips();
    }

    void ResetRegions()
    {
        CancelResize();
        Children.Empty();
        RegionWidgets.Reset();
        Regions.Reset();
        Sources.Reset();
    }

    void AddRegion(ERegion Region, UWidget& Source)
    {
        if (Regions.Contains(Region))
        {
            return;
        }

        RegionWidgets.Add(SNew(SBox)
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
        const FLayout Layout = Resolve(BuildInput(Geometry.GetLocalSize()));
        for (int32 Index = 0; Index < Regions.Num(); ++Index)
        {
            const FRegion& Region = Layout[Regions[Index]];
            if (Region.Size.X > 0 && Region.Size.Y > 0)
            {
                Arranged.AddWidget(Geometry.MakeChild(RegionWidgets[Index], Region.Size,
                    FSlateLayoutTransform(FVector2f(Region.Position))));
            }
        }
        for (int32 Index = 0; Index < EdgeCount; ++Index)
        {
            ArrangeGrip(Geometry, Arranged, Layout, static_cast<ERegion>(Index));
        }
    }

    FVector2D ComputeDesiredSize(float) const override
    {
        // The workspace is stretched by Add to Viewport.
        return FVector2D(NonNegative(Preferred.CenterMinimum.X),
            NonNegative(Preferred.CenterMinimum.Y));
    }

    FChildren* GetChildren() override { return &Children; }

    void RefreshChildOrder()
    {
        for (const auto& Widget : RegionWidgets)
        {
            Children.Add(Widget);
        }
        for (const auto& Grip : Grips)
        {
            Children.Add(Grip);
        }
    }

    static constexpr int32 EdgeCount = static_cast<int32>(ERegion::Center);
    static constexpr double GripWidth = 6.0;
    static constexpr double OppositeReserve = 32.0;

    void BuildGrips()
    {
        static const FSlateColorBrush GripBrush(FLinearColor(0.24f, 0.24f, 0.24f));
        for (int32 Index = 0; Index < EdgeCount; ++Index)
        {
            const auto Edge = static_cast<ERegion>(Index);
            const auto Grip = SNew(SBorder)
                .BorderImage(&GripBrush)
                .Padding(0.f)
                .OnMouseButtonDown(FPointerEventHandler::CreateSP(
                    SharedThis(this), &SWpDockLayoutPanel::BeginResize, Edge));
            Grip->SetCursor(TOptional<EMouseCursor::Type>(IsSide(Edge)
                ? EMouseCursor::ResizeLeftRight : EMouseCursor::ResizeUpDown));
            Grips.Add(Grip);
        }
    }

    void CancelResize()
    {
        if (!Drag.IsSet())
        {
            return;
        }
        const FDrag Previous = Drag.GetValue();
        Drag.Reset();
        if (FSlateApplication::IsInitialized() && HasMouseCaptureByUser(Previous.User, Previous.Pointer))
        {
            if (const auto User = FSlateApplication::Get().GetUser(Previous.User))
            {
                User->ReleaseCapture(Previous.Pointer);
            }
        }
    }

    void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SPanel::Tick(Geometry, Time, Delta);
        if (Drag.IsSet() && !HasDockContent(Drag->Edge))
        {
            CancelResize();
        }
    }

    FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (!MatchesDrag(Event))
        {
            return FReply::Unhandled();
        }
        if (!HasDockContent(Drag->Edge))
        {
            Drag.Reset();
            return FReply::Handled().ReleaseMouseCapture();
        }

        const ERegion Edge = Drag->Edge;
        const FInput Current = BuildInput(Geometry.GetLocalSize());
        const bool bSide = IsSide(Edge);
        const double Axis = bSide ? Current.Viewport.X : Current.Viewport.Y;
        const double Minimum = bSide ? Current.CenterMinimum.X : Current.CenterMinimum.Y;
        const double Budget = FMath::Max(0.0, Axis - NonNegative(Minimum));
        const double Opposite = OppositePreference(Current, Edge);
        const double Direction = Edge == ERegion::Left || Edge == ERegion::Top ? 1.0 : -1.0;
        const double Delta = PointerCoordinate(Geometry, Event, Edge) - Drag->StartPointer;
        if (const auto Preference = ResizePreference(
            Budget, Opposite, Drag->StartExtent + Direction * Delta, GripWidth, OppositeReserve))
        {
            SetEdgePreference(Edge, Preference.GetValue());
            Invalidate(EInvalidateWidgetReason::Layout);
        }
        return FReply::Handled();
    }

    FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
    {
        if (!MatchesDrag(Event) || Event.GetEffectingButton() != EKeys::LeftMouseButton)
        {
            return FReply::Unhandled();
        }
        Drag.Reset();
        return FReply::Handled().ReleaseMouseCapture();
    }

    void OnMouseCaptureLost(const FCaptureLostEvent&) override
    {
        Drag.Reset();
    }

private:
    struct FDrag
    {
        ERegion Edge;
        double StartPointer;
        double StartExtent;
        int32 User;
        uint32 Pointer;
    };

    static bool IsSide(ERegion Edge)
    {
        return Edge == ERegion::Left || Edge == ERegion::Right;
    }

    static double PointerCoordinate(const FGeometry& Geometry, const FPointerEvent& Event, ERegion Edge)
    {
        const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
        return IsSide(Edge) ? Local.X : Local.Y;
    }

    static double OppositePreference(const FInput& Input, ERegion Edge)
    {
        switch (Edge)
        {
        case ERegion::Left: return NonNegative(Input.Right);
        case ERegion::Right: return NonNegative(Input.Left);
        case ERegion::Top: return NonNegative(Input.Bottom);
        case ERegion::Bottom: return NonNegative(Input.Top);
        default: return 0.0;
        }
    }

    void SetEdgePreference(ERegion Edge, double Value)
    {
        switch (Edge)
        {
        case ERegion::Left: Preferred.Left = Value; break;
        case ERegion::Right: Preferred.Right = Value; break;
        case ERegion::Top: Preferred.Top = Value; break;
        case ERegion::Bottom: Preferred.Bottom = Value; break;
        default: break;
        }
    }

    bool MatchesDrag(const FPointerEvent& Event) const
    {
        return Drag.IsSet() && Drag->User == Event.GetUserIndex() &&
            Drag->Pointer == Event.GetPointerIndex();
    }

    FReply BeginResize(const FGeometry&, const FPointerEvent& Event, ERegion Edge)
    {
        if (Drag.IsSet() || Event.IsTouchEvent() || Event.GetEffectingButton() != EKeys::LeftMouseButton || !HasDockContent(Edge))
        {
            return FReply::Unhandled();
        }

        const FGeometry& Geometry = GetTickSpaceGeometry();
        const FLayout Layout = Resolve(BuildInput(Geometry.GetLocalSize()));
        const FRegion& Region = Layout[Edge];
        const double Extent = IsSide(Edge) ? Region.Size.X : Region.Size.Y;
        Drag = FDrag{Edge, PointerCoordinate(Geometry, Event, Edge), Extent,
            static_cast<int32>(Event.GetUserIndex()), Event.GetPointerIndex()};
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    FInput BuildInput(FVector2D Viewport) const
    {
        FInput Input = Preferred;
        Input.Viewport = Viewport;
        if (!HasDockContent(ERegion::Left)) { Input.Left = 0; }
        if (!HasDockContent(ERegion::Right)) { Input.Right = 0; }
        if (!HasDockContent(ERegion::Top)) { Input.Top = 0; }
        if (!HasDockContent(ERegion::Bottom)) { Input.Bottom = 0; }
        return Input;
    }

    void ArrangeGrip(const FGeometry& Geometry, FArrangedChildren& Arranged,
        const FLayout& Layout, ERegion Edge) const
    {
        const FRegion& Region = Layout[Edge];
        if (Region.Size.X <= 0 || Region.Size.Y <= 0)
        {
            return;
        }

        FVector2D Position = Region.Position;
        FVector2D Size = Region.Size;
        if (IsSide(Edge))
        {
            Size.X = FMath::Min(GripWidth, Size.X);
            if (Edge == ERegion::Left) { Position.X += Region.Size.X - Size.X; }
        }
        else
        {
            Size.Y = FMath::Min(GripWidth, Size.Y);
            if (Edge == ERegion::Top) { Position.Y += Region.Size.Y - Size.Y; }
        }
        Arranged.AddWidget(Geometry.MakeChild(Grips[static_cast<int32>(Edge)], Size,
            FSlateLayoutTransform(FVector2f(Position))));
    }

    bool HasDockContent(ERegion Region) const
    {
        const int32 Index = Regions.IndexOfByKey(Region);
        const auto* Panel = Sources.IsValidIndex(Index) ? Cast<UPanelWidget>(Sources[Index].Get()) : nullptr;
        return Panel && Panel->GetChildrenCount() > 0;
    }

    TSlotlessChildren<SWidget> Children;
    TArray<TSharedRef<SWidget>> RegionWidgets;
    TArray<TSharedRef<SWidget>> Grips;
    TOptional<FDrag> Drag;
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
    if (LayoutPanel)
    {
        LayoutPanel->CancelResize();
    }
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
    LayoutPanel->RefreshChildOrder();
}

#if WITH_EDITOR
const FText UWpDockLayoutPanel::GetPaletteCategory()
{
    return NSLOCTEXT("WindowingPlugin", "DockLayoutPalette", "Windowing");
}
#endif