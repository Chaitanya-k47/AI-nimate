// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSequence.h"
#include "ControlRig.h"
#include "Animation/SkeletalMeshActor.h"
#include "AInimateBPLibrary.generated.h"

class UAnimSequence;
class USkeletalMesh;

UCLASS()
class AINIMATEPLUGIN_API UAInimateBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	/**
	 *Parses a JSON string, drives a control rig to keyframe a skeletal mesh actor in a level sequence, and bakes it to a new
	 animation sequence.
	 * @param JsonString the raw JSON data from the Python backend.
	 * @param TargetMesh the Skeletal Mesh to apply the animation to. Must have a compatible skeleton with the JSON data.
	 * @param OutputFolder (optional) the folder path within the Unreal project to save the
	 * @param OutErrorReason a string describing why the function failed.
	 * @return True if the animation was baked successfully, false otherwise.
	*/

	UFUNCTION(BlueprintCallable, Category="AInimate Tools",  meta=(DisplayName = "Generate Animation From JSON (Direct)", AdvancedDisplay = "OutputFolder"))
	static UAnimSequence* GenerateAnimationFromJSON_Direct(
		const FString& JsonString,
		USkeletalMesh* TargetMesh,
		const FString& OutputFolder,
		FString& OutErrorReason
	);

};
