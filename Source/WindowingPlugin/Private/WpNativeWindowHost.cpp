


#include "WpNativeWindowHost.h"

#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Widgets/SNullWidget.h"

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

    Window->SetContent(Content);
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
    const TSharedPtr<FGenericWindow> Native =
        Window.IsValid() ? Window->GetNativeWindow() : nullptr;
    return Native.IsValid() && Native->GetOSWindowHandle() != nullptr;
}

void FWpNativeWindowHost::ReleaseResources()
{
    if (Window.IsValid())
    {
        check(!IsTracked() && !HasNativeHandle());
        Window->SetOnWindowClosed(FOnWindowClosed());
        Window->SetContent(SNullWidget::NullWidget);
        Window.Reset();
    }
}