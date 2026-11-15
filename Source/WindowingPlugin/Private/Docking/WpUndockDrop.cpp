// Developed by Neko Creative Technologies

#include "Docking/WpUndockDrop.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

void* WpUndockDrop::GetCaptureWindow()
{
#if PLATFORM_WINDOWS
    return ::GetCapture();
#else
    return nullptr;
#endif
}

void* WpUndockDrop::GetRootWindowAt(FVector2D Screen)
{
#if PLATFORM_WINDOWS
    if (!FMath::IsFinite(Screen.X) || !FMath::IsFinite(Screen.Y) ||
        FMath::Abs(Screen.X) > 1000000.0 || FMath::Abs(Screen.Y) > 1000000.0)
    {
        return nullptr;
    }
    const POINT Point{FMath::FloorToInt32(Screen.X), FMath::FloorToInt32(Screen.Y)};
    const HWND Hit = ::WindowFromPoint(Point);
    return Hit ? ::GetAncestor(Hit, GA_ROOT) : nullptr;
#else
    return nullptr;
#endif
}

bool WpUndockDrop::IsDesktopSurface(void* Window)
{
#if PLATFORM_WINDOWS
    const HWND Hit = static_cast<HWND>(Window);
    if (!Hit)
    {
        return false;
    }
    const HWND Shell = ::GetShellWindow();
    if (Hit == Shell || Hit == ::GetDesktopWindow())
    {
        return true;
    }
    if (!Shell)
    {
        return false;
    }

    DWORD HitProcess = 0;
    DWORD ShellProcess = 0;
    ::GetWindowThreadProcessId(Hit, &HitProcess);
    ::GetWindowThreadProcessId(Shell, &ShellProcess);
    WCHAR ClassName[64]{};
    return HitProcess != 0 && HitProcess == ShellProcess &&
        ::GetClassNameW(Hit, ClassName, UE_ARRAY_COUNT(ClassName)) > 0 &&
        (FCString::Strcmp(ClassName, TEXT("WorkerW")) == 0 ||
         FCString::Strcmp(ClassName, TEXT("Progman")) == 0);
#else
    return false;
#endif
}

TOptional<FWpPlacementSnapshot> WpUndockDrop::MakePlacement(
    FVector2D Screen, const FWpPlacementMonitor& Monitor,
    const FWpPlacementSnapshot* Previous, FVector2D InitialClientSize)
{
    const FIntRect& Work = Monitor.WorkArea;
    if (Monitor.Dpi <= 0 || Work.Width() <= 0 || Work.Height() <= 0 ||
        !FMath::IsFinite(Screen.X) || !FMath::IsFinite(Screen.Y) ||
        Screen.X < Work.Min.X || Screen.X >= Work.Max.X ||
        Screen.Y < Work.Min.Y || Screen.Y >= Work.Max.Y)
    {
        return {};
    }

    const double Scale = Monitor.Dpi / 96.0;
    FVector2D Size = InitialClientSize;
    if (Previous && Previous->Dpi > 0 && Previous->Normal.Width() > 0 && Previous->Normal.Height() > 0)
    {
        Size = FVector2D(Previous->Normal.Width(), Previous->Normal.Height()) * (96.0 / Previous->Dpi);
    }
    if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X <= 0.0 || Size.Y <= 0.0)
    {
        return {};
    }

    const int32 Width = FMath::RoundToInt32(FMath::Clamp(Size.X * Scale, 1.0, double(Work.Width())));
    const int32 Height = FMath::RoundToInt32(FMath::Clamp(Size.Y * Scale, 1.0, double(Work.Height())));
    const int32 Left = FMath::Clamp(FMath::RoundToInt32(Screen.X - 32.0 * Scale),
        Work.Min.X, Work.Max.X - Width);
    const int32 Top = FMath::Clamp(FMath::RoundToInt32(Screen.Y + 14.0 * Scale),
        Work.Min.Y, Work.Max.Y - Height);

    FWpPlacementSnapshot Placement;
    Placement.Normal = FIntRect(Left, Top, Left + Width, Top + Height);
    Placement.WorkArea = Work;
    Placement.MonitorId = Monitor.Id;
    Placement.Dpi = Monitor.Dpi;
    return Placement;
}