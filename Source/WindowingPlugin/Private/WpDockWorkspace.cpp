// Developed by Neko Creative Technologies


#include "WpDockWorkspace.h"

#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/NativeWidgetHost.h"

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
	return Stack->AddChild(DockHost) ? DockHost : nullptr;
}
