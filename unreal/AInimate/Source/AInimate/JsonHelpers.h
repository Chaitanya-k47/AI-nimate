// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JsonObjectConverter.h"
#include "VaRestJsonObject.h"
#include "JsonHelpers.generated.h"


/**
 * 
 */
UCLASS()
class AINIMATE_API UJsonHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()


public:

	UFUNCTION(BlueprintCallable, Category = "JSON")
	static bool GetJsonArrayFieldAsStrings(
		const FString& FilePath,
		UVaRestJsonObject* JsonObject,
		const FString& ArrayFieldName, 
		TArray<FString>& OutArrayStrings
	);
	
};
