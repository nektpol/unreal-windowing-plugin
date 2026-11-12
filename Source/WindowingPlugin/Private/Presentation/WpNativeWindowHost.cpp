// Developed by Neko Creative Technologies

#include "Presentation/WpNativeWindowHost.h"

#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBorder.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

void FWpNativeWindowHost::Construct(const FWpWindowCreateOptions& Options,
    const TSharedRef<SWidget>& Content, const FOnWindowClosed& PreClose)
{
    Window = SNew(SWindow)
        .Type(EWindowType::Normal)
        .Title(Options.Title)
        .ClientSize(Options.InitialClientSize)
        .MinWidth(TOptional<float>(static_cast<float>(Options.MinimumClientSize.X)))
        .MinHeight(TOptional<float>(static_cast<float>(Options.MinimumClientSize.Y)))
        .SizingRule(ESizingRule::UserSized)
        .UseOSWindowBorder(true)
        .HasCloseButton(true)
        .SupportsMinimize(true)
        .SupportsMaximize(true);

    // Clear exposed client pixels even when collapsed content stops painting.
    static const FSlateColorBrush ClientBackground(FLinearColor::Black);
    SAssignNew(ContentBackground, SBorder)
        .BorderImage(&ClientBackground)
        .Padding(0.f)
        [Content];
    Window->SetContent(ContentBackground.ToSharedRef());
    Window->SetOnWindowClosed(PreClose);
}

bool FWpNativeWindowHost::RegisterWindow()
{
    FSlateApplication::Get().AddWindow(Window.ToSharedRef(), false);
    return IsTracked() && HasNativeHandle();
}

bool FWpNativeWindowHost::ShowWindow()
{
    Window->ShowWindow();
    return IsTracked() && HasNativeHandle() && Window->IsVisible();
}

void FWpNativeWindowHost::SetTitle(const FText& Title)
{
    Window->SetTitle(Title);
}

void FWpNativeWindowHost::RequestDestroy()
{
    Window->RequestDestroyWindow();
}

bool FWpNativeWindowHost::IsTracked() const
{
    return Window.IsValid() && FSlateApplication::IsInitialized() &&
        FSlateApplication::Get().GetTopLevelWindows().Contains(Window.ToSharedRef());
}

bool FWpNativeWindowHost::HasNativeHandle() const
{
    return GetNativeHandle() != nullptr;
}

void* FWpNativeWindowHost::GetNativeHandle() const
{
    const TSharedPtr<FGenericWindow> Native = Window ? Window->GetNativeWindow() : nullptr;
    return Native ? Native->GetOSWindowHandle() : nullptr;
}

void FWpNativeWindowHost::MaximizeWindow()
{
    if (IsTracked() && HasNativeHandle())
    {
        Window->Maximize();
    }
}

bool FWpNativeWindowHost::IsMaximized() const
{
    return HasNativeHandle() && Window->IsWindowMaximized();
}

void FWpNativeWindowHost::ReleaseResources()
{
    if (Window.IsValid())
    {
        check(!IsTracked() && !HasNativeHandle());
        Window->SetOnWindowClosed(FOnWindowClosed());
        Window->SetContent(SNullWidget::NullWidget);
        Window.Reset();
        ContentBackground.Reset();
    }
}

void FWpNativeWindowHost::SetContent(const TSharedRef<SWidget>& Content)
{
    if (ContentBackground)
    {
        ContentBackground->SetContent(Content);
    }
}

void* FWpNativeWindowHost::GetForegroundHandle()
{
#if PLATFORM_WINDOWS
    return ::GetForegroundWindow();
#else
    return nullptr;
#endif
}