// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class AInimateTarget : TargetRules
{
	public AInimateTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		//Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.AddRange( new string[] { "AInimate" } );
	}
}
