// Developed by Neko Creative Technologies

#pragma once

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Docking/WpDockDragOperation.h"
#include "Rendering/DrawElements.h"

inline TOptional<EWpDockSide> WpFindPanelDropSide(FVector2D Position, FVector2D Size)
{
    if (Size.X <= 0.0 || Size.Y <= 0.0 ||
        !FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y) ||
        Position.X < 0.0 || Position.Y < 0.0 || Position.X >= Size.X || Position.Y >= Size.Y)
    {
        return {};
    }

    const double X = Position.X / Size.X;
    const double Y = Position.Y / Size.Y;
    const double Distances[] = {X, 1.0 - X, Y, 1.0 - Y};
    const EWpDockSide Sides[] = {EWpDockSide::Left, EWpDockSide::Right, EWpDockSide::Top, EWpDockSide::Bottom};
    TOptional<EWpDockSide> Result;
    double Closest = 0.25;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sides); ++Index)
    {
        if (Distances[Index] < Closest)
        {
            Closest = Distances[Index];
            Result = Sides[Index];
        }
    }
    return Result;
}

class SWpDockPanelDropTarget final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWpDockPanelDropTarget) {}
        SLATE_ARGUMENT(FWpDockPanelTarget, Target)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Target = Args._Target;
        ChildSlot[Args._Content.Widget];
    }

    FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override
    {
        const auto Operation = Event.GetOperationAs<FWpDockDragOperation>();
        const FVector2D Screen = Event.GetScreenSpacePosition();
        const auto Side = WpFindPanelDropSide(Geometry.AbsoluteToLocal(Screen), Geometry.GetLocalSize());
        if (!Operation || !Side.IsSet() || !Operation->CanDockBeside(Target, Screen))
        {
            ClearPreview();
            return FReply::Unhandled();
        }

        if (PreviewOperation.Pin() != Operation || PreviewSide != Side || !Operation->OwnsPreview(*this))
        {
            PreviewOperation = Operation;
            PreviewSide = Side;
            Operation->SetPreviewOwner(SharedThis(this));
            Invalidate(EInvalidateWidgetReason::Paint);
        }
        return FReply::Handled();
    }

    void OnDragLeave(const FDragDropEvent& Event) override
    {
        if (PreviewOperation.Pin() == Event.GetOperationAs<FWpDockDragOperation>())
        {
            ClearPreview();
        }
    }

    FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override
    {
        const auto Operation = Event.GetOperationAs<FWpDockDragOperation>();
        const FVector2D Screen = Event.GetScreenSpacePosition();
        const auto Side = WpFindPanelDropSide(Geometry.AbsoluteToLocal(Screen), Geometry.GetLocalSize());
        ClearPreview();
        return Operation && Side.IsSet() && Operation->DockBeside(Target, Screen, Side.GetValue())
            ? FReply::Handled() : FReply::Unhandled();
    }

    int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style,
        bool bParentEnabled) const override
    {
        const int32 ContentLayer = SCompoundWidget::OnPaint(
            Args, Geometry, Culling, Elements, Layer, Style, bParentEnabled);
        const auto Operation = PreviewOperation.Pin();
        if (!Operation || !Operation->OwnsPreview(*this) || !PreviewSide.IsSet())
        {
            return ContentLayer;
        }

        FVector2D Size = Geometry.GetLocalSize();
        FVector2D Position = FVector2D::ZeroVector;
        switch (PreviewSide.GetValue())
        {
        case EWpDockSide::Left:   Size.X *= 0.5; break;
        case EWpDockSide::Right:  Size.X *= 0.5; Position.X = Size.X; break;
        case EWpDockSide::Top:    Size.Y *= 0.5; break;
        case EWpDockSide::Bottom: Size.Y *= 0.5; Position.Y = Size.Y; break;
        }
        return WpPaintDockDropPreview(Geometry, Elements, ContentLayer + 1, Style, Position, Size);
    }

private:
    void ClearPreview()
    {
        if (const auto Operation = PreviewOperation.Pin())
        {
            Operation->ReleasePreviewOwner(*this);
        }
        if (PreviewSide.IsSet())
        {
            PreviewSide.Reset();
            PreviewOperation.Reset();
            Invalidate(EInvalidateWidgetReason::Paint);
        }
    }

    FWpDockPanelTarget Target;
    TWeakPtr<FWpDockDragOperation> PreviewOperation;
    TOptional<EWpDockSide> PreviewSide;
};