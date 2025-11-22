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
	
    #if WITH_EDITOR
    // Clear folder structure (UE 5.6: use GetRootFolders + EmptyRootFolders)
    {
        TArrayView<UMovieSceneFolder* const> RootFoldersView = MovieScene->GetRootFolders();
        TArray<UMovieSceneFolder*> RootFolders;
        RootFolders.Append(RootFoldersView.GetData(), RootFoldersView.Num());

        for (UMovieSceneFolder* Folder : RootFolders)
        {
            if (!Folder) continue;

            //remove child tracks
            Folder->ClearChildTracks();
            
            //remove child folders
            const TArrayView<UMovieSceneFolder* const> ChildFoldersView = Folder->GetChildFolders();
            TArray<UMovieSceneFolder*> ChildFolders;
            ChildFolders.Append(ChildFoldersView.GetData(), ChildFoldersView.Num());
            for (UMovieSceneFolder* SubFolder : ChildFolders)
            {
                Folder->RemoveChildFolder(SubFolder);
            }
        }

    }
    #endif

    UE_LOG(LogTemp, Log, TEXT("ResetMovieScene: MovieScene has been reset."));

}

bool UAInimateBPLibrary::GenerateAnimationFromJSON(
    const FString& JsonString,
    ULevelSequence* TargetSequence,
    ASkeletalMeshActor* TargetActor,
    UControlRig* ControlRigToUse,
    int32& OutTotalFrames,
	int32& OutFrameRate,
    FString& OutErrorReason)
{

    OutTotalFrames = 0;
    OutFrameRate = 0;

    //1.************* input validation *************
    if(!TargetSequence || !TargetActor || !ControlRigToUse)
    {
        OutErrorReason = TEXT("Invalid inputs: Target Sequence, Actor or Control rig is null. ");
        return false;
    }


    //2.************* parsing JSON string *************
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
    OutTotalFrames = static_cast<int32>(TotalFrames);

    double FrameRate;
    if (!(*MetaObject)->TryGetNumberField(TEXT("frame_rate"), FrameRate) || FrameRate <= 0)
    {
        OutErrorReason = TEXT("JSON 'meta' object is missing 'frame_rate' field or it is zero.");
        return false;
    }
    OutFrameRate = static_cast<int32>(FrameRate);

    const TArray<TSharedPtr<FJsonValue>>* FramesArray;
    if(!RootJsonObject->TryGetArrayField(TEXT("frames"), FramesArray) || FramesArray->Num() == 0)
    {
        OutErrorReason = TEXT("Either JSON is missing 'frames' array field or it is empty.");
        return false;
    }


    //3.************* preparing level sequence. *************
    UMovieScene* MovieScene = TargetSequence->GetMovieScene();
    if(!MovieScene)
    {
        OutErrorReason = TEXT("Could not get MovieScene from Level Sequence Asset.");
        return false;
    }

    //clearing any existing data from the sequence.
    ResetMovieScene(MovieScene);
    //MovieScene->SetPlaybackRange(0, TotalFrames-1); //sets frame range.
    int32 LastFrame = static_cast<int32>(TotalFrames) - 1;
    MovieScene->SetPlaybackRange(0, LastFrame);

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
    ControlRigTrack->AddSection(*ControlRigSection);

    //linking the control rig class/instance with the section
    ControlRigSection->SetControlRig(ControlRigToUse);

    //setting section range to entire duration of the animation.
    //ControlRigSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(static_cast<int32>(TotalFrames))));
    TRange<FFrameNumber> DesiredRange = TRange<FFrameNumber>::Inclusive(FFrameNumber(0), FFrameNumber(LastFrame));
    ControlRigSection->SetRange(DesiredRange);

    //register all parameters for all the bones before adding key
    //a. for root bone:
    ControlRigSection->AddTransformParameter(FName(TEXT("root_Transform")), TOptional<FEulerTransform>(), false);
    //b. for rest of the bones:
    const TSharedPtr<FJsonObject>* Frame;
    if((*FramesArray)[0]->TryGetObject(Frame))
    {
        const TSharedPtr<FJsonObject>* Bones;
        if((*Frame)->TryGetObjectField(TEXT("bone_transforms"), Bones))
        {
            //iterating each bone in "bone_transforms" object
            for(const auto& Bone : (*Bones)->Values)
            {
                FString BoneName = Bone.Key;
                FName ParameterName = FName(*FString::Printf(TEXT("%s_Transform"), *BoneName));
                if(!ControlRigSection->HasTransformParameter(ParameterName))
                    ControlRigSection->AddTransformParameter(ParameterName, TOptional<FEulerTransform>(), false); 
            }
        }
    }
    ControlRigSection->ReconstructChannelProxy();
    


    //------------------------DEBUG------------------------//
    {
        UE_LOG(LogTemp, Warning, TEXT("---- DEBUG: ControlRigSection Parameters & Channels ----"));
        UE_LOG(LogTemp, Warning, TEXT("After registering parameters and reconstructing ChannelProxy:"));
        // 1) list parameter names we created (Section maintains ParameterToChannelMap-like data)
        TSet<FName> ParamNames;
        ControlRigSection->GetParameterNames(ParamNames); // if available; if not, use your own list

        for (const FName& P : ParamNames)
        {
            UE_LOG(LogTemp, Warning, TEXT("Param: %s"), *P.ToString());
        }

        // 2) inspect channel proxy to count keys per channel (safe approach)
        const FMovieSceneChannelProxy& Proxy = ControlRigSection->GetChannelProxy();
        const TArrayView<const FMovieSceneChannelEntry>& Entries = Proxy.GetAllEntries();

        for (int32 EntryIdx = 0; EntryIdx < Entries.Num(); ++EntryIdx)
        {
            const FMovieSceneChannelEntry& Entry = Entries[EntryIdx];
            
            TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();

            for (int32 ChanIdx = 0; ChanIdx < Channels.Num(); ++ChanIdx)
            {
                FMovieSceneChannel* Channel = Channels[ChanIdx];
                if (Channel)
                {
                    UE_LOG(LogTemp, Warning, TEXT("Channel no: [%d], for Entry no: [%d]"), ChanIdx, EntryIdx);
                    continue;
                }
            }
            UE_LOG(LogTemp, Warning, TEXT("Total Channels: %d, for this Entry no: [%d]"), Channels.Num(), EntryIdx); 
        }
        UE_LOG(LogTemp, Warning, TEXT("Total Entries: %d"), Entries.Num());

        UE_LOG(LogTemp, Warning, TEXT("---- END DEBUG ----"));
    }
    //------------------------DEBUG------------------------//  

    // --- DEFINITIVE DIAGNOSTIC LOG ---
    TArray<FTransformParameterNameAndCurves>& TransformCurvez = ControlRigSection->GetTransformParameterNamesAndCurves();
    UE_LOG(LogTemp, Error, TEXT("CRITICAL DIAGNOSTIC: Number of Transform Curve structures found after Reconstruct outside for loop: %d"), TransformCurvez.Num());
    // --- END DIAGNOSTIC ---

    //4.************* main loop: adding keyframes *************
    for(int32 FrameIndex = 0; FrameIndex<TotalFrames; ++FrameIndex)
    {
        // --- START OF DEFINITIVE DIAGNOSTIC LOG (RUNS EVERY FRAME) ---
        if (FrameIndex == 0 || FrameIndex == TotalFrames - 1) // Only log for first and last frame to avoid spam
        {
            UE_LOG(LogTemp, Error, TEXT("--- FRAME %d DIAGNOSTIC ---"), FrameIndex);
            
            // Test 1: Can we access the transform curve structures inside the loop?
            TArray<FTransformParameterNameAndCurves>& TransformCurves = ControlRigSection->GetTransformParameterNamesAndCurves();
            UE_LOG(LogTemp, Error, TEXT("Frame %d: Found %d Transform Curve structures."), FrameIndex, TransformCurves.Num());

            // Test 2: If they exist, let's inspect the 'pelvis_Transform' parameter specifically.
            if (TransformCurves.Num() > 0)
            {
                FName PelvisParamName = FName("pelvis_Transform");
                for (FTransformParameterNameAndCurves& Curves : TransformCurves)
                {
                    if (Curves.ParameterName == PelvisParamName)
                    {
                        // Check key counts BEFORE adding a new key.
                        int32 KeysBefore_LocX = Curves.Translation[0].GetNumKeys();
                        int32 KeysBefore_RotZ = Curves.Rotation[2].GetNumKeys();
                        UE_LOG(LogTemp, Error, TEXT("Frame %d (BEFORE AddKey): 'pelvis_Transform' Loc.X keys: %d, Rot.Z keys: %d"), FrameIndex, KeysBefore_LocX, KeysBefore_RotZ);
                        break; // Found it, no need to continue this inner loop
                    }
                }
            }
        }
        // --- END OF DIAGNOSTIC LOG ---

        const TSharedPtr<FJsonObject>* FrameObject = nullptr;
        if(!(*FramesArray)[FrameIndex]->TryGetObject(FrameObject)) continue; //skip malformed frames

        //extract and handle root transform from the frame object
        const TSharedPtr<FJsonObject>* RootTransformObject;
        if((*FrameObject)->TryGetObjectField(TEXT("root_transform"), RootTransformObject))
        {
            FTransform RootTransform = ParseTransformFromJson(*RootTransformObject);
            ControlRigSection->AddTransformParameterKey(FName("root_Transform"), FFrameNumber(FrameIndex), RootTransform);
            //AddKeyToTransformChannels(FName("root_Transform"), RootTransform);
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
                    //AddKeyToTransformChannels(ParameterName, BoneTransform);
                }
            }
        }
    }



    //------------------------DEBUG------------------------//
    {
        UE_LOG(LogTemp, Warning, TEXT("---- DEBUG: ControlRigSection Parameters & Channels ----"));
        UE_LOG(LogTemp, Warning, TEXT("After adding keys: "));

        // 1) list parameter names we created (Section maintains ParameterToChannelMap-like data)
        TSet<FName> ParamNames;
        ControlRigSection->GetParameterNames(ParamNames); // if available; if not, use your own list

        for (const FName& P : ParamNames)
        {
            UE_LOG(LogTemp, Warning, TEXT("Param: %s"), *P.ToString());
        }

        // 2) inspect channel proxy to count keys per channel (safe approach)
        const FMovieSceneChannelProxy& Proxy = ControlRigSection->GetChannelProxy();
        const TArrayView<const FMovieSceneChannelEntry>& Entries = Proxy.GetAllEntries();

        for (int32 EntryIdx = 0; EntryIdx < Entries.Num(); ++EntryIdx)
        {
            const FMovieSceneChannelEntry& Entry = Entries[EntryIdx];
            
            TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();

            for (int32 ChanIdx = 0; ChanIdx < Channels.Num(); ++ChanIdx)
            {
                FMovieSceneChannel* Channel = Channels[ChanIdx];
                if (Channel)
                {
                    UE_LOG(LogTemp, Warning, TEXT("Channel no: [%d], for Entry no: [%d]"), ChanIdx, EntryIdx);
                    continue;
                }
            }
            UE_LOG(LogTemp, Warning, TEXT("Total Channels: %d, for this Entry no: [%d]"), Channels.Num(), EntryIdx); 
        }
        UE_LOG(LogTemp, Warning, TEXT("Total Entries: %d"), Entries.Num());

        UE_LOG(LogTemp, Warning, TEXT("---- END DEBUG ----"));
    }
    //------------------------DEBUG------------------------//  



    // 5.************* finalization *************
    //to let know the editor that the sequence is modified.
    TargetSequence->MarkPackageDirty();

    //baking is handled in bp.

    // --- FINAL DIAGNOSTIC LOG (CORRECTED) ---
    UE_LOG(LogTemp, Error, TEXT("--- FINAL C++ STATE CHECK ---"));
    UMovieScene* FinalMovieScene = TargetSequence->GetMovieScene();
    if (FinalMovieScene)
    {
        TRange<FFrameNumber> FinalRange = FinalMovieScene->GetPlaybackRange();
        
        // Get the lower and upper bounds as FFrameNumber structs
        FFrameNumber StartFrame = FinalRange.GetLowerBoundValue();
        FFrameNumber EndFrame = FinalRange.GetUpperBoundValue();

        // Access the .Value member to get the integer
        UE_LOG(LogTemp, Error, TEXT("C++ sees final Playback Range as: Start=%d, End=%d"), StartFrame.Value, EndFrame.Value);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("C++ could not get MovieScene at the very end."));
    }
    UE_LOG(LogTemp, Error, TEXT("-----------------------------"));
    // --- END FINAL DIAGNOSTIC ---

    

	return true; // success!
}
