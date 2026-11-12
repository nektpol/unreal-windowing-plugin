// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "WpWindowTypes.h"
#include "Blueprint/UserWidget.h"
#include "WpDockWorkspace.generated.h"

class UNativeWidgetHost;
class UVerticalBox;
class UHorizontalBox;
/**
 *
 */
UCLASS(Abstract, Blueprintable)
class WINDOWINGPLUGIN_API UWpDockWorkspace : public UUserWidget
{
	GENERATED_BODY()
public:
	UNativeWidgetHost* AddDockSlot(EWpDockSide Side);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Windowing", meta=(BindWidget))
	TObjectPtr<UVerticalBox> LeftDock;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Windowing", meta=(BindWidget))
	TObjectPtr<UVerticalBox> RightDock;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Windowing", meta=(BindWidget))
	TObjectPtr<UHorizontalBox> TopDock;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Windowing", meta=(BindWidget))
	TObjectPtr<UHorizontalBox> BottomDock;
};
