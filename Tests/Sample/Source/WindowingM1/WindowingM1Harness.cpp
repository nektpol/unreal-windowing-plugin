#include "WindowingM1Harness.h"

#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericWindow.h"
#include "Layout/WidgetPath.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "WpWindowingSubsystem.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, WindowingM1, "WindowingM1");

TWeakObjectPtr<UWindowingM1Widget> UWindowingM1Widget::LastBuilt;
FSimpleDelegate UWindowingM1Widget::DuringBuild;

namespace
{
    TSharedPtr<SWindow> FindWindow(const TCHAR* Title)
    {
        for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetTopLevelWindows())
        {
            if (Window->GetTitle().ToString() == Title)
            {
                return Window;
            }
        }
        return nullptr;
    }

#if PLATFORM_WINDOWS
    HWND NativeHandle(const TSharedPtr<SWindow>& Window)
    {
        const TSharedPtr<FGenericWindow> Native = Window ? Window->GetNativeWindow() : nullptr;
        return Native ? static_cast<HWND>(Native->GetOSWindowHandle()) : nullptr;
    }

    FString ClickWidget(UWindowingM1Widget& Widget, const TSharedRef<SWindow>& Window)
    {
        const TSharedPtr<SButton> Button = Widget.Button.Pin();
        if (!Button)
        {
            return TEXT("The Slate button is missing.");
        }

        FSlateApplication& Slate = FSlateApplication::Get();
        FWidgetPath Path;
        if (!Slate.GeneratePathToWidgetUnchecked(Button.ToSharedRef(), Path) ||
            Path.GetWindow() != Window)
        {
            return TEXT("Slate could not locate the button in the window.");
        }
        const FVector2D Center = Path.Widgets.Last().Geometry.GetAbsolutePositionAtCoordinates(
            FVector2D(0.5, 0.5));
        const FWidgetPath Hit = Slate.LocateWindowUnderMouse(Center, { Window }, false, 0);
        if (!Hit.ContainsWidget(Button.Get()))
        {
            return TEXT("Slate hit testing did not reach the button in window B.");
        }

        const FPointerEvent Down(FSlateApplication::CursorPointerIndex, Center, Center,
            { EKeys::LeftMouseButton }, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
        const FPointerEvent Up(FSlateApplication::CursorPointerIndex, Center, Center,
            {}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
        const bool bDownHandled = Slate.RoutePointerDownEvent(Hit, Down).IsEventHandled();
        Slate.RoutePointerUpEvent(Hit, Up);
        return bDownHandled ? FString() : TEXT("Slate did not route the click.");
    }
#endif

    void DestroyTestSession(UGameInstance& Instance, UWorld* World)
    {
        Instance.Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }
}

TSharedRef<SWidget> UWindowingM1Widget::RebuildWidget()
{
    TSharedPtr<SButton> NewButton;
    TSharedPtr<SEditableTextBox> NewInput;
    TSharedRef<SVerticalBox> Root = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(12)
        [
            SAssignNew(NewButton, SButton)
            .Text(FText::FromString(TEXT("Increment")))
            .ClickMethod(EButtonClickMethod::MouseDown)
            .OnClicked(FOnClicked::CreateWeakLambda(this, [this]()
            {
                ++Clicks;
                return FReply::Handled();
            }))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(12)
        [
            SAssignNew(NewInput, SEditableTextBox)
            .HintText(FText::FromString(TEXT("Native input")))
        ];
    Button = NewButton;
    Input = NewInput;
    LastBuilt = this;
    DuringBuild.ExecuteIfBound();
    return Root;
}

void UWindowingM1GameInstance::Init()
{
    Super::Init();
    if (FParse::Value(FCommandLine::Get(), TEXT("M1Report="), ReportPath))
    {
        FParse::Value(FCommandLine::Get(), TEXT("M1Scenario="), Scenario);
        TickHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateUObject(this, &ThisClass::TickTests));
    }
}

void UWindowingM1GameInstance::Shutdown()
{
    FTSTicker::RemoveTicker(TickHandle);
    Super::Shutdown();
}

bool UWindowingM1GameInstance::Expect(bool Condition, const FString& Message)
{
    if (!Condition)
    {
        Failures.Add(Message);
        UE_LOG(LogTemp, Error, TEXT("M1: %s"), *Message);
    }
    return Condition;
}

FWpWindowHandle UWindowingM1GameInstance::Open(
    UWpWindowingSubsystem* Subsystem, const TCHAR* Title)
{
    FWpWindowCreateOptions Options;
    Options.Title = FText::FromString(Title);
    FWpWindowHandle Handle;
    const FWpWindowResult Result = Subsystem->OpenWidgetWindow(
        UWindowingM1Widget::StaticClass(), Options, nullptr, Handle);
    Expect(Result.IsSuccess() && Handle.IsValid(),
        FString::Printf(TEXT("Open %s failed (%d): %s"), Title,
            static_cast<int32>(Result.Code), *Result.Message));
    return Handle;
}

void UWindowingM1GameInstance::Close(FWpWindowHandle Handle)
{
    Expect(GetSubsystem<UWpWindowingSubsystem>()->RequestCloseWindow(Handle).IsSuccess(),
        FString::Printf(TEXT("Close failed for %s"), *Handle.Id.ToString()));
}

void UWindowingM1GameInstance::OnOpened(FWpWindowHandle Handle)
{
    Opened.Add(Handle);
    if (CloseFromOpened.IsValid())
    {
        const FWpWindowHandle Target = CloseFromOpened;
        CloseFromOpened = {};
        Close(Target);
    }
}

void UWindowingM1GameInstance::OnClosed(FWpWindowClosedEvent Event)
{
    Closed.Add(Event);
    if (CloseFromClosed.IsValid())
    {
        const FWpWindowHandle Target = CloseFromClosed;
        CloseFromClosed = {};
        Close(Target);
    }
}

bool UWindowingM1GameInstance::TickTests(float)
{
    if (Phase == EPhase::Start)
    {
        Deadline = FPlatformTime::Seconds() + 60.0;
        if (Scenario == TEXT("registration-rollback"))
        {
            CheckRegistrationRollback();
        }
        else
        {
            OpenInitialWindows();
        }
    }
    else if (FPlatformTime::Seconds() > Deadline)
    {
        Expect(false, FString::Printf(TEXT("Timed out in phase %d, cycle %d."),
            static_cast<int32>(Phase), Cycles));
    }
    else
    {
        switch (Phase)
        {
        case EPhase::Input:
        case EPhase::InputAfterClose: CheckInputAndRequestCloses(); break;
        case EPhase::InitialCloses: CheckInitialCloses(); break;
        case EPhase::Isolation: CheckSessionIsolation(); break;
        case EPhase::Rollback: CheckRollback(); break;
        case EPhase::RegistrationRollback:
            CollectGarbage(RF_NoFlags);
            Expect(!PreviousWidget.IsValid(), TEXT("The registration rollback widget should be collectible."));
            Finish();
            return false;
        case EPhase::Stress: RunStressCycle(); break;
        default: break;
        }
    }

    if (!Failures.IsEmpty() || Scenario == TEXT("shutdown") ||
        (Cycles == 100 && !PreviousCycle.IsValid()))
    {
        Finish();
        return false;
    }
    return true;
}

void UWindowingM1GameInstance::OpenInitialWindows()
{
    UWpWindowingSubsystem* Subsystem = GetSubsystem<UWpWindowingSubsystem>();
    if (!Expect(Subsystem && FSlateApplication::IsInitialized(), TEXT("Windowing or Slate unavailable.")) ||
        !Expect(Scenario == TEXT("lifecycle") || Scenario == TEXT("shutdown"), TEXT("Unknown scenario.")))
    {
        return;
    }

    Subsystem->OnWindowOpened.AddDynamic(this, &ThisClass::OnOpened);
    Subsystem->OnWindowClosed.AddDynamic(this, &ThisClass::OnClosed);
    A = Open(Subsystem, TEXT("M1 A"));
    B = Open(Subsystem, TEXT("M1 B"));
    WidgetB = UWindowingM1Widget::LastBuilt;
    C = Open(Subsystem, TEXT("M1 C"));
    WidgetC = UWindowingM1Widget::LastBuilt;
    Expect(A.Id != B.Id && A.Id != C.Id && B.Id != C.Id &&
        Subsystem->IsWindowOpen(A) && Subsystem->IsWindowOpen(B) && Subsystem->IsWindowOpen(C),
        TEXT("Three independent windows should be open."));
    Expect(Opened.Num() == 3, TEXT("Expected three opened events."));

    if (Scenario == TEXT("shutdown"))
    {
        Close(A);
        Expect(Subsystem->GetWindowState(A) == EWpWindowState::Closing &&
            Subsystem->IsWindowOpen(B) && Subsystem->IsWindowOpen(C),
            TEXT("Shutdown should start with one pending finalization and two open windows."));
        return;
    }

    Expect(Subsystem->SetWindowTitle(B, FText::FromString(TEXT("M1 B renamed"))).IsSuccess() &&
        FindWindow(TEXT("M1 A")) && FindWindow(TEXT("M1 B renamed")) && FindWindow(TEXT("M1 C")),
        TEXT("Rename should affect only window B."));

    CollectGarbage(RF_NoFlags);
    Expect(WidgetB.IsValid() && WidgetC.IsValid() && WidgetB != WidgetC,
        TEXT("Open windows should retain distinct widgets across GC."));
    Phase = EPhase::Input;
}

void UWindowingM1GameInstance::CheckInputAndRequestCloses()
{
#if PLATFORM_WINDOWS
    const bool bAfterClose = Phase == EPhase::InputAfterClose;
    const TSharedPtr<SWindow> NativeB = FindWindow(TEXT("M1 B renamed"));
    if (!Expect(WidgetB.IsValid() && NativeB, TEXT("Expected native window B.")))
    {
        return;
    }

    const TSharedPtr<SEditableTextBox> InputB = WidgetB->Input.Pin();
    if (!bInputSent)
    {
        bInputSent = true;
        const FString Error = ClickWidget(*WidgetB, NativeB.ToSharedRef());
        Expect(Error.IsEmpty(), Error);
        return;
    }
    if (!bTextSent && WidgetB->Clicks == (bAfterClose ? 2 : 1))
    {
        bTextSent = true;
        FSlateApplication::Get().SetKeyboardFocus(InputB, EFocusCause::SetDirectly);
        Expect(::PostMessageW(NativeHandle(NativeB), WM_CHAR, bAfterClose ? TEXT('y') : TEXT('x'), 1),
            TEXT("Could not send native keyboard input to B."));
        return;
    }
    const FString Entered = InputB ? InputB->GetText().ToString() : FString();
    if (Entered.Len() < (bAfterClose ? 2 : 1))
    {
        return;
    }
    if (bAfterClose)
    {
        Expect(Entered.Contains(TEXT("x")) && Entered.Contains(TEXT("y")) &&
            WidgetB->Clicks == 2, TEXT("Window B should remain interactive after A and C close."));
        UWpWindowingSubsystem* Subsystem = GetSubsystem<UWpWindowingSubsystem>();
        CloseFromOpened = B;
        A = Open(Subsystem, TEXT("M1 replacement"));
        Expect(Subsystem->GetWindowState(B) == EWpWindowState::Closing &&
            Subsystem->RequestCloseWindow(B).Code == EWpWindowResultCode::AlreadyClosing,
            TEXT("Opened listener should close B exactly once."));
        CollectGarbage(RF_NoFlags);
        Expect(WidgetB.IsValid(), TEXT("A closing record should retain its widget until drain."));
        Phase = EPhase::Isolation;
        return;
    }

    const TSharedPtr<SEditableTextBox> InputC = WidgetC.IsValid() ? WidgetC->Input.Pin() : nullptr;
    Expect(Entered == TEXT("x") && WidgetB->Clicks == 1 && InputC &&
        WidgetC->Clicks == 0 && InputC->GetText().IsEmpty(),
        TEXT("Native input should update only window B."));

    CloseFromClosed = A;
    const HWND HandleC = NativeHandle(FindWindow(TEXT("M1 C")));
    Expect(HandleC && ::PostMessageW(HandleC, WM_SYSCOMMAND, SC_CLOSE, 0),
        TEXT("Could not post the native title-bar close command to C."));
    Phase = EPhase::InitialCloses;
#else
    Expect(false, TEXT("The native acceptance harness requires Windows."));
#endif
}

void UWindowingM1GameInstance::CheckInitialCloses()
{
    if (Closed.Num() < 2)
    {
        return;
    }
    UWpWindowingSubsystem* Subsystem = GetSubsystem<UWpWindowingSubsystem>();
    Expect(Closed.Num() == 2 &&
        Closed[0].Handle.Id == C.Id && Closed[0].Reason == EWpWindowCloseReason::UserOrPlatform &&
        Closed[1].Handle.Id == A.Id && Closed[1].Reason == EWpWindowCloseReason::HostRequest &&
        Subsystem->IsWindowOpen(B), TEXT("Native close of C should cause one reentrant close of A."));
    Expect(Subsystem->GetWindowState(C) == EWpWindowState::Invalid &&
        Subsystem->RequestCloseWindow(C).Code == EWpWindowResultCode::NotFound &&
        Subsystem->SetWindowTitle(C, FText::GetEmpty()).Code == EWpWindowResultCode::NotFound &&
        !FindWindow(TEXT("M1 C")), TEXT("Retired handles and native windows should be gone."));

    bInputSent = false;
    bTextSent = false;
    Phase = EPhase::InputAfterClose;
}

void UWindowingM1GameInstance::CheckSessionIsolation()
{
    UWpWindowingSubsystem* Primary = GetSubsystem<UWpWindowingSubsystem>();
    if (Primary->GetWindowState(B) != EWpWindowState::Invalid)
    {
        return;
    }

    TStrongObjectPtr<UGameInstance> Other(NewObject<UGameInstance>(GEngine));
    Other->InitializeStandalone();
    UWorld* World = Other->GetWorld();
    UWpWindowingSubsystem* Secondary = Other->GetSubsystem<UWpWindowingSubsystem>();
    B = Open(Secondary, TEXT("M1 secondary"));
    Expect(Secondary->GetWindowState(A) == EWpWindowState::Invalid &&
        Secondary->RequestCloseWindow(A).Code == EWpWindowResultCode::NotFound &&
        Secondary->SetWindowTitle(A, FText::GetEmpty()).Code == EWpWindowResultCode::NotFound &&
        Primary->GetWindowState(B) == EWpWindowState::Invalid,
        TEXT("Handles should be isolated by GameInstance."));
    if (APlayerController* Player = GetFirstLocalPlayerController())
    {
        FWpWindowHandle Invalid;
        Expect(Secondary->OpenWidgetWindow(UWindowingM1Widget::StaticClass(), {}, Player, Invalid).Code
            == EWpWindowResultCode::InvalidOwningContext && !Invalid.IsValid(),
            TEXT("A foreign controller should be rejected."));
    }
    Secondary->OnWindowClosed.AddDynamic(this, &ThisClass::OnClosed);
    DestroyTestSession(*Other, World);
    Expect(Primary->IsWindowOpen(A) && !FindWindow(TEXT("M1 secondary")),
        TEXT("Ending the second session should close only its window."));
    Close(A);
    Phase = EPhase::Rollback;
}

void UWindowingM1GameInstance::CheckRollback()
{
    if (Closed.Num() < 4)
    {
        return;
    }

    TStrongObjectPtr<UGameInstance> Other(NewObject<UGameInstance>(GEngine));
    Other->InitializeStandalone();
    UWorld* World = Other->GetWorld();
    UWpWindowingSubsystem* Secondary = Other->GetSubsystem<UWpWindowingSubsystem>();
    Secondary->OnWindowOpened.AddDynamic(this, &ThisClass::OnOpened);
    Secondary->OnWindowClosed.AddDynamic(this, &ThisClass::OnClosed);

    FWpWindowHandle Handle = A;
    Expect(Secondary->OpenWidgetWindow(nullptr, {}, nullptr, Handle).Code ==
        EWpWindowResultCode::InvalidWidgetClass && !Handle.IsValid(), TEXT("Invalid widget class."));
    FWpWindowCreateOptions Options;
    Options.InitialClientSize.X = 0;
    Expect(Secondary->OpenWidgetWindow(UWindowingM1Widget::StaticClass(), Options, nullptr, Handle).Code
        == EWpWindowResultCode::InvalidSize && !Handle.IsValid(), TEXT("Invalid initial client size."));
    Options = {};
    Options.Title = FText::FromString(TEXT("M1 rollback"));
    UWindowingM1Widget::DuringBuild.BindLambda([&Other]() { Other->Shutdown(); });
    const FWpWindowResult Result = Secondary->OpenWidgetWindow(
        UWindowingM1Widget::StaticClass(), Options, nullptr, Handle);
    UWindowingM1Widget::DuringBuild.Unbind();
    Expect(Result.Code == EWpWindowResultCode::ShuttingDown && !Handle.IsValid() &&
        !FindWindow(TEXT("M1 rollback")) && Opened.Num() == 4 && Closed.Num() == 4,
        TEXT("Session loss during widget construction should roll back without events."));
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    Phase = EPhase::Stress;
}

void UWindowingM1GameInstance::CheckRegistrationRollback()
{
    UWpWindowingSubsystem* Subsystem = GetSubsystem<UWpWindowingSubsystem>();
    Subsystem->OnWindowOpened.AddDynamic(this, &ThisClass::OnOpened);
    Subsystem->OnWindowClosed.AddDynamic(this, &ThisClass::OnClosed);
    FSlateApplication& Slate = FSlateApplication::Get();
    int32 Destroyed = 0;
    const FDelegateHandle Watch = Slate.OnWindowBeingDestroyed().AddLambda(
        [&Destroyed](const SWindow& Window)
        {
            if (Window.GetTitle().ToString() == TEXT("M1 registration rollback"))
            {
                ++Destroyed;
            }
        });

    FWpWindowCreateOptions Options;
    Options.Title = FText::FromString(TEXT("M1 registration rollback"));
    FWpWindowHandle Handle;
    const FWpWindowResult Result = Subsystem->OpenWidgetWindow(
        UWindowingM1Widget::StaticClass(), Options, nullptr, Handle);
    Slate.OnWindowBeingDestroyed().Remove(Watch);
    Expect(Result.Code == EWpWindowResultCode::NativeCreationFailed && !Handle.IsValid() &&
        Destroyed == 1 && !FindWindow(TEXT("M1 registration rollback")) &&
        Opened.IsEmpty() && Closed.IsEmpty(),
        TEXT("Off-screen registration should destroy its partial Slate window without publishing a handle."));
    PreviousWidget = UWindowingM1Widget::LastBuilt;
    Expect(PreviousWidget.IsValid(), TEXT("The registration attempt should have created a widget."));
    Phase = EPhase::RegistrationRollback;
}

void UWindowingM1GameInstance::RunStressCycle()
{
    UWpWindowingSubsystem* Subsystem = GetSubsystem<UWpWindowingSubsystem>();
    if (PreviousCycle.IsValid())
    {
        if (Subsystem->GetWindowState(PreviousCycle) != EWpWindowState::Invalid)
        {
            return;
        }
        Expect(Opened.Num() == 4 + Cycles && Closed.Num() == 4 + Cycles &&
            Closed.Last().Handle.Id == PreviousCycle.Id &&
            Closed.Last().Reason == EWpWindowCloseReason::HostRequest &&
            !FindWindow(TEXT("M1 stress")), TEXT("Stress cycle should retire exactly one window."));
        PreviousCycle = {};
        if (Cycles % 10 == 0)
        {
            CollectGarbage(RF_NoFlags);
            Expect(!PreviousWidget.IsValid(), TEXT("Retired stress widget should be collectible."));
        }
    }

    if (Cycles < 100 && Failures.IsEmpty())
    {
        PreviousCycle = Open(Subsystem, TEXT("M1 stress"));
        PreviousWidget = UWindowingM1Widget::LastBuilt;
        Close(PreviousCycle);
        ++Cycles;
    }
}

void UWindowingM1GameInstance::Finish()
{
    TArray<FString> Report;
    Report.Add(Failures.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"));
    Report.Add(FString::Printf(TEXT("scenario=%s"), *Scenario));
    Report.Add(FString::Printf(TEXT("cycles=%d opened=%d closed=%d"),
        Cycles, Opened.Num(), Closed.Num()));
    Report.Append(Failures);
    const bool Saved = FFileHelper::SaveStringArrayToFile(Report, *ReportPath);
    FPlatformMisc::RequestExitWithStatus(false, Saved && Failures.IsEmpty() ? 0 : 1);
}
