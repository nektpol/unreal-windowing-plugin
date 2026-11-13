// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"

namespace WpWorkspaceLayout
{
    enum class ERegion : uint8 { Left, Right, Top, Bottom, Center, Count };

    struct FInput
    {
        FVector2D Viewport = FVector2D::ZeroVector;
        FVector2D CenterMinimum = FVector2D(320, 180);
        double Left = 0;
        double Right = 0;
        double Top = 0;
        double Bottom = 0;
    };

    struct FRegion
    {
        FVector2D Position = FVector2D::ZeroVector;
        FVector2D Size = FVector2D::ZeroVector;
    };

    struct FLayout
    {
        FRegion Regions[static_cast<int32>(ERegion::Count)];

        const FRegion& operator[](ERegion Region) const
        {
            return Regions[static_cast<int32>(Region)];
        }
    };

    inline double NonNegative(double Value)
    {
        return FMath::IsFinite(Value) ? FMath::Max(0.0, Value) : 0.0;
    }

    inline void FitOpposingEdges(double Axis, double Minimum, double& First, double& Second)
    {
        First = NonNegative(First);
        Second = NonNegative(Second);
        const double Budget = FMath::Max(0.0, Axis - NonNegative(Minimum));
        const double Total = First + Second;
        if (Total > Budget)
        {
            First = Budget * (First / Total);
            Second = Budget - First;
        }
    }

    inline FLayout Resolve(const FInput& Input)
    {
        const double Width = NonNegative(Input.Viewport.X);
        const double Height = NonNegative(Input.Viewport.Y);
        double Left = Input.Left;
        double Right = Input.Right;
        double Top = Input.Top;
        double Bottom = Input.Bottom;
        FitOpposingEdges(Width, Input.CenterMinimum.X, Left, Right);
        FitOpposingEdges(Height, Input.CenterMinimum.Y, Top, Bottom);

        const double MiddleWidth = FMath::Max(0.0, Width - Left - Right);
        const double MiddleHeight = FMath::Max(0.0, Height - Top - Bottom);
        FLayout Layout;
        Layout.Regions[static_cast<int32>(ERegion::Left)] = {{0, 0}, {Left, Height}};
        Layout.Regions[static_cast<int32>(ERegion::Right)] = {{Width - Right, 0}, {Right, Height}};
        Layout.Regions[static_cast<int32>(ERegion::Top)] = {{Left, 0}, {MiddleWidth, Top}};
        Layout.Regions[static_cast<int32>(ERegion::Bottom)] =
            {{Left, Height - Bottom}, {MiddleWidth, Bottom}};
        Layout.Regions[static_cast<int32>(ERegion::Center)] =
            {{Left, Top}, {MiddleWidth, MiddleHeight}};
        return Layout;
    }
}
