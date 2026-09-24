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

		// Fish anim (T-030f): UFishAnimInstance::ClassHasRolePin reads ABP_Fish's Blend Poses by enum node
		PrivateDependencyModuleNames.Add("AnimGraphRuntime");

		// Fish system (T-008): row structs expose gameplay tags; UFishSettings is a UDeveloperSettings
		PublicDependencyModuleNames.AddRange(new string[] { "GameplayTags", "DeveloperSettings" });

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

		// Lure character settings (UDeveloperSettings) and the game-mode config check in tests (UGameMapsSettings), T-004
		PrivateDependencyModuleNames.AddRange(new string[] { "DeveloperSettings", "EngineSettings" });

		// Editor builds only: tests that place actors the way the editor and the level builder do (actor factories, the
		// placement subsystem; T-026 water volume crash fix). Gameplay code never uses these: keep editor calls in tests
		// or behind WITH_EDITOR, so packaged game targets link without them.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "EditorFramework", "TypedElementFramework" });
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
