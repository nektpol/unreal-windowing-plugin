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
        bool bTopOwnsLeft = false;
        bool bTopOwnsRight = false;
        bool bBottomOwnsLeft = false;
        bool bBottomOwnsRight = false;
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

    inline TOptional<double> ResizePreference(double Budget, double Opposite, double Requested,
        double GripWidth, double OppositeReserve)
    {
        if (Budget <= 0.0)
        {
            return {};
        }

        const double Reserve = FMath::Min3(Opposite, OppositeReserve, Budget * 0.5);
        const double MaxExtent = Budget - Reserve;
        const double Extent = FMath::Clamp(Requested, FMath::Min(GripWidth, MaxExtent), MaxExtent);
        if (Reserve > 0.0 && Extent + Opposite > Budget)
        {
            return Opposite * (Extent / (Budget - Extent));
        }
        return Extent;
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

        const bool bTopLeft = Top > 0.0 && (Left == 0.0 || Input.bTopOwnsLeft);
        const bool bTopRight = Top > 0.0 && (Right == 0.0 || Input.bTopOwnsRight);
        const bool bBottomLeft = Bottom > 0.0 && (Left == 0.0 || Input.bBottomOwnsLeft);
        const bool bBottomRight = Bottom > 0.0 && (Right == 0.0 || Input.bBottomOwnsRight);

        const double LeftStart = bTopLeft ? Top : 0.0;
        const double LeftEnd = Height - (bBottomLeft ? Bottom : 0.0);
        const double RightStart = bTopRight ? Top : 0.0;
        const double RightEnd = Height - (bBottomRight ? Bottom : 0.0);
        const double TopStart = bTopLeft ? 0.0 : Left;
        const double TopEnd = Width - (bTopRight ? 0.0 : Right);
        const double BottomStart = bBottomLeft ? 0.0 : Left;
        const double BottomEnd = Width - (bBottomRight ? 0.0 : Right);

        FLayout Layout;
        Layout.Regions[static_cast<int32>(ERegion::Left)] =
            {{0, LeftStart}, {Left, FMath::Max(0.0, LeftEnd - LeftStart)}};
        Layout.Regions[static_cast<int32>(ERegion::Right)] =
            {{Width - Right, RightStart}, {Right, FMath::Max(0.0, RightEnd - RightStart)}};
        Layout.Regions[static_cast<int32>(ERegion::Top)] =
            {{TopStart, 0}, {FMath::Max(0.0, TopEnd - TopStart), Top}};
        Layout.Regions[static_cast<int32>(ERegion::Bottom)] =
            {{BottomStart, Height - Bottom}, {FMath::Max(0.0, BottomEnd - BottomStart), Bottom}};
        Layout.Regions[static_cast<int32>(ERegion::Center)] =
            {{Left, Top}, {MiddleWidth, MiddleHeight}};
        return Layout;
    }
}