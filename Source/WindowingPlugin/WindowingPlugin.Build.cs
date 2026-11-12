// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WindowingPlugin : ModuleRules
{
	public WindowingPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", "CoreUObject", "Engine", "UMG",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate", "SlateCore", "ApplicationCore","Json"
			}
			);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("user32.lib");
		}
	}
}
