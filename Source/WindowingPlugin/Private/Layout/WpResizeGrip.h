// Developed by Neko Creative Technologies

#pragma once

#include "Brushes/SlateColorBrush.h"
#include "Widgets/Layout/SBorder.h"

namespace WpResizeGrip
{
    inline constexpr float HitSize = 6.f;
    inline constexpr float LineSize = 2.f;
    inline constexpr float LineInset = HitSize - LineSize;

    inline TSharedRef<SBorder> Make(bool bResizeHorizontally, FMargin Inset)
    {
        static const FSlateColorBrush LineBrush(FLinearColor(0.24f, 0.24f, 0.24f));
        const auto Grip = SNew(SBorder)
            .BorderImage(nullptr)
            .Padding(Inset)
            [
                SNew(SBorder)
                .BorderImage(&LineBrush)
                .Padding(0.f)
                .Visibility(EVisibility::HitTestInvisible)
            ];
        Grip->SetCursor(TOptional<EMouseCursor::Type>(bResizeHorizontally
            ? EMouseCursor::ResizeLeftRight : EMouseCursor::ResizeUpDown));
        return Grip;
    }
}
