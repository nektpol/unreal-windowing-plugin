// Developed by Neko Creative Technologies

#pragma once

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Input/DragAndDrop.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "WpWindowTypes.h"

class UWpDockLayoutPanel;

struct FWpDockPanelTarget
{
    FGuid SessionId;
    FWpWindowHandle Handle;
    TWeakPtr<SWidget> ContentSlot;
};

inline int32 WpPaintDockDropPreview(const FGeometry& Geometry, FSlateWindowElementList& Elements,
    int32 Layer, const FWidgetStyle& Style, FVector2D Position, FVector2D Size)
{
    static const FSlateRoundedBoxBrush Brush(
        FLinearColor(0.04f, 0.35f, 0.86f, 0.50f), 4.f, FLinearColor(0.18f, 0.72f, 1.f), 3.f);
    const FPaintGeometry PaintGeometry = Geometry.ToPaintGeometry(
        FVector2f(Size), FSlateLayoutTransform(FVector2f(Position)));
    FSlateDrawElement::MakeBox(Elements, Layer, PaintGeometry, &Brush,
        ESlateDrawEffect::None, Brush.GetTint(Style) * Style.GetColorAndOpacityTint());
    return Layer;
}

class FWpDockDragOperation final : public FDragDropOperation
{
public:
    DRAG_DROP_OPERATOR_TYPE(FWpDockDragOperation, FDragDropOperation)

    using FCanDock = TFunction<bool(const UWpDockLayoutPanel&)>;
    using FDock = TFunction<bool(const UWpDockLayoutPanel&, EWpDockSide)>;

    using FCanDockBeside = TFunction<bool(const FWpDockPanelTarget&, FVector2D)>;
    using FDockBeside = TFunction<bool(const FWpDockPanelTarget&, FVector2D, EWpDockSide)>;

    static TSharedRef<FWpDockDragOperation> New(const FText& PanelTitle, FCanDock CanDock,
        FDock Dock, FCanDockBeside CanDockBeside, FDockBeside DockBeside)
    {
        const auto Operation = MakeShared<FWpDockDragOperation>();
        Operation->PanelTitle = PanelTitle;
        Operation->CanDockCallback = MoveTemp(CanDock);
        Operation->DockCallback = MoveTemp(Dock);
        Operation->CanDockBesideCallback = MoveTemp(CanDockBeside);
        Operation->DockBesideCallback = MoveTemp(DockBeside);
        Operation->bCreateNewWindow = true;
        Operation->MouseCursor = EMouseCursor::GrabHandClosed;
        Operation->Construct();
        return Operation;
    }

    TSharedPtr<SWidget> GetDefaultDecorator() const override
    {
        static const FSlateRoundedBoxBrush CardBrush(
            FLinearColor(0.025f, 0.065f, 0.13f), 6.f, FLinearColor(0.05f, 0.65f, 1.f), 2.f);
        const FText Label = PanelTitle.IsEmpty()
            ? NSLOCTEXT("WindowingPlugin", "MovingUnnamedPanel", "Moving panel")
            : FText::Format(NSLOCTEXT("WindowingPlugin", "MovingPanel", "Moving {0}"), PanelTitle);

        return SNew(SBorder)
            .Visibility(EVisibility::HitTestInvisible)
            .BorderImage(&CardBrush)
            .Padding(FMargin(14.f, 9.f))
            [
                SNew(SBox).MaxDesiredWidth(320.f)
                [
                    SNew(STextBlock)
                    .Text(Label)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                    .ColorAndOpacity(FSlateColor(FLinearColor::White))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]
            ];
    }

    bool CanDockIn(const UWpDockLayoutPanel& Panel) const
    {
        return CanDockCallback && CanDockCallback(Panel);
    }

    bool DockIn(const UWpDockLayoutPanel& Panel, EWpDockSide Side) const
    {
        return DockCallback && DockCallback(Panel, Side);
    }

    bool CanDockBeside(const FWpDockPanelTarget& Target, FVector2D Screen) const
    {
        return CanDockBesideCallback && CanDockBesideCallback(Target, Screen);
    }

    bool DockBeside(const FWpDockPanelTarget& Target, FVector2D Screen, EWpDockSide Side) const
    {
        return DockBesideCallback && DockBesideCallback(Target, Screen, Side);
    }

    bool OwnsPreview(const SWidget& Widget) const
    {
        return PreviewOwner.Pin().Get() == &Widget;
    }

    void SetPreviewOwner(const TSharedRef<SWidget>& Widget)
    {
        const auto Previous = PreviewOwner.Pin();
        if (Previous.Get() == &Widget.Get())
        {
            return;
        }

        PreviewOwner = Widget;
        if (Previous)
        {
            Previous->Invalidate(EInvalidateWidgetReason::Paint);
        }
    }

    void ReleasePreviewOwner(const SWidget& Widget)
    {
        if (OwnsPreview(Widget))
        {
            PreviewOwner.Reset();
        }
    }

    void OnDrop(bool bHandled, const FPointerEvent& Event) override
    {
        const auto Previous = PreviewOwner.Pin();
        PreviewOwner.Reset();
        if (Previous)
        {
            Previous->Invalidate(EInvalidateWidgetReason::Paint);
        }
        FDragDropOperation::OnDrop(bHandled, Event);
    }

private:
    FText PanelTitle;
    FCanDock CanDockCallback;
    FDock DockCallback;
    FCanDockBeside CanDockBesideCallback;
    FDockBeside DockBesideCallback;
    TWeakPtr<SWidget> PreviewOwner;
};

class SWpDockDragTitle final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWpDockDragTitle) {}
        SLATE_ARGUMENT(TFunction<FReply()>, BeginDrag)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        BeginDrag = Args._BeginDrag;
        ChildSlot[Args._Content.Widget];
    }

    FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
    {
        if (!BeginDrag || Event.IsTouchEvent() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
        {
            return FReply::Unhandled();
        }
        return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
    }

    FReply OnDragDetected(const FGeometry&, const FPointerEvent& Event) override
    {
        if (!BeginDrag || !Event.IsMouseButtonDown(EKeys::LeftMouseButton))
        {
            return FReply::Unhandled();
        }
        return BeginDrag();
    }

private:
    TFunction<FReply()> BeginDrag;
};