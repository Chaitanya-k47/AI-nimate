// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AInimate : ModuleRules
{
	public AInimate(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		// --- Standard Runtime Modules ---
		// These are always available.
		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", 
			"CoreUObject", 
			"Engine",
			"LevelSequence",
			"ControlRig"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"InputCore",
			"VaRest", 
			"Json", 
			"JsonUtilities",
			//"UnrealEd", //direct dependency
			//"Sequencer", //indirect dependency
			"MovieScene",
			//"MovieSceneTools", //direct dependency
			"MovieSceneTracks",
		});
		
		if(Target.bBuildEditor)
		{
			PublicDependencyModuleNames.AddRange(new string[] { 
				"Core", 
				"CoreUObject", 
				"Engine",
				"LevelSequence",
				"ControlRig"
			});

			PrivateDependencyModuleNames.AddRange(new string[] {
				"InputCore",
				"VaRest", 
				"Json", 
				"JsonUtilities",
				"UnrealEd",
				"Sequencer",
				"MovieScene",
				"MovieSceneTools",
				"MovieSceneTracks",
			});
		}
	}
}
