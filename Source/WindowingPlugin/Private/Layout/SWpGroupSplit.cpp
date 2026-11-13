// Developed by Neko Creative Technologies

#include "Layout/SWpGroupSplit.h"

#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "InputCoreTypes.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"

SWpGroupSplit::SWpGroupSplit() : Children(this) {}

void SWpGroupSplit::Construct(const FArguments& Args)
{
    bHorizontal = Args._Horizontal;
}

void SWpGroupSplit::AddPanel(FGuid Id, const TSharedRef<SWidget>& Widget, bool bCollapsed)
{
    CancelResize();
    static const FSlateColorBrush Brush(FLinearColor(0.24f, 0.24f, 0.24f));
    const auto Grip = SNew(SBorder).BorderImage(&Brush).Padding(0.f)
        .OnMouseButtonDown_Lambda([Weak = TWeakPtr<SWpGroupSplit>(SharedThis(this)), Id](
            const FGeometry&, const FPointerEvent& Event)
        {
            if (const auto Split = Weak.Pin())
            {
                return Split->BeginResize(Event, Id);
            }
            return FReply::Unhandled();
        });
    Grip->SetCursor(TOptional<EMouseCursor::Type>(
        bHorizontal ? EMouseCursor::ResizeLeftRight : EMouseCursor::ResizeUpDown));
    const auto Clip = SNew(SBox).Clipping(EWidgetClipping::ClipToBoundsAlways)[Widget];
    Members.Add({Id, Clip, Grip, 1.0, bCollapsed});
    RebuildChildren();
}

void SWpGroupSplit::RemovePanel(FGuid Id)
{
    CancelResize();
    Members.RemoveAll([Id](const FMember& Member) { return Member.Id == Id; });
    RebuildChildren();
}

void SWpGroupSplit::SetPanelCollapsed(FGuid Id, bool bCollapsed)
{
    auto* Member = Members.FindByPredicate([Id](const FMember& Item) { return Item.Id == Id; });
    if (Member && Member->bCollapsed != bCollapsed)
    {
        CancelResize();
        Member->bCollapsed = bCollapsed;
        Invalidate(EInvalidateWidgetReason::Layout);
    }
}

void SWpGroupSplit::CancelResize()
{
    if (Drag.IsSet())
    {
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
}

FChildren* SWpGroupSplit::GetChildren()
{
    return &Children;
}

void SWpGroupSplit::RebuildChildren()
{
    Children.Empty();
    for (const FMember& Member : Members)
    {
        Children.Add(Member.Widget);
        Children.Add(Member.Grip);
    }
}

bool SWpGroupSplit::HasGrip(int32 Index) const
{
    return Members.IsValidIndex(Index + 1) &&
        !Members[Index].bCollapsed && !Members[Index + 1].bCollapsed;
}

double SWpGroupSplit::Axis(FVector2D Size) const
{
    return bHorizontal ? Size.X : Size.Y;
}

double SWpGroupSplit::Minimum(const FMember& Member) const
{
    const double Desired = Axis(Member.Widget->GetDesiredSize());
    return Member.bCollapsed ? Desired : PanelMinimum;
}

double SWpGroupSplit::MinimumLength() const
{
    double Length = 0.0;
    for (int32 Index = 0; Index < Members.Num(); ++Index)
    {
        Length += Minimum(Members[Index]) + (HasGrip(Index) ? GripSize : 0.0);
    }
    return Length;
}

void SWpGroupSplit::Measure(double Available, TArray<double>& Sizes) const
{
    double TotalWeight = 0.0;
    int32 ExpandedCount = 0;
    for (const FMember& Member : Members)
    {
        if (!Member.bCollapsed)
        {
            TotalWeight += Member.Weight;
            ++ExpandedCount;
        }
    }

    const double Extra = FMath::Max(0.0, Available - MinimumLength());
    Sizes.Reset(Members.Num());
    for (const FMember& Member : Members)
    {
        double Share = 0.0;
        if (!Member.bCollapsed)
        {
            Share = TotalWeight > 0.0 ? Extra * Member.Weight / TotalWeight : Extra / ExpandedCount;
        }
        Sizes.Add(Minimum(Member) + Share);
    }
}

FVector2D SWpGroupSplit::ComputeDesiredSize(float) const
{
    double Cross = 0.0;
    for (const FMember& Member : Members)
    {
        const FVector2D Desired = Member.Widget->GetDesiredSize();
        Cross = FMath::Max(Cross, bHorizontal ? Desired.Y : Desired.X);
    }
    return bHorizontal ? FVector2D(MinimumLength(), Cross) : FVector2D(Cross, MinimumLength());
}

void SWpGroupSplit::OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Arranged) const
{
    const FVector2D LocalSize = Geometry.GetLocalSize();
    const double Cross = bHorizontal ? LocalSize.Y : LocalSize.X;
    TArray<double> Sizes;
    Measure(Axis(LocalSize), Sizes);
    double Offset = 0.0;
    const auto Arrange = [&](const TSharedRef<SWidget>& Widget, double Extent)
    {
        const FVector2D Position = bHorizontal ? FVector2D(Offset, 0) : FVector2D(0, Offset);
        const FVector2D Size = bHorizontal ? FVector2D(Extent, Cross) : FVector2D(Cross, Extent);
        Arranged.AddWidget(Geometry.MakeChild(Widget, Size, FSlateLayoutTransform(FVector2f(Position))));
        Offset += Extent;
    };
    for (int32 Index = 0; Index < Members.Num(); ++Index)
    {
        Arrange(Members[Index].Widget, Sizes[Index]);
        if (HasGrip(Index))
        {
            Arrange(Members[Index].Grip, GripSize);
        }
    }
}

FReply SWpGroupSplit::BeginResize(const FPointerEvent& Event, FGuid Id)
{
    const int32 Index = Members.IndexOfByPredicate([Id](const FMember& Member) { return Member.Id == Id; });
    if (Drag.IsSet() || Index == INDEX_NONE || !HasGrip(Index) || Event.IsTouchEvent() ||
        Event.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return FReply::Unhandled();
    }

    const FGeometry& Geometry = GetTickSpaceGeometry();
    TArray<double> Sizes;
    Measure(Axis(Geometry.GetLocalSize()), Sizes);
    const double FirstExtra = Sizes[Index] - Minimum(Members[Index]);
    const double SecondExtra = Sizes[Index + 1] - Minimum(Members[Index + 1]);
    const double TotalWeight = Members[Index].Weight + Members[Index + 1].Weight;
    if (FirstExtra + SecondExtra <= UE_SMALL_NUMBER || TotalWeight <= 0.0)
    {
        return FReply::Unhandled();
    }

    Drag = FDrag{Index, static_cast<int32>(Event.GetUserIndex()), Event.GetPointerIndex(),
        Axis(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())), FirstExtra,
        FirstExtra + SecondExtra, TotalWeight};
    return FReply::Handled().CaptureMouse(SharedThis(this));
}

bool SWpGroupSplit::Matches(const FPointerEvent& Event) const
{
    return Drag.IsSet() && Drag->User == Event.GetUserIndex() && Drag->Pointer == Event.GetPointerIndex();
}

FReply SWpGroupSplit::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!Matches(Event))
    {
        return FReply::Unhandled();
    }

    const double Delta = Axis(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())) - Drag->Start;
    const double Fraction = FMath::Clamp((Drag->FirstExtra + Delta) / Drag->TotalExtra, 0.000001, 0.999999);
    Members[Drag->Index].Weight = Drag->TotalWeight * Fraction;
    Members[Drag->Index + 1].Weight = Drag->TotalWeight - Members[Drag->Index].Weight;
    Invalidate(EInvalidateWidgetReason::Layout);
    return FReply::Handled();
}

FReply SWpGroupSplit::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
    if (!Matches(Event) || Event.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return FReply::Unhandled();
    }
    Drag.Reset();
    return FReply::Handled().ReleaseMouseCapture();
}

void SWpGroupSplit::OnMouseCaptureLost(const FCaptureLostEvent&)
{
    Drag.Reset();
}