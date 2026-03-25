#include "AInimateBPLibrary.h"

#if WITH_EDITOR

#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"

#include "IMovieSceneTools.h"
#include "AssetToolsModule.h"
#include "Factories/AnimSequenceFactory.h"
#include "Exporters/AnimSeqExportOption.h"
#include "SequencerTools.h"
#include "MovieSceneBindingProxy.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#endif

#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "MovieScene.h"
#include "MovieSceneFolder.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "GameFramework/Actor.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"

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
    FString& OutErrorReason)
{


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

    const TSharedPtr<FJsonObject>* MetaObject = nullptr;
    if(!RootJsonObject->TryGetObjectField(TEXT("meta"), MetaObject))
    {
        OutErrorReason = TEXT("JSON is missing 'meta' object field.");
        return false;
    }

    double TotalFramesDouble = 0.0;
    if(!(*MetaObject)->TryGetNumberField(TEXT("total_frames"), TotalFramesDouble) || TotalFramesDouble <= 0)
    {
        OutErrorReason = TEXT("JSON 'meta' object is missing 'total_frames' field or it is zero.");
        return false;
    }

    double FrameRateDouble = 30.0;
    if (!(*MetaObject)->TryGetNumberField(TEXT("frame_rate"), FrameRateDouble) || FrameRateDouble <= 0)
    {
        OutErrorReason = TEXT("JSON 'meta' object is missing 'frame_rate' field or it is zero.");
        return false;
    }
    const int32 FrameRateInt = FMath::Max(1, static_cast<int32>(FrameRateDouble));
    const int32 TotalFrames = static_cast<int32>(TotalFramesDouble);


    const TArray<TSharedPtr<FJsonValue>>* FramesArray = nullptr;
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

    const FFrameRate DisplayRate(FrameRateInt, 1);
    MovieScene->SetDisplayRate(DisplayRate);
    MovieScene->SetPlaybackRange(0, TotalFrames-1);

    //add target actor to the sequence.
    //FGuid ActorBinding is a global unique identifier for the actorbinding within the movie scene
    #if WITH_EDITOR
    FString ActorName = TargetActor->GetActorLabel(); //requires instance
    #else
    FString ActorName = TargetActor->GetName();
    #endif

    const FGuid ActorBinding = MovieScene->AddPossessable(ActorName, TargetActor->GetClass());
    if(!ActorBinding.IsValid())
    {
        OutErrorReason = TEXT("Failed to add actor possessable to moviescene.");
        return false;
    }

    //now bing this GUID to the actual actor instance
    ULevelSequence* LevelSequence = TargetSequence;
    if(LevelSequence)
    {
        //clear previous bindings first
        LevelSequence->UnbindPossessableObjects(ActorBinding);

        LevelSequence->BindPossessableObject(ActorBinding, *TargetActor, TargetActor->GetWorld());
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

    ControlRigSection->ControlRigClass = ControlRigToUse->GetClass();

    //setting section range to entire duration of the animation.
    ControlRigSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(TotalFrames)));

    //register all parameters for all the bones before adding key
    //a. for root bone:
    ControlRigSection->AddTransformParameter(FName(TEXT("root_Transform")), TOptional<FEulerTransform>(), true);
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
                    ControlRigSection->AddTransformParameter(ParameterName, TOptional<FEulerTransform>(), true);
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

    //to let know the editor that the sequence is modified.
    TargetSequence->MarkPackageDirty();

    // 5.************* BAKE TO ANIMSEQUENCE + SAVE *************
    #if WITH_EDITOR

        //Get World
        UWorld* World = TargetActor->GetWorld();
        if(!World)
        {
            OutErrorReason = TEXT("GenerateAnimationFromJSON: TargetActor has no valid World.");
            return false;
        }

        //get SkeletalMeshComponent
        USkeletalMeshComponent* SkeletalMeshComponent = TargetActor->GetSkeletalMeshComponent();
        if(!SkeletalMeshComponent)
        {
            OutErrorReason = TEXT("GenerateAnimationFromJSON: TargetActor has no SkeletalMeshComponent.");
            return false;
        }

        //get SkeletalMesh
        USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
        if(!SkeletalMesh)
        {
            OutErrorReason = TEXT("GenerateAnimationFromJSON: SkeletalMeshComponent has no SkeletalMesh asset.");
            return false;
        }

        //get Skeleton
        USkeleton* TargetSkeleton = SkeletalMesh->GetSkeleton();
        if (!TargetSkeleton)
        {
            OutErrorReason = TEXT("GenerateAnimationFromJSON: Could not retrieve Skeleton from SkeletalMesh.");
            return false;
        }   

        //decide where to create the AnimSequence asset
        const FString SequencePackageName = TargetSequence->GetOutermost()->GetName();
        const FString SequenceFolder = FPackageName::GetLongPackagePath(SequencePackageName);
        const FString AnimFolder = SequenceFolder / TEXT("Animations");

        // Unique asset name (like "GeneratedAnim_1234567890")
        const int64 TimeStamp = FDateTime::Now().ToUnixTimestamp();
        const FString BaseAssetName = FString::Printf(TEXT("GeneratedAnim_%lld"), TimeStamp);

        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

        FString UniquePackageName;
        FString UniqueAssetName;
        AssetToolsModule.Get().CreateUniqueAssetName(
            AnimFolder / BaseAssetName,
            TEXT(""),
            UniquePackageName,
            UniqueAssetName
        );

        UAnimSequenceFactory* AnimFactory = NewObject<UAnimSequenceFactory>();
        AnimFactory->TargetSkeleton = TargetSkeleton;

        UObject* NewAssetObj = AssetToolsModule.Get().CreateAsset(
            UniqueAssetName,
            AnimFolder,
            UAnimSequence::StaticClass(),
            AnimFactory
        );

        //create AnimSequence asset with AnimSequenceFactory + skeleton
        UAnimSequence* NewAnimSequence = Cast<UAnimSequence>(NewAssetObj);
        if(!NewAnimSequence)
        {
            OutErrorReason = TEXT("GenerateAnimationFromJSON: Failed to create AnimSequence asset.");
            return false;
        }

        //Register in AssetRegistry and mark dirty
        {
            FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            AssetRegistryModule.Get().AssetCreated(NewAnimSequence);
            NewAnimSequence->MarkPackageDirty();
        }

        //configure AnimSeqExportOption
        UAnimSeqExportOption* ExportOptions = NewObject<UAnimSeqExportOption>();
        ExportOptions->bExportTransforms              = true;
        ExportOptions->bExportMorphTargets            = false;
        ExportOptions->bExportAttributeCurves         = false;
        ExportOptions->bExportMaterialCurves          = false;
        ExportOptions->bEvaluateAllSkeletalMeshComponents = false;
        ExportOptions->bRecordInWorldSpace            = false;

        //use our custom frame rate & time range: [0, TotalFrames-1]
        ExportOptions->bUseCustomTimeRange  = true;
        ExportOptions->CustomDisplayRate    = DisplayRate;
        ExportOptions->CustomStartFrame     = FFrameNumber(0);
        ExportOptions->CustomEndFrame       = FFrameNumber(TotalFrames - 1);

        ExportOptions->bUseCustomFrameRate  = true;
        ExportOptions->CustomFrameRate      = DisplayRate;

        //building binding proxy for export
        FMovieSceneBindingProxy BindingProxy(ActorBinding, TargetSequence);

        //call SequencerTools ExportAnimSequence
        const bool bCreateLink = false;
        const bool bExportOk = USequencerToolsFunctionLibrary::ExportAnimSequence(
            World,
            TargetSequence,
            NewAnimSequence,
            ExportOptions,
            BindingProxy,
            bCreateLink
        );

        if (!bExportOk)
        {
            OutErrorReason = TEXT("GenerateAnimationFromJSON: ExportAnimSequence failed.");
            return false;
        }

        //save the populated AnimSequence package
        {
            UPackage* AnimPackage = NewAnimSequence->GetOutermost();
            const FString PackageFileName = FPackageName::LongPackageNameToFilename(AnimPackage->GetName(), FPackageName::GetAssetPackageExtension());
            
            //set up save args for UE 5.6
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
            SaveArgs.Error = GError;
            SaveArgs.SaveFlags = SAVE_NoError;

            const bool bSavedOk = UPackage::SavePackage(
                AnimPackage,
                NewAnimSequence,
                *PackageFileName,
                SaveArgs
            );

            if (!bSavedOk)
            {
                OutErrorReason = TEXT("GenerateAnimationFromJSON: Baked anim but failed to save package.");
                return false;
            }       
        
            UE_LOG(LogTemp, Log, TEXT("GenerateAnimationFromJSON: SUCCESS. Animation baked & saved to: %s"), *AnimPackage->GetName());
        }

    #else
        //in non-editor builds, we cannot bake using SequencerTools
        OutErrorReason = TEXT("GenerateAnimationFromJSON: Baking is editor-only (SequencerTools not available).");
        return false;

    #endif 

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

UAnimSequence* UAInimateBPLibrary::GenerateAnimationFromJSON_Direct(
    const FString& JsonString,
    USkeletalMesh* TargetMesh,
    const FString& OutputFolder,
    FString& OutErrorReason)
{
#if !WITH_EDITOR
    OutErrorReason = TEXT("GenerateAnimationFromJSON_Direct is editor-only.");
    return nullptr;

#else
    OutErrorReason.Reset();

    if(!TargetMesh)
    {
        OutErrorReason = TEXT("TargetMesh is null.");
        return nullptr;
    }

    USkeleton* TargetSkeleton = TargetMesh->GetSkeleton();
    if(!TargetSkeleton)
    {
        OutErrorReason = TEXT("TargetMesh has no Skeleton.");
        return nullptr;
    }

    //1.************* parsing JSON string *************
    TSharedPtr<FJsonObject> RootJsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if(!FJsonSerializer::Deserialize(Reader, RootJsonObject) || !RootJsonObject.IsValid())
    {
        OutErrorReason = TEXT("Falied to parse JSON string. Check for syntax errors.");
        return nullptr;
    }

    const TSharedPtr<FJsonObject>* MetaObject = nullptr;
    if(!RootJsonObject->TryGetObjectField(TEXT("meta"), MetaObject))
    {
        OutErrorReason = TEXT("JSON is missing 'meta' object field.");
        return nullptr;
    }

    double TotalFramesDouble = 0.0;
    if(!(*MetaObject)->TryGetNumberField(TEXT("total_frames"), TotalFramesDouble) || TotalFramesDouble <= 0)
    {
        OutErrorReason = TEXT("JSON 'meta' object is missing 'total_frames' field or it is zero.");
        return nullptr;
    }

    double FrameRateDouble = 30.0;
    if (!(*MetaObject)->TryGetNumberField(TEXT("frame_rate"), FrameRateDouble) || FrameRateDouble <= 0)
    {
        OutErrorReason = TEXT("JSON 'meta' object is missing 'frame_rate' field or it is zero.");
        return nullptr;
    }
    const int32 FrameRateInt = FMath::Max(1, static_cast<int32>(FrameRateDouble));
    const int32 TotalFrames = static_cast<int32>(TotalFramesDouble);


    const TArray<TSharedPtr<FJsonValue>>* FramesArray = nullptr;
    if(!RootJsonObject->TryGetArrayField(TEXT("frames"), FramesArray) || FramesArray->Num() == 0)
    {
        OutErrorReason = TEXT("Either JSON is missing 'frames' array field or it is empty.");
        return nullptr;
    }

    //2.************* create AnimSequence asset *************
    FString AnimFolder = OutputFolder;
    if(AnimFolder.IsEmpty()) //if path not specidied
    {
        //save at default path:
        const FString MeshPackageName = TargetMesh->GetOutermost()->GetName();
        const FString Meshfolder = FPackageName::GetLongPackagePath(MeshPackageName);
        AnimFolder = Meshfolder/TEXT("Animations");

    }

    const int64 TimeStamp = FDateTime::Now().ToUnixTimestamp();
    const FString BaseAssetName = FString::Printf(TEXT("GeneratedAnim_%lld"), TimeStamp);

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

    FString UniquePackageName;
    FString UniqueAssetName;
    AssetToolsModule.Get().CreateUniqueAssetName(
        AnimFolder / BaseAssetName,
        TEXT(""),
        UniquePackageName,
        UniqueAssetName
    );

    UAnimSequenceFactory* AnimFactory = NewObject<UAnimSequenceFactory>();
    AnimFactory->TargetSkeleton = TargetSkeleton;

    UObject* NewAssetObj = AssetToolsModule.Get().CreateAsset(
        UniqueAssetName,
        AnimFolder,
        UAnimSequence::StaticClass(),
        AnimFactory
    );
    
    UAnimSequence* NewAnimSequence = Cast<UAnimSequence>(NewAssetObj);
    if(!NewAnimSequence)
    {
        OutErrorReason = TEXT("Failed to create AnimSequence asset.");
        return nullptr;
    }

    //register and mark dirty
    {
        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        AssetRegistryModule.Get().AssetCreated(NewAnimSequence);
        NewAnimSequence->MarkPackageDirty();        
    }

    //3.************* fill bone tracks using IAnimationDataController *************
    IAnimationDataController& Controller = NewAnimSequence->GetController();
    Controller.OpenBracket(FText::FromString(TEXT("AInimate: GenerateAnimationFromJSON_Direct")));

    //set timing info.
    const FFrameRate AnimFrameRate(FrameRateInt, 1);
    Controller.SetFrameRate(AnimFrameRate);
    Controller.SetNumberOfFrames(FFrameNumber(TotalFrames));

    //build per-bone arrays of keys
    //mapping: BoneName->arrays of pos/rot/scale
    TMap<FName, TArray<FTransform>> BoneTransformsPerBone;

    for(int32 FrameIndex=0; FrameIndex<TotalFrames; ++FrameIndex)
    {
        const TSharedPtr<FJsonObject>* FrameObject = nullptr;
        if(!(*FramesArray)[FrameIndex]->TryGetObject(FrameObject)) continue; //skip malformed frames
    
        const TSharedPtr<FJsonObject>* BoneTransformsObject = nullptr;
        if (!(*FrameObject)->TryGetObjectField(TEXT("bone_transforms"), BoneTransformsObject)) continue;
        
        for(const auto& BonePair:(*BoneTransformsObject)->Values)
        {
            const FString& BoneNameStr = BonePair.Key;
            FName BoneName(*BoneNameStr);

            const TSharedPtr<FJsonObject>* BoneTransformJson = nullptr;
            if (!BonePair.Value->TryGetObject(BoneTransformJson)) continue;

            FTransform UETransform = ParseTransformFromJson(*BoneTransformJson);

            TArray<FTransform>& BoneArray = BoneTransformsPerBone.FindOrAdd(BoneName);
            if(BoneArray.Num() != TotalFrames) BoneArray.SetNum(TotalFrames); //ensure size
            BoneArray[FrameIndex] = UETransform;       
            
        }

    }

    const FReferenceSkeleton& RefSkeleton = TargetSkeleton->GetReferenceSkeleton();

    //now actually create tracks for every bone we collected:
    for (const TPair<FName, TArray<FTransform>>& BoneData : BoneTransformsPerBone)
    {
        const FName BoneName = BoneData.Key;
        const TArray<FTransform>& Keys = BoneData.Value;

        // skip if skeleton doesn't have this bone
        const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
        if (BoneIndex == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("JSON bone '%s' not found in skeleton. Skipping."), *BoneName.ToString());
            continue;
        }

        //----test block----//
        const FTransform& RefPose = RefSkeleton.GetRefBonePose()[BoneIndex];
        const FVector3f RefPos   = (FVector3f)RefPose.GetTranslation();
        const FVector3f RefScale = (FVector3f)RefPose.GetScale3D();
        //----test block----//

        // Create track if needed
        Controller.RemoveBoneTrack(BoneName, false);
        Controller.AddBoneCurve(BoneName, false);

        //build separate arrays for translations, rotations, scales
        TArray<FVector3f> Translations;
        TArray<FQuat4f>   Rotations;
        TArray<FVector3f> Scales;

        Translations.Reserve(TotalFrames);
        Rotations.Reserve(TotalFrames);
        Scales.Reserve(TotalFrames);

        for (int32 FrameIndex = 0; FrameIndex < TotalFrames; ++FrameIndex)
        {
            const FTransform& T = Keys[FrameIndex];

            // Translations.Add((FVector3f)T.GetTranslation());
            // Rotations.Add((FQuat4f)T.GetRotation());
            // Scales.Add((FVector3f)T.GetScale3D());

            //----test block----//
            //use reference-pose position, not JSON position
            //use ref-pose scale (or FVector3f(1,1,1))
            Translations.Add(RefPos);
            Scales.Add(RefScale);

            //use JSON rotation, but keep quaternion sign stable
            FQuat4f ThisRot = (FQuat4f)T.GetRotation();
            if(Rotations.Num() > 0 && ((Rotations.Last() | ThisRot) < 0.f))
            {
                //flip if dot < 0 to avoid 180° interpolation jumps
                ThisRot = ThisRot * -1.f;
            }
            Rotations.Add(ThisRot);

            //----test block----//

        }

        Controller.SetBoneTrackKeys(
            BoneName,
            Translations,
            Rotations,
            Scales,
            false
        );
    }

    Controller.NotifyPopulated(); // optional, keeps editor UI in sync
    Controller.CloseBracket();

     //4.---------- save the package ----------
    {
        UPackage* AnimPackage = NewAnimSequence->GetOutermost();
        const FString PackageFileName = FPackageName::LongPackageNameToFilename(
            AnimPackage->GetName(),
            FPackageName::GetAssetPackageExtension()
        );

        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
        SaveArgs.Error = GError;
        SaveArgs.SaveFlags = SAVE_NoError;

        const bool bSavedOk = UPackage::SavePackage(
            AnimPackage,
            NewAnimSequence,
            *PackageFileName,
            SaveArgs
        );

        if (!bSavedOk)
        {
            OutErrorReason = TEXT("Baked AnimSequence but failed to save.");
            return nullptr;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("GenerateAnimationFromJSON_Direct: SUCCESS (%s)"), *NewAnimSequence->GetPathName());

    return NewAnimSequence;

#endif //WITH_EDITOR

}
