#pragma once
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FParamSchemaBuilder
{
public:
	FParamSchemaBuilder& Required(const FString& Name, const FString& Type, const FString& Desc)
	{
		AddParam(Name, Type, Desc, true);
		return *this;
	}

	FParamSchemaBuilder& Optional(const FString& Name, const FString& Type, const FString& Desc, const FString& Default = TEXT(""))
	{
		AddParam(Name, Type, Desc, false, Default);
		return *this;
	}

	TSharedPtr<FJsonObject> Build()
	{
		return Schema;
	}

private:
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();

	void AddParam(const FString& Name, const FString& Type, const FString& Desc, bool bRequired, const FString& Default = TEXT(""))
	{
		TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
		Param->SetStringField(TEXT("type"), Type);
		Param->SetStringField(TEXT("description"), Desc);
		Param->SetBoolField(TEXT("required"), bRequired);
		if (!Default.IsEmpty())
		{
			Param->SetStringField(TEXT("default"), Default);
		}
		Schema->SetObjectField(Name, Param);
	}
};
