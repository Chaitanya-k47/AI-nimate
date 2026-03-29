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
#include "ReferenceSkeleton.h"

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

    //3. ************* fill bone tracks using IAnimationDataController *************
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

    //4. ************* Auto bone-Calibration *************
    const FReferenceSkeleton& RefSkeleton = TargetSkeleton->GetReferenceSkeleton();

    TArray<FTransform> GlobalRefPose;
    GlobalRefPose.SetNum(RefSkeleton.GetNum());
    for(int32 BoneIndex = 0; BoneIndex<RefSkeleton.GetNum(); ++BoneIndex)
    {
        int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
        FTransform LocalRef = RefSkeleton.GetRefBonePose()[BoneIndex];

        if(ParentIndex != INDEX_NONE)
        {
            GlobalRefPose[BoneIndex] = LocalRef * GlobalRefPose[ParentIndex];
        }
        else
        {
            GlobalRefPose[BoneIndex] = LocalRef;
        }
    }

    //now calculate correction deltas.
    //assuming frame 0 from json input is T-pose or close to it.
    //Correction = Inverse(SMPL_T_Pose) * Unreal_Ref_Pose.
    TMap<FName, FQuat> CalibrationOffsets;
    for(const TPair<FName, TArray<FTransform>>& BoneData : BoneTransformsPerBone)
    {
        FName BoneName = BoneData.Key;
        int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
        if(BoneIndex == INDEX_NONE) continue;

        //get smpl frame 0 rotation (global)
        FQuat SMPL_Rest_Rot = BoneData.Value[0].GetRotation();

        // Get Unreal Reference Rotation (Global)
        FQuat UE_Ref_Rot = GlobalRefPose[BoneIndex].GetRotation();

        //calculate delta:
        FQuat Correction = SMPL_Rest_Rot.Inverse() * UE_Ref_Rot;
        CalibrationOffsets.Add(BoneName, Correction);
    }
    
    //now actually create tracks for every bone we collected:
    for (const TPair<FName, TArray<FTransform>>& BoneData : BoneTransformsPerBone)
    {
        const FName BoneName = BoneData.Key;
        const TArray<FTransform>& RawGlobalKeys = BoneData.Value;

        // skip if skeleton doesnt have this bone
        const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
        if (BoneIndex == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("JSON bone '%s' not found in skeleton. Skipping."), *BoneName.ToString());
            continue;
        }
        //get parent index
        const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);

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

        // Retrieve the calculated correction for this bone
        FQuat BoneCorrection = CalibrationOffsets.Contains(BoneName) ? CalibrationOffsets[BoneName] : FQuat::Identity;

        for (int32 FrameIndex = 0; FrameIndex < TotalFrames; ++FrameIndex)
        {
            //1.Get Raw Global Transform from JSON
            FTransform CurrentGlobal = RawGlobalKeys[FrameIndex];

            //2. apply calibration
            FQuat CorrectedRot = CurrentGlobal.GetRotation() * BoneCorrection;
            CurrentGlobal.SetRotation(CorrectedRot);

            //3, convert to local space(relative to parent.)
            FTransform LocalTransform;
            if(ParentIndex == INDEX_NONE)  //#
            {
                //root bone: stays in global space.
                LocalTransform = CurrentGlobal;
            }
            else
            {
                // We need the Parent's Global Transform for this frame to do the math.
                // We must apply the Parent's calibration too!
                FName ParentName = RefSkeleton.GetBoneName(ParentIndex);
                
                FTransform ParentGlobal = FTransform::Identity;
                
                // Try to find parent data in JSON
                if(BoneTransformsPerBone.Contains(ParentName))
                {
                    ParentGlobal = BoneTransformsPerBone[ParentName][FrameIndex];
                    // Apply Parent Correction
                    FQuat ParentCorrection = CalibrationOffsets.Contains(ParentName) ? CalibrationOffsets[ParentName] : FQuat::Identity;
                    ParentGlobal.SetRotation(ParentGlobal.GetRotation() * ParentCorrection);
                }
                else
                {
                    // Fallback to Reference Pose if parent not animated
                    ParentGlobal = GlobalRefPose[ParentIndex];
                }

                // MATH: Local = Global_Child * Inverse(Global_Parent)
                LocalTransform = CurrentGlobal.GetRelativeTransform(ParentGlobal);
            }

            Translations.Add((FVector3f)LocalTransform.GetTranslation());
            
            FQuat4f ThisRot = (FQuat4f)LocalTransform.GetRotation();
            if(Rotations.Num() > 0 && ((Rotations.Last() | ThisRot) < 0.f))
            {
                //flip if dot < 0 to avoid 180° interpolation jumps
                ThisRot *= -1.f;
            }
            Rotations.Add(ThisRot);
            
            Scales.Add((FVector3f)LocalTransform.GetScale3D());

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

     //5.---------- save the package ----------
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

bool UAInimateBPLibrary::RunSystemCommandAndWait(const FString& Command)
{
    uint32 ProcessID = 0;
    FString Executable = TEXT("cmd.exe");

    // /c tells cmd to execute then terminate.
    FString Parameters = FString::Printf(TEXT("/c \"%s\""), *Command);

    UE_LOG(LogTemp, Warning, TEXT("Executing System Command: %s"), *Parameters);

	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
        *Executable, 
        *Parameters, 
        false, // bLaunchDetached (False means it's a child process of UE5)
        true,  // bLaunchHidden (True means no ugly black command prompt window pops up!)
        false, // bLaunchReallyHidden
        &ProcessID, 
        0, 
        nullptr, 
        nullptr
    );

    if(ProcHandle.IsValid())
    {
        //freeze UE5 main thread until process finishes:
        FPlatformProcess::WaitForProc(ProcHandle);

        int32 ReturnCode = 0;
        FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);

        FPlatformProcess::CloseProc(ProcHandle);

        //if python execution finished successfully, Exist code 0.
        if (ReturnCode == 0)
        {
            UE_LOG(LogTemp, Log, TEXT("System Command completed successfully."));
            return true; 
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("System Command failed with exit code: %d"), ReturnCode);
            return false;
        }
    }

    UE_LOG(LogTemp, Error, TEXT("Failed to create the system process."));
    return false;    
}