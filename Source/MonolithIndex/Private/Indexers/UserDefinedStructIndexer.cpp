#include "Indexers/UserDefinedStructIndexer.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UnrealType.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

bool FUserDefinedStructIndexer::IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId)
{
	UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(LoadedAsset);
	if (!UDS) return false;

	const void* DefaultInstance = UDS->GetDefaultInstance();

	auto Props = MakeShared<FJsonObject>();
	int32 FieldCount = 0;

	TArray<TSharedPtr<FJsonValue>> Fields;
	for (TFieldIterator<FProperty> It(UDS); It; ++It)
	{
		FProperty* Prop = *It;
		FieldCount++;

		auto Field = MakeShared<FJsonObject>();
		Field->SetStringField(TEXT("name"), Prop->GetName());
		Field->SetStringField(TEXT("type"), Prop->GetCPPType());
		Field->SetStringField(TEXT("category"), Prop->GetMetaData(TEXT("Category")));

		if (DefaultInstance)
		{
			FString DefaultStr;
			Prop->ExportTextItem_Direct(DefaultStr, Prop->ContainerPtrToValuePtr<void>(DefaultInstance), nullptr, nullptr, PPF_None);
			Field->SetStringField(TEXT("default_value"), DefaultStr);
		}

		Fields.Add(MakeShared<FJsonValueObject>(Field));

		FIndexedVariable Var;
		Var.AssetId = AssetId;
		Var.VarName = Prop->GetName();
		Var.VarType = Prop->GetCPPType();
		Var.Category = Prop->GetMetaData(TEXT("Category"));
		if (DefaultInstance)
		{
			Prop->ExportTextItem_Direct(Var.DefaultValue, Prop->ContainerPtrToValuePtr<void>(DefaultInstance), nullptr, nullptr, PPF_None);
		}
		DB.InsertVariable(Var);
	}

	Props->SetNumberField(TEXT("field_count"), FieldCount);
	Props->SetArrayField(TEXT("fields"), Fields);

	FString PropsStr;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PropsStr);
	FJsonSerializer::Serialize(Props, *Writer, true);

	FIndexedNode StructNode;
	StructNode.AssetId = AssetId;
	StructNode.NodeName = UDS->GetName();
	StructNode.NodeClass = TEXT("UserDefinedStruct");
	StructNode.NodeType = TEXT("Struct");
	StructNode.Properties = PropsStr;
	DB.InsertNode(StructNode);

	return true;
}
