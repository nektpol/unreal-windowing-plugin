using UnrealBuildTool;

public class WindowingM1 : ModuleRules
{
    public WindowingM1(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "UMG", "Slate", "SlateCore",
            "ApplicationCore", "InputCore", "WindowingPlugin"
        });
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.Add("user32.lib");
        }
    }
}
