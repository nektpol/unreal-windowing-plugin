using UnrealBuildTool;

public class WindowingM1Target : TargetRules
{
    public WindowingM1Target(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("WindowingM1");
    }
}
