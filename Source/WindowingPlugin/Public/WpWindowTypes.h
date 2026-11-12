// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "WpWindowTypes.generated.h"

USTRUCT(BlueprintType)
struct WINDOWINGPLUGIN_API FWpWindowHandle
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Windowing")
    FGuid Id;

    bool IsValid() const { return Id.IsValid(); }
};

UENUM(BlueprintType)
enum class EWpWindowState : uint8
{
    Invalid, Creating, Open, Closing, Closed, Failed
};

UENUM(BlueprintType)
enum class EWpWindowCloseReason : uint8
{
    Unknown, UserOrPlatform, HostRequest,
    GameInstanceShutdown, PluginShutdown, CreationRollback
};

UENUM(BlueprintType)
enum class EWpWindowResultCode : uint8
{
    Success, InvalidWidgetClass, InvalidOwningContext,
    InvalidSize, WidgetCreationFailed,
    SlateUnavailable, UnsupportedPlatform, WrongThread,
    ShuttingDown, NativeCreationFailed, NotFound,
    InvalidState, AlreadyClosing, InternalError,
    InvalidPlacementIdentity, DuplicatePlacementIdentity
};

USTRUCT(BlueprintType)
struct WINDOWINGPLUGIN_API FWpWindowResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Windowing")
    EWpWindowResultCode Code = EWpWindowResultCode::InternalError;

    UPROPERTY(BlueprintReadOnly, Category="Windowing")
    FString Message;

    bool IsSuccess() const { return Code == EWpWindowResultCode::Success; }
};

USTRUCT(BlueprintType)
struct WINDOWINGPLUGIN_API FWpWindowCreateOptions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windowing")
    FText Title;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windowing")
    FVector2D InitialClientSize = FVector2D(640.0, 480.0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windowing")
    FVector2D MinimumClientSize = FVector2D(320.0, 240.0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windowing")
    FString PlacementProfile = TEXT("Default");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windowing")
    FString PlacementKey;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windowing")
    bool bRestorePlacement = true;
};

USTRUCT(BlueprintType)
struct WINDOWINGPLUGIN_API FWpWindowClosedEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Windowing")
    FWpWindowHandle Handle;

    UPROPERTY(BlueprintReadOnly, Category="Windowing")
    EWpWindowCloseReason Reason = EWpWindowCloseReason::Unknown;
};
