// Developed by Neko Creative Technologies

#pragma once

#include "Components/PanelWidget.h"
#include "WpDockLayoutPanel.generated.h"

class SWpDockLayoutPanel;

UCLASS(meta=(DisplayName="Dock Layout Panel"))
class WINDOWINGPLUGIN_API UWpDockLayoutPanel : public UPanelWidget
{
	GENERATED_BODY()

public:
	UWpDockLayoutPanel(const FObjectInitializer& ObjectInitializer);
	void ReleaseSlateResources(bool bReleaseChildren) override;

	UPROPERTY(EditAnywhere, Category="Dock Layout")
	FVector2D CenterMinimum = FVector2D(320, 180);

	UPROPERTY(EditAnywhere, Category="Dock Layout")
	FVector2D PreferredSideWidths = FVector2D(240, 240);

	UPROPERTY(EditAnywhere, Category="Dock Layout")
	FVector2D PreferredRowHeights = FVector2D(160, 160);

#if WITH_EDITOR
	const FText GetPaletteCategory() override;
#endif

protected:
	TSharedRef<SWidget> RebuildWidget() override;
	void SynchronizeProperties() override;
	void OnSlotAdded(UPanelSlot* Slot) override;
	void OnSlotRemoved(UPanelSlot* Slot) override;

private:
	void RefreshRegions();
	TSharedPtr<SWpDockLayoutPanel> LayoutPanel;
};
