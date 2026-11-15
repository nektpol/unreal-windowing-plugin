// Developed by Neko Creative Technologies

#pragma once

#include "Input/DragAndDrop.h"
#include "InputCoreTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "WpWindowTypes.h"

class UWpDockLayoutPanel;

class FWpDockDragOperation final : public FDragDropOperation
{
public:
    DRAG_DROP_OPERATOR_TYPE(FWpDockDragOperation, FDragDropOperation)

    using FCanDock = TFunction<bool(const UWpDockLayoutPanel&)>;
    using FDock = TFunction<bool(const UWpDockLayoutPanel&, EWpDockSide)>;

    static TSharedRef<FWpDockDragOperation> New(FCanDock CanDock, FDock Dock)
    {
        const auto Operation = MakeShared<FWpDockDragOperation>();
        Operation->CanDockCallback = MoveTemp(CanDock);
        Operation->DockCallback = MoveTemp(Dock);
        Operation->bCreateNewWindow = false;
        Operation->MouseCursor = EMouseCursor::GrabHandClosed;
        Operation->Construct();
        return Operation;
    }

    bool CanDockIn(const UWpDockLayoutPanel& Panel) const
    {
        return CanDockCallback && CanDockCallback(Panel);
    }

    bool DockIn(const UWpDockLayoutPanel& Panel, EWpDockSide Side) const
    {
        return DockCallback && DockCallback(Panel, Side);
    }

private:
    FCanDock CanDockCallback;
    FDock DockCallback;
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