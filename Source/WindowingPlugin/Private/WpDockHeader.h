// Developed by Neko Creative Technologies

#pragma once

#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

struct FWpDockMenuAction
{
    FText Label;
    FSimpleDelegate Execute;
};

inline FReply WpRunDockAction(FSimpleDelegate Action)
{
    Action.ExecuteIfBound();
    return FReply::Handled();
}

inline TSharedRef<SWidget> WpBuildDockMenu(const TArray<FWpDockMenuAction>& Actions)
{
    const TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
    for (const FWpDockMenuAction& Action : Actions)
    {
        Menu->AddSlot().AutoHeight()
        [
            SNew(SButton).Text(Action.Label)
            .OnClicked_Lambda([Execute = Action.Execute]
            {
                FSlateApplication::Get().DismissAllMenus();
                return WpRunDockAction(Execute);
            })
        ];
    }
    return Menu;
}

struct FWpDockFrame
{
    TSharedPtr<SVerticalBox> Root;
    TSharedPtr<SBox> Body;
    TSharedPtr<SButton> Toggle;

    bool IsCollapsed() const
    {
        return Body->GetVisibility() == EVisibility::Collapsed;
    }

    void SetCollapsed(bool bCollapsed) const
    {
        if (bCollapsed && (Body->HasKeyboardFocus() || Body->HasFocusedDescendants()))
        {
            FSlateApplication::Get().SetKeyboardFocus(Toggle, EFocusCause::SetDirectly);
        }
        Body->SetVisibility(bCollapsed ? EVisibility::Collapsed : EVisibility::Visible);
    }
};

inline FSlateFontInfo WpDockSymbolFont()
{
#if PLATFORM_WINDOWS
    static const FString SymbolFont = FPaths::Combine(
        FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")), TEXT("Fonts/seguisym.ttf"));
    if (FPaths::FileExists(SymbolFont))
    {
        return FSlateFontInfo(SymbolFont, 14);
    }
#endif
    return FCoreStyle::GetDefaultFontStyle("Regular", 14);
}

inline FWpDockFrame WpBuildDockFrame(
    TAttribute<FText> Title, const TSharedRef<SWidget>& Content,
    FSimpleDelegate Close, FSimpleDelegate Toggle,
    TFunction<TArray<FWpDockMenuAction>()> GetActions)
{
    // Native Slate windows can load an existing back buffer. Paint an opaque
    // base before blending the title glyph edges into it.
    static const FSlateColorBrush HeaderBackground(FLinearColor::Black);
    const FSlateFontInfo SymbolFont = WpDockSymbolFont();
    const FComboButtonStyle& SettingsStyle =
        FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton");
    const FVector2D GearSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
        ->Measure(INVTEXT("\u2699"), SymbolFont);
    const TSharedRef<STextBlock> TitleText = SNew(STextBlock)
        .Text(Title)
        .SimpleTextMode(true)
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
        .ShadowOffset(FVector2D::ZeroVector)
        .ShadowColorAndOpacity(FLinearColor::Transparent);
    TitleText->SetPixelSnapping(EWidgetPixelSnapping::SnapToPixel);
    FWpDockFrame Frame;
    Frame.Body = SNew(SBox)[Content];
    const TWeakPtr<SBox> WeakBody = Frame.Body;
    SAssignNew(Frame.Root, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBorder)
            .BorderImage(&HeaderBackground)
            .Padding(0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[TitleText]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SComboButton)
                    .HasDownArrow(false)
                    .ToolTipText(INVTEXT("Settings"))
                    .OnGetMenuContent_Lambda([Actions = MoveTemp(GetActions)]
                    {
                        return WpBuildDockMenu(Actions());
                    })
                    .ButtonContent()
                    [
                        SNew(STextBlock).Font(SymbolFont).Text(INVTEXT("\u2699"))
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SAssignNew(Frame.Toggle, SButton)
                    .ButtonStyle(&SettingsStyle.ButtonStyle)
                    .ContentPadding(SettingsStyle.ContentPadding)
                    .ToolTipText_Lambda([WeakBody]
                    {
                        const TSharedPtr<SBox> Body = WeakBody.Pin();
                        return Body && Body->GetVisibility() == EVisibility::Collapsed
                            ? INVTEXT("Expand") : INVTEXT("Collapse");
                    })
                    .OnClicked_Lambda([Toggle] { return WpRunDockAction(Toggle); })
                    [
                        SNew(SBox).WidthOverride(GearSize.X).HAlign(HAlign_Center).VAlign(VAlign_Center)
                        [
                            SNew(STextBlock).Font(SymbolFont)
                            .Text_Lambda([WeakBody]
                            {
                                const TSharedPtr<SBox> Body = WeakBody.Pin();
                                return Body && Body->GetVisibility() == EVisibility::Collapsed
                                    ? INVTEXT("\U0001F5D6") : INVTEXT("\U0001F5D5");
                            })
                        ]
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .ButtonStyle(&SettingsStyle.ButtonStyle)
                    .ContentPadding(SettingsStyle.ContentPadding)
                    .ToolTipText(INVTEXT("Close"))
                    .OnClicked_Lambda([Close] { return WpRunDockAction(Close); })
                    [
                        SNew(SBox).WidthOverride(GearSize.X).HAlign(HAlign_Center).VAlign(VAlign_Center)
                        [
                            SNew(STextBlock).Font(SymbolFont).Text(INVTEXT("\U0001F5D9"))
                        ]
                    ]
                ]
            ]
        ]
        + SVerticalBox::Slot().FillHeight(1.f)[Frame.Body.ToSharedRef()];
    return Frame;
}