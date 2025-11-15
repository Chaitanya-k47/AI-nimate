// Fill out your copyright notice in the Description page of Project Settings.


#include "AInimateBPLibrary.h"

#if WITH_EDITOR
#include "IMovieSceneTools.h"
// other editor-only includes
#endif

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "MovieScene.h"
#include "MovieSceneFolder.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GameFramework/Actor.h"

//a static helper function to parse json object into FTransform
static FTransform ParseTransformFromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
    FTransform ResultTransform = FTransform::Identity;

    //parse location
    const TArray<TSharedPtr<FJsonValue>>* LocationArray;
    if(JsonObject->TryGetArrayField(TEXT("location"), LocationArray) && LocationArray->Num() == 3)
    {
        FVector Location(
            static_cast<float>((*LocationArray)[0]->AsNumber()), //X
            static_cast<float>((*LocationArray)[1]->AsNumber()), //Y
            static_cast<float>((*LocationArray)[2]->AsNumber()) //Z
        );
        ResultTransform.SetLocation(Location);
    }

    //parse rotation
    const TArray<TSharedPtr<FJsonValue>>* RotationArray;
    if(JsonObject->TryGetArrayField(TEXT("rotation"), RotationArray) && RotationArray->Num() == 4)
    {
        FQuat Rotation(
            static_cast<float>((*RotationArray)[0]->AsNumber()), //X
            static_cast<float>((*RotationArray)[1]->AsNumber()), //Y
            static_cast<float>((*RotationArray)[2]->AsNumber()), //Z
            static_cast<float>((*RotationArray)[3]->AsNumber()) //W
        );
        ResultTransform.SetRotation(Rotation);

    }

    return ResultTransform;
}

//a helper function to reset a UMovieScene instance.
void ResetMovieScene(UMovieScene* MovieScene)
{
    if(!MovieScene)
    {
        UE_LOG(LogTemp, Warning, TEXT("ResetMovieScene: MovieScene pointer is null."));
        return;
    }

    //remove possessables
    TArray<FGuid> PossessableGuids;
    for(int32 i=0; i<MovieScene->GetPossessableCount(); ++i)
    {
        PossessableGuids.Add(MovieScene->GetPossessable(i).GetGuid());
    }
    for(const FGuid& Guid : PossessableGuids)
    {
        MovieScene->RemovePossessable(Guid);   
    }

    //remove all spawnables
    TArray<FGuid> SpawnableGuids;
    for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
    {
        SpawnableGuids.Add(MovieScene->GetSpawnable(i).GetGuid());
    }
    for (const FGuid& Guid : SpawnableGuids)
    {
        MovieScene->RemoveSpawnable(Guid);
    }

    //remove all tracks
	TArray<UMovieSceneTrack*> Tracks = MovieScene->GetTracks();
    for (UMovieSceneTrack* Track : Tracks)
    {
        if(Track) MovieScene->RemoveTrack(*Track);
    }
	
    //clear child tracks from all folders
    // const TArray<UMovieSceneFolder*>& Folders = MovieScene->GetFolders();
    // for (UMovieSceneFolder* Folder : Folders)
    // {
    //     if (Folder) Folder->ClearChildTracks();
    // }

    UE_LOG(LogTemp, Log, TEXT("ResetMovieScene: MovieScene has been reset."));

}

bool UAInimateBPLibrary::GenerateAnimationFromJSON(
    const FString& JsonString,
    ULevelSequence* TargetSequence,
    ASkeletalMeshActor* TargetActor,
    UControlRig* ControlRigToUse,
    FString& OutErrorReason)
{
    //1. input validation
    if(!TargetSequence || !TargetActor || !ControlRigToUse)
    {
        OutErrorReason = TEXT("Invalid inputs: Target Sequence, Actor or Control rig is null. ");
        return false;
    }

    //2. parsing JSON string
    TSharedPtr<FJsonObject> RootJsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if(!FJsonSerializer::Deserialize(Reader, RootJsonObject) || !RootJsonObject.IsValid())
    {
        OutErrorReason = TEXT("Falied to parse JSON string. Check for syntax errors.");
        return false;
    }

    const TSharedPtr<FJsonObject>* MetaObject;
    if(!RootJsonObject->TryGetObjectField(TEXT("meta"), MetaObject))
    {
        OutErrorReason = TEXT("JSON is missing 'meta' object field.");
        return false;
    }

    double TotalFrames;
    if(!(*MetaObject)->TryGetNumberField(TEXT("total_frames"), TotalFrames) || TotalFrames <= 0)
    {
        OutErrorReason = TEXT("JSON 'meta' object is missing 'total_frames' field or it is zero.");
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* FramesArray;
    if(!RootJsonObject->TryGetArrayField(TEXT("frames"), FramesArray) || FramesArray->Num() == 0)
    {
        OutErrorReason = TEXT("Either JSON is missing 'frames' array field or it is empty.");
        return false;
    }

    //3. preparing level sequence.
    UMovieScene* MovieScene = TargetSequence->GetMovieScene();
    if(!MovieScene)
    {
        OutErrorReason = TEXT("Could not get MovieScene from Level Sequence Asset.");
        return false;
    }

    //clearing any existing data from the sequence.
    ResetMovieScene(MovieScene);
    MovieScene->SetPlaybackRange(0, TotalFrames -1); //sets frame range.

    //add target actor to the sequence.
    //FGuid ActorBinding is a global unique identifier for the actorbinding within the movie scene
    #if WITH_EDITOR
    FString ActorName = TargetActor->GetActorLabel();
    #else
    FString ActorName = TargetActor->GetName();
    #endif
    FGuid ActorBinding = MovieScene->AddPossessable(ActorName, TargetActor->GetClass());
    if(!ActorBinding.IsValid())
    {
        OutErrorReason = TEXT("Failed to add actor possessable to moviescene.");
        return false;
    }

    //adding control rig track for this actor
    UMovieSceneControlRigParameterTrack* ControlRigTrack = MovieScene->AddTrack<UMovieSceneControlRigParameterTrack>(ActorBinding);
    if(!ControlRigTrack)
    {
        OutErrorReason = TEXT("Failed to add control rig parameter track.");
        return false;
    }

    UMovieSceneControlRigParameterSection* ControlRigSection = Cast<UMovieSceneControlRigParameterSection>(ControlRigTrack->CreateNewSection());
    if(!ControlRigSection)
    {
        OutErrorReason = TEXT("Failed to create new section on control rig track.");
        return false;
    }

    ControlRigSection->SetControlRig(ControlRigToUse);
    //setting section range to entire duration of the animation.
    ControlRigSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(static_cast<int32>(TotalFrames))));

    //4. main loop: adding keyframes
    for(int32 FrameIndex = 0; FrameIndex<TotalFrames; ++FrameIndex)
    {
        const TSharedPtr<FJsonObject>* FrameObject = nullptr;
        if(!(*FramesArray)[FrameIndex]->TryGetObject(FrameObject)) continue; //skip malformed frames

        //extract and handle root transform from the frame object
        const TSharedPtr<FJsonObject>* RootTransformObject;
        if((*FrameObject)->TryGetObjectField(TEXT("root_transform"), RootTransformObject))
        {
            FTransform RootTransform = ParseTransformFromJson(*RootTransformObject);
            ControlRigSection->AddTransformParameterKey(FName("root_Transform"), FFrameNumber(FrameIndex), RootTransform);
        }

        //handle bone transforms
        const TSharedPtr<FJsonObject>* BoneTransformsObject;
        if((*FrameObject)->TryGetObjectField(TEXT("bone_transforms"), BoneTransformsObject))
        {
            //iterating each bone in "bone_transforms" object
            for(const auto& Bone : (*BoneTransformsObject)->Values)
            {
                const TSharedPtr<FJsonObject>* BoneTransformObj;
                if(Bone.Value->TryGetObject(BoneTransformObj))
                {
                    FString BoneName = Bone.Key;
                    FName ParameterName = FName(*FString::Printf(TEXT("%s_Transform"), *BoneName));
                    FTransform BoneTransform = ParseTransformFromJson(*BoneTransformObj);
                    ControlRigSection->AddTransformParameterKey(ParameterName, FFrameNumber(FrameIndex), BoneTransform);
                }
            }
        }
    }

    // 5.finalization
    //to let know the editor that the sequence is modified.
    TargetSequence->MarkPackageDirty();

    //baking is handled in bp.

	return true; // success!
}
