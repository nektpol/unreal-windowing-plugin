// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"

struct FWpPlacementIdentity
{
    FString Profile;
    FString Key;

    bool operator==(const FWpPlacementIdentity& Other) const
    {
        return Profile.Equals(Other.Profile, ESearchCase::CaseSensitive) &&
            Key.Equals(Other.Key, ESearchCase::CaseSensitive);
    }

    friend uint32 GetTypeHash(const FWpPlacementIdentity& Value)
    {
        return HashCombine(GetTypeHash(Value.Profile), GetTypeHash(Value.Key));
    }
};

struct FWpPlacementSnapshot
{
    FIntRect Normal = FIntRect(0, 0, 0, 0);
    FIntRect WorkArea = FIntRect(0, 0, 0, 0);
    FString MonitorId;
    int32 Dpi = 96;
    bool bMaximized = false;

    bool operator==(const FWpPlacementSnapshot& Other) const
    {
        return Normal == Other.Normal && WorkArea == Other.WorkArea &&
            MonitorId == Other.MonitorId && Dpi == Other.Dpi &&
            bMaximized == Other.bMaximized;
    }
};

struct FWpPlacementMonitor
{
    FIntRect WorkArea;
    FString Id;
    int32 Dpi;
    bool bPrimary;
};

using FWpPlacementMap = TMap<FWpPlacementIdentity, FWpPlacementSnapshot>;

namespace WpPlacement
{
    bool IsValidIdentity(const FWpPlacementIdentity& Identity);
    int32 SelectMonitor(const FWpPlacementSnapshot& Saved,
        TConstArrayView<FWpPlacementMonitor> Monitors);
    TOptional<FIntRect> ResolveBounds(const FWpPlacementSnapshot& Saved,
        const FWpPlacementMonitor& Monitor, FIntPoint MinimumOuterSize);
    TArray<FWpPlacementMonitor> ReadMonitors();
    bool IsNormal(void* NativeHandle);
    bool Capture(void* NativeHandle, FWpPlacementSnapshot& InOut);
    bool Restore(void* NativeHandle, const FWpPlacementSnapshot& Saved,
        FVector2D MinimumClientSize);
    bool HasReachableTitleBar(const FIntRect& Bounds,
        TConstArrayView<FWpPlacementMonitor> Monitors);

    enum class ELoadResult { Loaded, Missing, Invalid, UnsupportedVersion, IoError };
    ELoadResult Load(const FString& Path, FWpPlacementMap& Out);
    bool MergeAndSave(const FString& Path, const FWpPlacementMap& Dirty);
    FString DefaultPath();
}