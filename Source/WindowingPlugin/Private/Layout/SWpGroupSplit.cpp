// Developed by Neko Creative Technologies

#include "Layout/SWpGroupSplit.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "InputCoreTypes.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/WpResizeGrip.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"

SWpGroupSplit::SWpGroupSplit() : Children(this) {}

void SWpGroupSplit::Construct(const FArguments& Args)
{
    bHorizontal = Args._Horizontal;
}

SWpGroupSplit::FMember SWpGroupSplit::MakeMember(
    FGuid Id, const TSharedRef<SWidget>& Widget, bool bCollapsed)
{
    const float Inset = WpResizeGrip::LineInset * 0.5f;
    const auto Grip = WpResizeGrip::Make(bHorizontal,
        bHorizontal ? FMargin(Inset, 0.f) : FMargin(0.f, Inset));
    Grip->SetOnMouseButtonDown(FPointerEventHandler::CreateLambda(
        [Weak = TWeakPtr<SWpGroupSplit>(SharedThis(this)), Id](
            const FGeometry&, const FPointerEvent& Event)
        {
            if (const auto Split = Weak.Pin())
            {
                return Split->BeginResize(Event, Id);
            }
            return FReply::Unhandled();
        }));
    const auto Clip = SNew(SBox).Clipping(EWidgetClipping::ClipToBoundsAlways)[Widget];
    return {Id, Clip, Grip, 1.0, bCollapsed, nullptr};
}

void SWpGroupSplit::AddPanel(FGuid Id, const TSharedRef<SWidget>& Widget, bool bCollapsed)
{
    CancelResize();
    Members.Add(MakeMember(Id, Widget, bCollapsed));
    RebuildChildren();
}

int32 SWpGroupSplit::FindMember(FGuid Id) const
{
    return Members.IndexOfByPredicate([Id](const FMember& Member) { return Member.Id == Id; });
}

bool SWpGroupSplit::ContainsPanel(FGuid Id) const
{
    const int32 Index = FindMember(Id);
    if (Index != INDEX_NONE && !Members[Index].Branch)
    {
        return true;
    }
    return Members.ContainsByPredicate([Id](const FMember& Member)
    {
        return Member.Branch && Member.Branch->ContainsPanel(Id);
    });
}

bool SWpGroupSplit::InsertPanelBeside(FGuid TargetId, FGuid NewId, const TSharedRef<SWidget>& NewWidget,
    bool bNewCollapsed, bool bHorizontalDrop, bool bInsertBefore)
{
    if (!TargetId.IsValid() || !NewId.IsValid() || TargetId == NewId || ContainsPanel(NewId))
    {
        return false;
    }

    const int32 Index = FindMember(TargetId);
    if (Index == INDEX_NONE)
    {
        for (const FMember& Member : Members)
        {
            if (Member.Branch && Member.Branch->InsertPanelBeside(
                TargetId, NewId, NewWidget, bNewCollapsed, bHorizontalDrop, bInsertBefore))
            {
                CancelResize();
                return true;
            }
        }
        return false;
    }

    FMember& Target = Members[Index];
    if (Target.Branch || Target.bCollapsed)
    {
        return false;
    }

    CancelResize();
    if (bHorizontal == bHorizontalDrop)
    {
        FMember Incoming = MakeMember(NewId, NewWidget, bNewCollapsed);
        Incoming.Weight = Target.Weight * 0.5;
        if (!bNewCollapsed)
        {
            Target.Weight -= Incoming.Weight;
        }
        Members.Insert(MoveTemp(Incoming), bInsertBefore ? Index : Index + 1);
        RebuildChildren();
        return true;
    }

    const double OuterWeight = Target.Weight;
    const auto TargetContent = Target.Widget->GetChildren()->GetChildAt(0);
    Target.Widget->SetContent(SNullWidget::NullWidget);
    const auto Nested = SNew(SWpGroupSplit).Horizontal(bHorizontalDrop);
    if (bInsertBefore)
    {
        Nested->AddPanel(NewId, NewWidget, bNewCollapsed);
        Nested->AddPanel(TargetId, TargetContent, false);
    }
    else
    {
        Nested->AddPanel(TargetId, TargetContent, false);
        Nested->AddPanel(NewId, NewWidget, bNewCollapsed);
    }
    const auto Scroll = SNew(SScrollBox)
        .Orientation(bHorizontalDrop ? Orient_Horizontal : Orient_Vertical)
        .ConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible)
        + SScrollBox::Slot().FillContentSize(1.f)
        [
            Nested
        ];

    FMember Branch = MakeMember(FGuid::NewGuid(), Scroll, false);
    Branch.Branch = Nested;
    Branch.Weight = OuterWeight;
    Target = MoveTemp(Branch);
    RebuildChildren();
    return true;
}

bool SWpGroupSplit::NestAdjacent(FGuid First, FGuid Second)
{
    const int32 Index = FindMember(First);
    if (Index == INDEX_NONE)
    {
        for (const FMember& Member : Members)
        {
            if (Member.Branch && Member.Branch->NestAdjacent(First, Second))
            {
                CancelResize();
                return true;
            }
        }
        return false;
    }
    const int32 Next = FindMember(Second);
    if (Next == INDEX_NONE || FMath::Abs(Index - Next) != 1 ||
        Members[Index].Branch || Members[Next].Branch)
    {
        return false;
    }

    CancelResize();
    const int32 Start = FMath::Min(Index, Next);
    FMember& A = Members[Start];
    FMember& B = Members[Start + 1];
    const double OuterWeight = A.Weight + B.Weight;
    const auto FirstContent = A.Widget->GetChildren()->GetChildAt(0);
    const auto SecondContent = B.Widget->GetChildren()->GetChildAt(0);
    A.Widget->SetContent(SNullWidget::NullWidget);
    B.Widget->SetContent(SNullWidget::NullWidget);

    const auto Nested = SNew(SWpGroupSplit).Horizontal(!bHorizontal);
    Nested->AddPanel(A.Id, FirstContent, A.bCollapsed);
    Nested->AddPanel(B.Id, SecondContent, B.bCollapsed);
    const auto Scroll = SNew(SScrollBox)
        .Orientation(bHorizontal ? Orient_Vertical : Orient_Horizontal)
        .ConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible)
        + SScrollBox::Slot().FillContentSize(1.f)
        [
            Nested
        ];

    // Use a fresh member ID for the branch and create its divider through AddPanel.
    Members.RemoveAt(Start, 2);
    AddPanel(FGuid::NewGuid(), Scroll, false);
    FMember Branch = Members.Pop();
    Branch.Branch = Nested;
    Branch.Weight = OuterWeight;
    Members.Insert(MoveTemp(Branch), Start);
    RebuildChildren();
    return true;
}

void SWpGroupSplit::RemovePanel(FGuid Id)
{
    CancelResize();
    const int32 Index = FindMember(Id);
    if (Index != INDEX_NONE)
    {
        Members.RemoveAt(Index);
        RebuildChildren();
        return;
    }

    for (int32 BranchIndex = 0; BranchIndex < Members.Num(); ++BranchIndex)
    {
        FMember& Member = Members[BranchIndex];
        const auto Nested = Member.Branch;
        if (!Nested)
        {
            continue;
        }
        Nested->RemovePanel(Id);
        if (Nested->Members.IsEmpty())
        {
            Members.RemoveAt(BranchIndex--);
            RebuildChildren();
        }
        else if (Nested->Members.Num() == 1 && !Nested->Members[0].Branch)
        {
            FMember Remaining = Nested->Members.Pop();
            Nested->RebuildChildren();
            // Recreate the grip so its weak callback targets this split.
            const double Weight = Member.Weight;
            const auto Content = Remaining.Widget->GetChildren()->GetChildAt(0);
            Remaining.Widget->SetContent(SNullWidget::NullWidget);
            Members.RemoveAt(BranchIndex);
            AddPanel(Remaining.Id, Content, Remaining.bCollapsed);
            FMember Promoted = Members.Pop();
            Promoted.Weight = Weight;
            Members.Insert(MoveTemp(Promoted), BranchIndex);
            RebuildChildren();
        }
    }
}

void SWpGroupSplit::SetPanelCollapsed(FGuid Id, bool bCollapsed)
{
    const int32 Index = FindMember(Id);
    if (Index != INDEX_NONE)
    {
        auto& Member = Members[Index];
        if (Member.bCollapsed != bCollapsed)
        {
            CancelResize();
            Member.bCollapsed = bCollapsed;
            Invalidate(EInvalidateWidgetReason::Layout);
        }
        return;
    }
    CancelResize();
    for (const FMember& Member : Members)
    {
        if (Member.Branch)
        {
            Member.Branch->SetPanelCollapsed(Id, bCollapsed);
        }
    }
}

void SWpGroupSplit::CancelResize()
{
    for (const FMember& Member : Members)
    {
        if (Member.Branch)
        {
            Member.Branch->CancelResize();
        }
    }
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
        Length += Minimum(Members[Index]) + (HasGrip(Index) ? WpResizeGrip::HitSize : 0.0);
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
            Arrange(Members[Index].Grip, WpResizeGrip::HitSize);
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
