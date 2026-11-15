using UnrealBuildTool;

public class WindowingM1EditorTarget : TargetRules
{
    public WindowingM1EditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("WindowingM1");
    }
}
