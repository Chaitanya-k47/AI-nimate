// Fill out your copyright notice in the Description page of Project Settings.


#include "JsonHelpers.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"


bool UJsonHelpers::GetJsonArrayFieldAsStrings(
		const FString& FilePath,
		UVaRestJsonObject* JsonObject,
		const FString& ArrayFieldName, 
		TArray<FString>& OutArrayStrings
	)
{
    OutArrayStrings.Empty();
    

    //mutually exclusive input validation
    if (!FilePath.IsEmpty() && JsonObject)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid usage: both FilePath and JsonObject were provided. Use only one."));
        ensureMsgf(false, TEXT("UJsonHelpers::GetJsonArrayFieldAsStrings: both FilePath and JsonObject are set! Provide only one."));
        return false;
    }

    //pointer for root json object
    TSharedPtr<FJsonObject> RootObject;

    //if file path is provided, load json from file
    if(!FilePath.IsEmpty())
    {
        FString JsonRaw;
        if(!FFileHelper::LoadFileToString(JsonRaw, *FilePath))
        {
            UE_LOG(LogTemp, Error, TEXT("GetJsonArrayFieldAsStrings: failed to load file %s"), *FilePath);
            return false;
        }

        //creating a JSON reader
        TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
    
        //parse
        if(!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("GetJsonArrayFieldAsStrings: failed to parse JSON file %s"), *FilePath);
            return false;
        }
    }
    //if VaRestJsonObject is provided
    else if(JsonObject)
    {
        RootObject = JsonObject->GetRootObject();
    }
    //neither provided
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid input: Neither FilePath nor JsonObject was provided."));
        return false;
    }

    //extract array field
    const TArray<TSharedPtr<FJsonValue>> *ArrayPtr = nullptr;
    if(!RootObject->TryGetArrayField(ArrayFieldName, ArrayPtr) || !ArrayPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("GetJsonArrayFieldAsStrings: field '%s' not found or is not an array"), *ArrayFieldName);
        return false;
    }

    for(const TSharedPtr<FJsonValue> &Element : *ArrayPtr)
    {
        if(!Element.IsValid()) continue;

        FString ElementString;

        //handle different json value types
        switch(Element->Type)
        {
            case EJson::Object:
            {
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ElementString);
                FJsonSerializer::Serialize(Element->AsObject().ToSharedRef(), Writer);
                break;
            }
            case EJson::Array:
            {
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ElementString);
                FJsonSerializer::Serialize(Element->AsArray(), Writer);
                break;
            }
            case EJson::String:
            {
                ElementString = Element->AsString();
                break;
            }
            case EJson::Number:
            {
                ElementString = FString::SanitizeFloat(Element->AsNumber());
                break;
            }
            case EJson::Boolean:
            {
                ElementString = Element->AsBool() ? TEXT("true") : TEXT("false");
                break;
            }
            default:
            {
                ElementString = TEXT("null");
                break;
            }
        }

        OutArrayStrings.Add(ElementString);
    
    }

    return true;

}

