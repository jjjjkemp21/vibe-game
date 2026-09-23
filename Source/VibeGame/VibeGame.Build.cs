// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VibeGame : ModuleRules
{
	public VibeGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"SlateCore",
			"Json"
		});

		PublicIncludePaths.AddRange(new string[] {
			"VibeGame",
			"VibeGame/Variant_Platforming",
			"VibeGame/Variant_Platforming/Animation",
			"VibeGame/Variant_Combat",
			"VibeGame/Variant_Combat/AI",
			"VibeGame/Variant_Combat/Animation",
			"VibeGame/Variant_Combat/Gameplay",
			"VibeGame/Variant_Combat/Interfaces",
			"VibeGame/Variant_Combat/UI",
			"VibeGame/Variant_SideScrolling",
			"VibeGame/Variant_SideScrolling/AI",
			"VibeGame/Variant_SideScrolling/Gameplay",
			"VibeGame/Variant_SideScrolling/Interfaces",
			"VibeGame/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
