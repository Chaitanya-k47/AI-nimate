// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSequence.h"
#include "ControlRig.h"
#include "Animation/SkeletalMeshActor.h"
#include "AInimateBPLibrary.generated.h"


UCLASS()
class AINIMATE_API UAInimateBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	/**
	 *Parses a JSON string, drives a control rig to keyframe a skeletal mesh actor in a level sequence, and bakes it to a new
	 animation sequence.
	 * @param JsonString the raw JSON data from the Python backend.
	 * @param TargetSequence the Level Sequence asset to use as a workbench.
	 * @param TargetActor the Skeletal Mesh Actor in the level to animate.
	 * @param ControlRigToUse the Control Rig asset that knows how to apply the transforms.
	 * @param OutErrorReason a string describing why the function failed.
	 * @return True if the animation was baked successfully, false otherwise.
	*/

	UFUNCTION(BlueprintCallable, Category="AInimate Tools")
	static bool GenerateAnimationFromJSON(
		const FString& JsonString,
		ULevelSequence* TargetSequence,
		ASkeletalMeshActor* TargetActor,
		UControlRig* ControlRigToUse,
		FString& OutErrorReason
	);

};
