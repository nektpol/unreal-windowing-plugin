// Developed by Neko Creative Technologies

#pragma once

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Containers/Ticker.h"
#include "Docking/WpUndockDrop.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

class FWpUndockDragOperation final : public FDragDropOperation
{
public:
    DRAG_DROP_OPERATOR_TYPE(FWpUndockDragOperation, FDragDropOperation)

    using FResolve = TFunction<TOptional<FWpPlacementSnapshot>(FVector2D)>;
    using FCommit = TFunction<void(const FWpPlacementSnapshot&)>;
    using FIsCurrent = TFunction<bool()>;

    static TSharedRef<FWpUndockDragOperation> New(
        const FText& Title, FResolve Resolve, FCommit Commit, FIsCurrent IsCurrent)
    {
        const auto Operation = MakeShared<FWpUndockDragOperation>();
        Operation->Title = Title;
        Operation->ResolveDrop = MoveTemp(Resolve);
        Operation->CommitDrop = MoveTemp(Commit);
        Operation->IsCurrent = MoveTemp(IsCurrent);
        Operation->bCreateNewWindow = true;
        Operation->MouseCursor = EMouseCursor::GrabHandClosed;
        Operation->Construct();

        const TWeakPtr<FWpUndockDragOperation> Weak = Operation;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
        {
            const auto Active = Weak.Pin();
            if (!Active || !FSlateApplication::IsInitialized())
            {
                return false;
            }
            FSlateApplication& Slate = FSlateApplication::Get();
            if (Slate.GetDragDroppingContent().Get() != Active.Get())
            {
                return false;
            }
            if (!Active->IsCurrent())
            {
                Slate.CancelDragDrop();
                return false;
            }
            return true;
        }));
        return Operation;
    }

    TSharedPtr<SWidget> GetDefaultDecorator() const override
    {
        static const FSlateRoundedBoxBrush Brush(
            FLinearColor(0.025f, 0.065f, 0.13f), 6.f, FLinearColor(0.05f, 0.65f, 1.f), 2.f);
        return SNew(SBorder)
            .Visibility(EVisibility::HitTestInvisible)
            .BorderImage(&Brush)
            .Padding(FMargin(14.f, 9.f))
            [
                SNew(STextBlock)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                .ColorAndOpacity(FSlateColor(FLinearColor::White))
                .Text_Lambda([Weak = TWeakPtr<const FWpUndockDragOperation>(
                    StaticCastSharedRef<const FWpUndockDragOperation>(AsShared()))]
                {
                    const auto Operation = Weak.Pin();
                    return Operation ? Operation->GetLabel() : FText::GetEmpty();
                })
            ];
    }

    bool AffectedByPointerEvent(const FPointerEvent& Event) override
    {
        return !Event.IsTouchEvent() && Event.GetPointerIndex() == FSlateApplication::CursorPointerIndex &&
            (Event.GetEffectingButton() == EKeys::Invalid || Event.GetEffectingButton() == EKeys::LeftMouseButton);
    }

    void OnDragged(const FDragDropEvent& Event) override
    {
        Preview = ResolveDrop(Event.GetScreenSpacePosition());
        FDragDropOperation::OnDragged(Event);
    }

    void OnDrop(bool bHandled, const FPointerEvent& Event) override
    {
        Preview.Reset();
        const auto Placement = !bHandled && !Event.IsTouchEvent() &&
            Event.GetEffectingButton() == EKeys::LeftMouseButton && IsCurrent()
            ? ResolveDrop(Event.GetScreenSpacePosition()) : TOptional<FWpPlacementSnapshot>{};
        FDragDropOperation::OnDrop(bHandled, Event);
        if (Placement.IsSet())
        {
            CommitDrop(Placement.GetValue());
        }
    }

private:
    FText GetLabel() const
    {
        if (Preview.IsSet())
        {
            return FText::Format(NSLOCTEXT("WindowingPlugin", "UndockDragReady", "Float {0}  ({1} × {2})"),
                Title, FText::AsNumber(Preview->Normal.Width()), FText::AsNumber(Preview->Normal.Height()));
        }
        return FText::Format(NSLOCTEXT("WindowingPlugin", "UndockDragMoving", "Moving {0}"), Title);
    }

    FText Title;
    FResolve ResolveDrop;
    FCommit CommitDrop;
    FIsCurrent IsCurrent;
    TOptional<FWpPlacementSnapshot> Preview;
};