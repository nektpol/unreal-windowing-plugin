// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UWpWindowRecord;
class UWpWindowingSubsystem;
class FWpNativeWindowHost;

struct FWpWindowRuntimeEntry final
{
	FWpWindowRuntimeEntry() = default;
	~FWpWindowRuntimeEntry();

	FWpWindowRuntimeEntry(const FWpWindowRuntimeEntry&) = delete;
	FWpWindowRuntimeEntry& operator=(const FWpWindowRuntimeEntry&) = delete;

	TObjectPtr<UWpWindowRecord> Record = nullptr;
	TUniquePtr<FWpNativeWindowHost> Host;
	TWeakObjectPtr<UWpWindowingSubsystem> EventSink;
	bool bFailedCreation = false;
};