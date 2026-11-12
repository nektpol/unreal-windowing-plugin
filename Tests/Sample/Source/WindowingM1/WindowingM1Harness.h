#pragma once

#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "WpWindowTypes.h"
#include "WpDockWorkspace.h"
#include "WindowingM1Harness.generated.h"

class SButton;
class SEditableTextBox;
class UWpWindowingSubsystem;

UCLASS()
class UWindowingM1Widget final : public UUserWidget
{
    GENERATED_BODY()

public:
    static TWeakObjectPtr<UWindowingM1Widget> LastBuilt;
    static FSimpleDelegate DuringBuild;

    TWeakPtr<SButton> Button;
    TWeakPtr<SEditableTextBox> Input;
    int32 Clicks = 0;

protected:
    TSharedRef<SWidget> RebuildWidget() override;
};

UCLASS()
class UWindowingM1GameInstance final : public UGameInstance
{
    GENERATED_BODY()

    void Init() override;
    void Shutdown() override;

    bool TickTests(float DeltaTime);
    bool TickDockingTest();
    void OpenInitialWindows();
    void CheckInputAndRequestCloses();
    void CheckInitialCloses();
    void CheckSessionIsolation();
    void CheckRollback();
    void CheckRegistrationRollback();
    void RunStressCycle();
    void Finish();
    bool Expect(bool Condition, const FString& Message);
    FWpWindowHandle Open(UWpWindowingSubsystem* Subsystem, const TCHAR* Title);
    void Close(FWpWindowHandle Handle);

    UFUNCTION()
    void OnOpened(FWpWindowHandle Handle);

    UFUNCTION()
    void OnClosed(FWpWindowClosedEvent Event);

    enum class EPhase : uint8
    {
        Start, Input, InitialCloses, InputAfterClose, Isolation, Rollback, RegistrationRollback, Stress
    };

    FTSTicker::FDelegateHandle TickHandle;
    FString ReportPath;
    FString Scenario;
    TArray<FString> Failures;
    TArray<FWpWindowHandle> Opened;
    TArray<FWpWindowClosedEvent> Closed;
    FWpWindowHandle A;
    FWpWindowHandle B;
    FWpWindowHandle C;
    FWpWindowHandle PreviousCycle;
    FWpWindowHandle CloseFromOpened;
    FWpWindowHandle CloseFromClosed;
    TWeakObjectPtr<UWindowingM1Widget> WidgetB;
    TWeakObjectPtr<UWindowingM1Widget> WidgetC;
    TWeakObjectPtr<UWindowingM1Widget> PreviousWidget;
    EPhase Phase = EPhase::Start;
    int32 Cycles = 0;
    double Deadline = 0.0;
    bool bInputSent = false;
    bool bTextSent = false;
};

UCLASS()
class UWindowingM51Workspace final : public UWpDockWorkspace
{
    GENERATED_BODY()
protected:
    TSharedRef<SWidget> RebuildWidget() override;
};