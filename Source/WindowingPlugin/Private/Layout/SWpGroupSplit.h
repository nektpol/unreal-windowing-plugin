// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "Layout/Children.h"
#include "Widgets/SPanel.h"

class SBox;

class SWpGroupSplit final : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SWpGroupSplit) {}
	SLATE_ARGUMENT(bool, Horizontal)
SLATE_END_ARGS()

SWpGroupSplit();
	void Construct(const FArguments& Args);
	void AddPanel(FGuid Id, const TSharedRef<SWidget>& Widget, bool bCollapsed);
	void RemovePanel(FGuid Id);
	void SetPanelCollapsed(FGuid Id, bool bCollapsed);
	bool NestAdjacent(FGuid First, FGuid Second);
	void CancelResize();

	FChildren* GetChildren() override;
	FVector2D ComputeDesiredSize(float Scale) const override;
	void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Arranged) const override;
	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	void OnMouseCaptureLost(const FCaptureLostEvent& Event) override;

private:
	struct FMember
	{
		FGuid Id;
		TSharedRef<SBox> Widget;
		TSharedRef<SWidget> Grip;
		double Weight = 1.0;
		bool bCollapsed = false;
		TSharedPtr<SWpGroupSplit> Branch;
	};

	struct FDrag
	{
		int32 Index;
		int32 User;
		uint32 Pointer;
		double Start;
		double FirstExtra;
		double TotalExtra;
		double TotalWeight;
	};

	static constexpr double GripSize = 6.0;
	static constexpr double PanelMinimum = 96.0;

	void RebuildChildren();
	int32 FindMember(FGuid Id) const;
	bool HasGrip(int32 Index) const;
	double Axis(FVector2D Size) const;
	double Minimum(const FMember& Member) const;
	double MinimumLength() const;
	void Measure(double Available, TArray<double>& Sizes) const;
	FReply BeginResize(const FPointerEvent& Event, FGuid Id);
	bool Matches(const FPointerEvent& Event) const;

	TSlotlessChildren<SWidget> Children;
	TArray<FMember> Members;
	TOptional<FDrag> Drag;
	bool bHorizontal = false;
};