// Developed by Neko Creative Technologies


#include "WpDockWorkspace.h"

#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/NativeWidgetHost.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"

UNativeWidgetHost* UWpDockWorkspace::AddDockSlot(EWpDockSide Side)
{
	UPanelWidget* Stack = nullptr;
	switch (Side)
	{
	case EWpDockSide::Left:   Stack = LeftDock.Get(); break;
	case EWpDockSide::Right:  Stack = RightDock.Get(); break;
	case EWpDockSide::Top:    Stack = TopDock.Get(); break;
	case EWpDockSide::Bottom: Stack = BottomDock.Get(); break;
	}
	if (!IsValid(Stack))
	{
		return nullptr;
	}

	UNativeWidgetHost* DockHost = NewObject<UNativeWidgetHost>(this);
	UPanelSlot* HostSlot = Stack->AddChild(DockHost);
	if (auto* RowSlot = Cast<UHorizontalBoxSlot>(HostSlot))
	{
		RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	else if (auto* ColumnSlot = Cast<UVerticalBoxSlot>(HostSlot))
	{
		ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	return HostSlot ? DockHost : nullptr;
}
