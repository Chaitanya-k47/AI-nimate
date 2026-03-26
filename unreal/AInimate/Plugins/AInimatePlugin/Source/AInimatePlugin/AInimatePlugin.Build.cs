// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AInimatePlugin : ModuleRules
{
	public AInimatePlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", 
			"CoreUObject", 
			"Engine",
			"LevelSequence",
			"ControlRig",
			
		});
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"ToolMenus",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",

				"VaRest", 
				"Json", 
				"JsonUtilities",
				//"UnrealEd", //direct dependency
				//"Sequencer", //indirect dependency
				"MovieScene",
				//"MovieSceneTools", //direct dependency
				"MovieSceneTracks",
				"AssetRegistry"
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

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
				"LevelSequenceEditor",
				"AssetTools",
				"SequencerScriptingEditor"

			});
		}

	}
}
