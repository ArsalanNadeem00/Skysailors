// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShipGame : ModuleRules
{
	public ShipGame(ReadOnlyTargetRules Target) : base(Target)
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

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"ShipGame",
			"ShipGame/Variant_Platforming",
			"ShipGame/Variant_Platforming/Animation",
			"ShipGame/Variant_Combat",
			"ShipGame/Variant_Combat/AI",
			"ShipGame/Variant_Combat/Animation",
			"ShipGame/Variant_Combat/Gameplay",
			"ShipGame/Variant_Combat/Interfaces",
			"ShipGame/Variant_Combat/UI",
			"ShipGame/Variant_SideScrolling",
			"ShipGame/Variant_SideScrolling/AI",
			"ShipGame/Variant_SideScrolling/Gameplay",
			"ShipGame/Variant_SideScrolling/Interfaces",
			"ShipGame/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
