// Developed by Neko Creative Technologies

#include "Placement/WpPlacement.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
    constexpr int32 CoordinateLimit = 1000000;
    constexpr int32 SchemaVersion = 1;

    bool ValidRect(const FIntRect& Rect)
    {
        return Rect.Min.X >= -CoordinateLimit && Rect.Min.Y >= -CoordinateLimit &&
            Rect.Max.X <= CoordinateLimit && Rect.Max.Y <= CoordinateLimit &&
            Rect.Min.X < Rect.Max.X && Rect.Min.Y < Rect.Max.Y;
    }

    bool ValidDpi(int32 Dpi)
    {
        return Dpi >= 48 && Dpi <= 768;
    }

    bool ValidSnapshot(const FWpPlacementSnapshot& Saved)
    {
        return ValidRect(Saved.Normal) && ValidRect(Saved.WorkArea) && ValidDpi(Saved.Dpi);
    }

    int64 Overlap(const FIntRect& A, const FIntRect& B)
    {
        const int32 Width = FMath::Max(0, FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X));
        const int32 Height = FMath::Max(0, FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y));
        return static_cast<int64>(Width) * Height;
    }

    int32 RestoreAxis(int32 OldStart, int32 OldOrigin, int32 OldWork, int32 OldSize,
        int32 NewOrigin, int32 NewWork, int32 NewSize, bool bCenter)
    {
        const int32 OldRange = FMath::Max(0, OldWork - OldSize);
        const int32 NewRange = FMath::Max(0, NewWork - NewSize);
        const double Fraction = bCenter || OldRange == 0
            ? 0.5
            : FMath::Clamp(static_cast<double>(OldStart - OldOrigin) / OldRange, 0.0, 1.0);
    return NewOrigin + FMath::RoundToInt32(Fraction * NewRange);
    }
}

bool WpPlacement::IsValidIdentity(const FWpPlacementIdentity& Identity)
{
    const auto ValidPart = [](const FString& Part)
    {
        return Part.Len() <= 128 && !Part.TrimStartAndEnd().IsEmpty();
    };
    return ValidPart(Identity.Profile) && ValidPart(Identity.Key);
}

int32 WpPlacement::SelectMonitor(const FWpPlacementSnapshot& Saved,
    TConstArrayView<FWpPlacementMonitor> Monitors)
{
    int32 First = INDEX_NONE;
    int32 Primary = INDEX_NONE;
    int32 MatchingId = INDEX_NONE;
    int32 MatchCount = 0;
    int32 BestOverlap = INDEX_NONE;
    int64 BestArea = 0;

    for (int32 Index = 0; Index < Monitors.Num(); ++Index)
    {
        const FWpPlacementMonitor& Monitor = Monitors[Index];
        if (!ValidRect(Monitor.WorkArea) || !ValidDpi(Monitor.Dpi))
        {
            continue;
        }
        if (First == INDEX_NONE)
        {
            First = Index;
        }
        if (Monitor.bPrimary)
        {
            Primary = Index;
        }
        if (!Saved.MonitorId.IsEmpty() && Saved.MonitorId == Monitor.Id)
        {
            MatchingId = Index;
            ++MatchCount;
        }
        const int64 Area = Overlap(Saved.Normal, Monitor.WorkArea);
        if (Area > BestArea)
        {
            BestOverlap = Index;
            BestArea = Area;
        }
    }

    if (MatchCount == 1)
    {
        return MatchingId;
    }
    if (BestOverlap != INDEX_NONE)
    {
        return BestOverlap;
    }
    return Primary != INDEX_NONE ? Primary : First;
}

TOptional<FIntRect> WpPlacement::ResolveBounds(const FWpPlacementSnapshot& Saved,
    const FWpPlacementMonitor& Monitor, FIntPoint MinimumOuterSize)
{
    if (!ValidSnapshot(Saved) || !ValidRect(Monitor.WorkArea) || !ValidDpi(Monitor.Dpi) ||
        MinimumOuterSize.X <= 0 || MinimumOuterSize.Y <= 0 ||
        MinimumOuterSize.X > CoordinateLimit || MinimumOuterSize.Y > CoordinateLimit)
    {
        return {};
    }

    const double Scale = static_cast<double>(Monitor.Dpi) / Saved.Dpi;
    const FIntRect& Work = Monitor.WorkArea;
    const auto RestoreSize = [Scale](int32 Old, int32 Minimum, int32 Available)
    {
        return FMath::Clamp(FMath::RoundToInt32(Old * Scale), Minimum, FMath::Max(Minimum, Available));
    };
    const int32 Width = RestoreSize(Saved.Normal.Width(), MinimumOuterSize.X, Work.Width());
    const int32 Height = RestoreSize(Saved.Normal.Height(), MinimumOuterSize.Y, Work.Height());
    const bool bCenter = Saved.MonitorId.IsEmpty()
        ? Overlap(Saved.WorkArea, Work) == 0
        : Saved.MonitorId != Monitor.Id;
    const int32 Left = RestoreAxis(Saved.Normal.Min.X, Saved.WorkArea.Min.X, Saved.WorkArea.Width(),
        Saved.Normal.Width(), Work.Min.X, Work.Width(), Width, bCenter);
    const int32 Top = RestoreAxis(Saved.Normal.Min.Y, Saved.WorkArea.Min.Y, Saved.WorkArea.Height(),
        Saved.Normal.Height(), Work.Min.Y, Work.Height(), Height, bCenter);
    return FIntRect(Left, Top, Left + Width, Top + Height);
}

bool WpPlacement::HasReachableTitleBar(const FIntRect& Bounds,
    TConstArrayView<FWpPlacementMonitor> Monitors)
{
    if (!ValidRect(Bounds))
    {
        return false;
    }
    for (const FWpPlacementMonitor& Monitor : Monitors)
    {
        const FIntRect& Work = Monitor.WorkArea;
        const int32 VisibleWidth = FMath::Min(Work.Max.X, Bounds.Max.X) -
            FMath::Max(Work.Min.X, Bounds.Min.X);
        if (ValidRect(Work) && Bounds.Min.Y >= Work.Min.Y &&
            Bounds.Min.Y + 32 <= Work.Max.Y && VisibleWidth >= FMath::Min(120, Bounds.Width()))
        {
            return true;
        }
    }
    return false;
}

TArray<FWpPlacementMonitor> WpPlacement::ReadMonitors()
{
    FDisplayMetrics Metrics;
    FDisplayMetrics::RebuildDisplayMetrics(Metrics);
    TArray<FWpPlacementMonitor> Result;
    for (const FMonitorInfo& Source : Metrics.MonitorInfo)
    {
        const auto& Area = Source.WorkArea;
        const FIntRect Work(Area.Left, Area.Top, Area.Right, Area.Bottom);
        if (ValidRect(Work))
        {
            const float Scale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(
                static_cast<float>(Work.Min.X + Work.Width() / 2),
                static_cast<float>(Work.Min.Y + Work.Height() / 2));
            const int32 Dpi = FMath::IsFinite(Scale) && Scale >= 0.5f && Scale <= 8.0f
                ? FMath::RoundToInt(96.0f * Scale) : 96;
            Result.Add({Work, Source.ID, Dpi, Source.bIsPrimary});
        }
    }
    return Result;
}

bool WpPlacement::IsNormal(void* NativeHandle)
{
#if PLATFORM_WINDOWS
    HWND Window = static_cast<HWND>(NativeHandle);
    return Window && ::IsWindow(Window) && !::IsIconic(Window) && !::IsZoomed(Window);
#else
    return false;
#endif
}

bool WpPlacement::Capture(void* NativeHandle, FWpPlacementSnapshot& InOut)
{
#if PLATFORM_WINDOWS
    HWND Window = static_cast<HWND>(NativeHandle);
    if (!Window)
    {
        return false;
    }
    if (::IsIconic(Window) || ::IsZoomed(Window))
    {
        if (!ValidSnapshot(InOut))
        {
            return false;
        }
        WINDOWPLACEMENT State{};
        State.length = sizeof(State);
        InOut.bMaximized = ::IsZoomed(Window) ||
            (::GetWindowPlacement(Window, &State) && (State.flags & WPF_RESTORETOMAXIMIZED));
        return true;
    }

    RECT Native{};
    MONITORINFO Info{};
    Info.cbSize = sizeof(Info);
    const HMONITOR Monitor = ::MonitorFromWindow(Window, MONITOR_DEFAULTTONEAREST);
    if (!::GetWindowRect(Window, &Native) || !Monitor || !::GetMonitorInfoW(Monitor, &Info))
    {
        return false;
    }

    FWpPlacementSnapshot Candidate;
    Candidate.Normal = FIntRect(Native.left, Native.top, Native.right, Native.bottom);
    Candidate.WorkArea = FIntRect(Info.rcWork.left, Info.rcWork.top, Info.rcWork.right, Info.rcWork.bottom);
    Candidate.Dpi = static_cast<int32>(::GetDpiForWindow(Window));
    if (!ValidSnapshot(Candidate))
    {
        return false;
    }
    for (const FWpPlacementMonitor& Display : ReadMonitors())
    {
        if (Display.WorkArea == Candidate.WorkArea)
        {
            Candidate.MonitorId = Display.Id;
            break;
        }
    }
    InOut = MoveTemp(Candidate);
    return true;
#else
    return false;
#endif
}

bool WpPlacement::Restore(void* NativeHandle, const FWpPlacementSnapshot& Saved,
    FVector2D MinimumClientSize)
{
#if PLATFORM_WINDOWS
    HWND Window = static_cast<HWND>(NativeHandle);
    if (!Window || !ValidSnapshot(Saved))
    {
        return false;
    }
    const TArray<FWpPlacementMonitor> Monitors = ReadMonitors();
    const int32 Index = SelectMonitor(Saved, Monitors);
    if (!Monitors.IsValidIndex(Index))
    {
        return false;
    }

    const FWpPlacementMonitor& Monitor = Monitors[Index];
    const double Scale = Monitor.Dpi / 96.0;
    if (!FMath::IsFinite(MinimumClientSize.X) || !FMath::IsFinite(MinimumClientSize.Y) ||
        MinimumClientSize.X < 1 || MinimumClientSize.Y < 1 ||
        MinimumClientSize.X * Scale > CoordinateLimit || MinimumClientSize.Y * Scale > CoordinateLimit)
    {
        return false;
    }

    RECT Minimum{0, 0, FMath::CeilToInt32(MinimumClientSize.X * Scale),
        FMath::CeilToInt32(MinimumClientSize.Y * Scale)};
    if (!::AdjustWindowRectExForDpi(&Minimum, static_cast<DWORD>(::GetWindowLongPtrW(Window, GWL_STYLE)),
        0, static_cast<DWORD>(::GetWindowLongPtrW(Window, GWL_EXSTYLE)), Monitor.Dpi))
    {
        return false;
    }

    const TOptional<FIntRect> Bounds = ResolveBounds(Saved, Monitor,
        FIntPoint(Minimum.right - Minimum.left, Minimum.bottom - Minimum.top));
    return Bounds.IsSet() && ::SetWindowPos(Window, nullptr, Bounds->Min.X, Bounds->Min.Y,
        Bounds->Width(), Bounds->Height(), SWP_NOACTIVATE | SWP_NOZORDER) != 0;
#else
    return false;
#endif
}

namespace
{
    bool ReadRect(const FJsonObject& Object, const TCHAR* Field, FIntRect& Out)
    {
        const TSharedPtr<FJsonObject>* Value = nullptr;
        double X = 0, Y = 0, W = 0, H = 0;
        const auto IsInt = [](double Number)
        {
            return FMath::IsFinite(Number) && FMath::Abs(Number) <= 2 * CoordinateLimit &&
                Number == FMath::TruncToDouble(Number);
        };
        if (!Object.TryGetObjectField(Field, Value) || !Value || !Value->IsValid() ||
            !(*Value)->TryGetNumberField(TEXT("left"), X) ||
            !(*Value)->TryGetNumberField(TEXT("top"), Y) ||
            !(*Value)->TryGetNumberField(TEXT("width"), W) ||
            !(*Value)->TryGetNumberField(TEXT("height"), H) ||
            !IsInt(X) || !IsInt(Y) || !IsInt(W) || !IsInt(H))
        {
            return false;
        }
        Out = FIntRect(static_cast<int32>(X), static_cast<int32>(Y),
            static_cast<int32>(X + W), static_cast<int32>(Y + H));
        return ValidRect(Out);
    }

    TSharedRef<FJsonObject> WriteRect(const FIntRect& Rect)
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("left"), Rect.Min.X);
        Object->SetNumberField(TEXT("top"), Rect.Min.Y);
        Object->SetNumberField(TEXT("width"), Rect.Width());
        Object->SetNumberField(TEXT("height"), Rect.Height());
        return Object;
    }
}

FString WpPlacement::DefaultPath()
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WindowingPlugin/Placement.json")));
}

WpPlacement::ELoadResult WpPlacement::Load(const FString& Path, FWpPlacementMap& Out)
{
    Out.Reset();
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const int64 Size = Files.FileSize(*Path);
    if (Size < 0)
    {
        return Files.FileExists(*Path) ? ELoadResult::IoError : ELoadResult::Missing;
    }
    if (Size > 4 * 1024 * 1024)
    {
        return ELoadResult::Invalid;
    }

    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path))
    {
        return ELoadResult::IoError;
    }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
    {
        return ELoadResult::Invalid;
    }

    double Version = 0;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"), Version))
    {
        return ELoadResult::Invalid;
    }
    if (Version != SchemaVersion)
    {
        return ELoadResult::UnsupportedVersion;
    }
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Root->TryGetArrayField(TEXT("placements"), Rows) || !Rows)
    {
        return ELoadResult::Invalid;
    }

    for (const TSharedPtr<FJsonValue>& Row : *Rows)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!Row || !Row->TryGetObject(Object) || !Object || !Object->IsValid())
        {
            continue;
        }

        const FJsonObject& Value = *Object->Get();
        FWpPlacementIdentity Id;
        FWpPlacementSnapshot Saved;
        double Dpi = 0;
        if (!Value.TryGetStringField(TEXT("profile"), Id.Profile) ||
            !Value.TryGetStringField(TEXT("key"), Id.Key) || !IsValidIdentity(Id) ||
            !ReadRect(Value, TEXT("normal"), Saved.Normal) ||
            !ReadRect(Value, TEXT("workArea"), Saved.WorkArea) ||
            !Value.TryGetNumberField(TEXT("dpi"), Dpi) || !FMath::IsFinite(Dpi) ||
            Dpi < 48 || Dpi > 768 || Dpi != FMath::TruncToDouble(Dpi))
        {
            continue;
        }
        Saved.Dpi = static_cast<int32>(Dpi);
        Value.TryGetStringField(TEXT("monitorId"), Saved.MonitorId);
        Value.TryGetBoolField(TEXT("maximized"), Saved.bMaximized);
        Out.Add(MoveTemp(Id), MoveTemp(Saved));
    }
    return ELoadResult::Loaded;
}

bool WpPlacement::MergeAndSave(const FString& Path, const FWpPlacementMap& Dirty)
{
    if (Dirty.IsEmpty())
    {
        return true;
    }
#if PLATFORM_WINDOWS
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (!Files.CreateDirectoryTree(*FPaths::GetPath(Path)))
    {
        return false;
    }

    // A zero share mode serializes writers across game processes.
    const FString LockPath = Path + TEXT(".lock");
    HANDLE Lock = ::CreateFileW(*LockPath, GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Lock == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    ON_SCOPE_EXIT { ::CloseHandle(Lock); };

    FWpPlacementMap All;
    const ELoadResult State = Load(Path, All);
    if (State != ELoadResult::Loaded && State != ELoadResult::Missing)
    {
        return false;
    }
    for (const auto& Pair : Dirty)
    {
        if (IsValidIdentity(Pair.Key) && ValidSnapshot(Pair.Value))
        {
            All.Add(Pair.Key, Pair.Value);
        }
    }

    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const auto& Pair : All)
    {
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("profile"), Pair.Key.Profile);
        Row->SetStringField(TEXT("key"), Pair.Key.Key);
        Row->SetObjectField(TEXT("normal"), WriteRect(Pair.Value.Normal));
        Row->SetObjectField(TEXT("workArea"), WriteRect(Pair.Value.WorkArea));
        Row->SetStringField(TEXT("monitorId"), Pair.Value.MonitorId);
        Row->SetNumberField(TEXT("dpi"), Pair.Value.Dpi);
        Row->SetBoolField(TEXT("maximized"), Pair.Value.bMaximized);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
    Root->SetArrayField(TEXT("placements"), MoveTemp(Rows));
    FString Text;
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text)))
    {
        return false;
    }

    const FString Temp = Path + TEXT(".tmp");
    ON_SCOPE_EXIT { Files.DeleteFile(*Temp); };
    return FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
        ::MoveFileExW(*Temp, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return false;
#endif
}