#include "MonolithMaterialActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithParamSchema.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionMaterialAttributeLayers.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionMaterialLayer.h"
#include "Materials/MaterialFunctionMaterialLayerBlend.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "MaterialExpressionIO.h"
#include "EditorAssetLibrary.h"
#include "MaterialEditingLibrary.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "ObjectTools.h"
#include "ImageUtils.h"

// ============================================================================
// Registration
// ============================================================================

void FMonolithMaterialActions::RegisterActions(FMonolithToolRegistry& Registry)
{
	Registry.RegisterAction(TEXT("material"), TEXT("get_all_expressions"),
		TEXT("Get all expression nodes in a base material"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::GetAllExpressions),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("get_expression_details"),
		TEXT("Get full property reflection, inputs, and outputs for a single expression"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::GetExpressionDetails),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Required(TEXT("expression_name"), TEXT("string"), TEXT("Name of the expression node"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("get_full_connection_graph"),
		TEXT("Get the complete connection graph (all wires) of a material"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::GetFullConnectionGraph),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("disconnect_expression"),
		TEXT("Disconnect inputs or outputs on a named expression"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::DisconnectExpression),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Required(TEXT("expression_name"), TEXT("string"), TEXT("Name of the expression to disconnect"))
			.Optional(TEXT("input_name"), TEXT("string"), TEXT("Specific input to disconnect"))
			.Optional(TEXT("disconnect_outputs"), TEXT("bool"), TEXT("Also disconnect outputs"), TEXT("false"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("build_material_graph"),
		TEXT("Build entire material graph from JSON spec in a single undo transaction"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::BuildMaterialGraph),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Required(TEXT("graph_spec"), TEXT("object"), TEXT("JSON specification of the material graph"))
			.Optional(TEXT("clear_existing"), TEXT("bool"), TEXT("Clear existing expressions before building"), TEXT("false"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("begin_transaction"),
		TEXT("Begin a named undo transaction for batching edits"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::BeginTransaction),
		FParamSchemaBuilder()
			.Required(TEXT("transaction_name"), TEXT("string"), TEXT("Name for the undo transaction"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("end_transaction"),
		TEXT("End the current undo transaction"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::EndTransaction),
		MakeShared<FJsonObject>());

	Registry.RegisterAction(TEXT("material"), TEXT("export_material_graph"),
		TEXT("Export complete material graph to JSON (round-trippable with build_material_graph)"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::ExportMaterialGraph),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Optional(TEXT("include_properties"), TEXT("bool"), TEXT("Include full property data"), TEXT("true"))
			.Optional(TEXT("include_positions"), TEXT("bool"), TEXT("Include node positions"), TEXT("true"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("import_material_graph"),
		TEXT("Import material graph from JSON string. Mode: overwrite or merge"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::ImportMaterialGraph),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Required(TEXT("graph_json"), TEXT("string"), TEXT("JSON string of the material graph"))
			.Optional(TEXT("mode"), TEXT("string"), TEXT("Import mode: overwrite or merge"), TEXT("overwrite"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("validate_material"),
		TEXT("Validate material graph health and optionally auto-fix issues"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::ValidateMaterial),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Optional(TEXT("fix_issues"), TEXT("bool"), TEXT("Auto-fix detected issues"), TEXT("false"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("render_preview"),
		TEXT("Render material preview to PNG file"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::RenderPreview),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Optional(TEXT("resolution"), TEXT("integer"), TEXT("Preview resolution in pixels"), TEXT("256"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("get_thumbnail"),
		TEXT("Get material thumbnail as base64-encoded PNG"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::GetThumbnail),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Optional(TEXT("resolution"), TEXT("integer"), TEXT("Thumbnail resolution"), TEXT("256"))
			.Optional(TEXT("save_to_file"), TEXT("string"), TEXT("Optional file path to save PNG to disk"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("create_custom_hlsl_node"),
		TEXT("Create a Custom HLSL expression node with inputs, outputs, and code"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::CreateCustomHLSLNode),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Required(TEXT("code"), TEXT("string"), TEXT("HLSL code for the custom node"))
			.Optional(TEXT("description"), TEXT("string"), TEXT("Node description"))
			.Optional(TEXT("output_type"), TEXT("string"), TEXT("Output type (float, float2, float3, float4)"))
			.Optional(TEXT("pos_x"), TEXT("integer"), TEXT("Node X position in graph"))
			.Optional(TEXT("pos_y"), TEXT("integer"), TEXT("Node Y position in graph"))
			.Optional(TEXT("inputs"), TEXT("array"), TEXT("Array of input pin definitions"))
			.Optional(TEXT("additional_outputs"), TEXT("array"), TEXT("Array of additional output pin definitions"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("get_layer_info"),
		TEXT("Get Material Layer or Material Layer Blend info"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::GetLayerInfo),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material Layer or Layer Blend asset path"))
			.Build());
}

// ============================================================================
// Helpers
// ============================================================================

UMaterial* FMonolithMaterialActions::LoadBaseMaterial(const FString& AssetPath)
{
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return nullptr;
	}
	return Cast<UMaterial>(LoadedAsset);
}

TSharedPtr<FJsonObject> FMonolithMaterialActions::SerializeExpression(const UMaterialExpression* Expression)
{
	auto ExprJson = MakeShared<FJsonObject>();

	ExprJson->SetStringField(TEXT("name"), Expression->GetName());
	ExprJson->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
	ExprJson->SetNumberField(TEXT("pos_x"), Expression->MaterialExpressionEditorX);
	ExprJson->SetNumberField(TEXT("pos_y"), Expression->MaterialExpressionEditorY);

	if (const auto* TexSampleParam = Cast<UMaterialExpressionTextureSampleParameter>(Expression))
	{
		ExprJson->SetStringField(TEXT("parameter_name"), TexSampleParam->ParameterName.ToString());
		if (TexSampleParam->Texture)
		{
			ExprJson->SetStringField(TEXT("texture"), TexSampleParam->Texture->GetPathName());
		}
	}
	else if (const auto* Param = Cast<UMaterialExpressionParameter>(Expression))
	{
		ExprJson->SetStringField(TEXT("parameter_name"), Param->ParameterName.ToString());
	}
	else if (const auto* TexBase = Cast<UMaterialExpressionTextureBase>(Expression))
	{
		if (TexBase->Texture)
		{
			ExprJson->SetStringField(TEXT("texture"), TexBase->Texture->GetPathName());
		}
	}

	if (const auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
	{
		FString CodePreview = Custom->Code.Left(100);
		ExprJson->SetStringField(TEXT("code"), CodePreview);
	}

	if (const auto* Comment = Cast<UMaterialExpressionComment>(Expression))
	{
		ExprJson->SetStringField(TEXT("text"), Comment->Text);
	}

	if (const auto* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
	{
		if (FuncCall->MaterialFunction)
		{
			ExprJson->SetStringField(TEXT("function"), FuncCall->MaterialFunction->GetPathName());
		}
	}

	return ExprJson;
}

/** Map string property name to EMaterialProperty. */
static EMaterialProperty ParseMaterialProperty(const FString& PropName)
{
	static const TMap<FString, EMaterialProperty> Map = {
		{ TEXT("BaseColor"),            MP_BaseColor },
		{ TEXT("Metallic"),             MP_Metallic },
		{ TEXT("Specular"),             MP_Specular },
		{ TEXT("Roughness"),            MP_Roughness },
		{ TEXT("Anisotropy"),           MP_Anisotropy },
		{ TEXT("EmissiveColor"),        MP_EmissiveColor },
		{ TEXT("Opacity"),              MP_Opacity },
		{ TEXT("OpacityMask"),          MP_OpacityMask },
		{ TEXT("Normal"),               MP_Normal },
		{ TEXT("WorldPositionOffset"),  MP_WorldPositionOffset },
		{ TEXT("SubsurfaceColor"),      MP_SubsurfaceColor },
		{ TEXT("AmbientOcclusion"),     MP_AmbientOcclusion },
		{ TEXT("Refraction"),           MP_Refraction },
		{ TEXT("PixelDepthOffset"),     MP_PixelDepthOffset },
		{ TEXT("ShadingModel"),         MP_ShadingModel },
	};

	const EMaterialProperty* Found = Map.Find(PropName);
	return Found ? *Found : MP_MAX;
}

/** Map string to ECustomMaterialOutputType. */
static ECustomMaterialOutputType ParseCustomOutputType(const FString& TypeName)
{
	if (TypeName == TEXT("CMOT_Float1") || TypeName == TEXT("Float1")) return CMOT_Float1;
	if (TypeName == TEXT("CMOT_Float2") || TypeName == TEXT("Float2")) return CMOT_Float2;
	if (TypeName == TEXT("CMOT_Float3") || TypeName == TEXT("Float3")) return CMOT_Float3;
	if (TypeName == TEXT("CMOT_Float4") || TypeName == TEXT("Float4")) return CMOT_Float4;
	return CMOT_Float1;
}

/** Map ECustomMaterialOutputType to string. */
static FString CustomOutputTypeToString(ECustomMaterialOutputType Type)
{
	switch (Type)
	{
	case CMOT_Float1: return TEXT("Float1");
	case CMOT_Float2: return TEXT("Float2");
	case CMOT_Float3: return TEXT("Float3");
	case CMOT_Float4: return TEXT("Float4");
	default: return TEXT("Float1");
	}
}

/** Map EMaterialProperty to string name. */
static FString MaterialPropertyToString(EMaterialProperty Prop)
{
	switch (Prop)
	{
	case MP_BaseColor:            return TEXT("BaseColor");
	case MP_Metallic:             return TEXT("Metallic");
	case MP_Specular:             return TEXT("Specular");
	case MP_Roughness:            return TEXT("Roughness");
	case MP_Anisotropy:           return TEXT("Anisotropy");
	case MP_EmissiveColor:        return TEXT("EmissiveColor");
	case MP_Opacity:              return TEXT("Opacity");
	case MP_OpacityMask:          return TEXT("OpacityMask");
	case MP_Normal:               return TEXT("Normal");
	case MP_WorldPositionOffset:  return TEXT("WorldPositionOffset");
	case MP_SubsurfaceColor:      return TEXT("SubsurfaceColor");
	case MP_AmbientOcclusion:     return TEXT("AmbientOcclusion");
	case MP_Refraction:           return TEXT("Refraction");
	case MP_PixelDepthOffset:     return TEXT("PixelDepthOffset");
	case MP_ShadingModel:         return TEXT("ShadingModel");
	default:                      return TEXT("");
	}
}

/** All material properties for iteration */
static const EMaterialProperty AllMaterialProperties[] =
{
	MP_BaseColor, MP_Metallic, MP_Specular, MP_Roughness, MP_Anisotropy,
	MP_EmissiveColor, MP_Opacity, MP_OpacityMask, MP_Normal,
	MP_WorldPositionOffset, MP_SubsurfaceColor, MP_AmbientOcclusion,
	MP_Refraction, MP_PixelDepthOffset, MP_ShadingModel,
	MP_Tangent, MP_Displacement, MP_CustomData0, MP_CustomData1,
	MP_SurfaceThickness, MP_FrontMaterial, MP_MaterialAttributes,
};

/** Material output entries for connection graph */
struct FMaterialOutputEntry
{
	EMaterialProperty Property;
	const TCHAR* Name;
};

static const FMaterialOutputEntry MaterialOutputEntries[] =
{
	{ MP_BaseColor,              TEXT("BaseColor") },
	{ MP_Metallic,               TEXT("Metallic") },
	{ MP_Specular,               TEXT("Specular") },
	{ MP_Roughness,              TEXT("Roughness") },
	{ MP_Anisotropy,             TEXT("Anisotropy") },
	{ MP_EmissiveColor,          TEXT("EmissiveColor") },
	{ MP_Opacity,                TEXT("Opacity") },
	{ MP_OpacityMask,            TEXT("OpacityMask") },
	{ MP_Normal,                 TEXT("Normal") },
	{ MP_WorldPositionOffset,    TEXT("WorldPositionOffset") },
	{ MP_SubsurfaceColor,        TEXT("SubsurfaceColor") },
	{ MP_AmbientOcclusion,       TEXT("AmbientOcclusion") },
	{ MP_Refraction,             TEXT("Refraction") },
	{ MP_PixelDepthOffset,       TEXT("PixelDepthOffset") },
	{ MP_ShadingModel,           TEXT("ShadingModel") },
	{ MP_Tangent,                TEXT("Tangent") },
	{ MP_Displacement,           TEXT("Displacement") },
	{ MP_CustomData0,            TEXT("ClearCoat") },
	{ MP_CustomData1,            TEXT("ClearCoatRoughness") },
	{ MP_SurfaceThickness,       TEXT("SurfaceThickness") },
	{ MP_FrontMaterial,          TEXT("FrontMaterial") },
	{ MP_MaterialAttributes,     TEXT("MaterialAttributes") },
};

// ============================================================================
// Action: get_all_expressions
// Params: { "asset_path": "/Game/..." }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::GetAllExpressions(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Mat->GetExpressions();
	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (Expr)
		{
			ExpressionsArray.Add(MakeShared<FJsonValueObject>(SerializeExpression(Expr)));
		}
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetNumberField(TEXT("expression_count"), ExpressionsArray.Num());
	ResultJson->SetArrayField(TEXT("expressions"), ExpressionsArray);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: get_expression_details
// Params: { "asset_path": "...", "expression_name": "..." }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::GetExpressionDetails(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExpressionName = Params->GetStringField(TEXT("expression_name"));

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	UMaterialExpression* FoundExpr = nullptr;
	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Mat->GetExpressions();
	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (Expr && Expr->GetName() == ExpressionName)
		{
			FoundExpr = Expr;
			break;
		}
	}

	if (!FoundExpr)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Expression '%s' not found in material '%s'"), *ExpressionName, *AssetPath));
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetStringField(TEXT("expression_name"), ExpressionName);
	ResultJson->SetStringField(TEXT("class"), FoundExpr->GetClass()->GetName());

	// Serialize ALL UProperties via reflection
	auto PropsJson = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> PropIt(FoundExpr->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
		{
			continue;
		}
		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(FoundExpr);
		Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
		if (!ValueStr.IsEmpty())
		{
			PropsJson->SetStringField(Prop->GetName(), ValueStr);
		}
	}
	ResultJson->SetObjectField(TEXT("properties"), PropsJson);

	// List input pins
	TArray<TSharedPtr<FJsonValue>> InputsArray;
	for (int32 i = 0; ; ++i)
	{
		FExpressionInput* Input = FoundExpr->GetInput(i);
		if (!Input)
		{
			break;
		}
		auto InputJson = MakeShared<FJsonObject>();
		InputJson->SetStringField(TEXT("name"), FoundExpr->GetInputName(i).ToString());
		InputJson->SetBoolField(TEXT("connected"), Input->Expression != nullptr);
		if (Input->Expression)
		{
			InputJson->SetStringField(TEXT("connected_to"), Input->Expression->GetName());
			InputJson->SetNumberField(TEXT("output_index"), Input->OutputIndex);
		}
		InputsArray.Add(MakeShared<FJsonValueObject>(InputJson));
	}
	ResultJson->SetArrayField(TEXT("inputs"), InputsArray);

	// List output pins
	TArray<TSharedPtr<FJsonValue>> OutputsArray;
	const TArray<FExpressionOutput>& Outputs = FoundExpr->Outputs;
	for (int32 i = 0; i < Outputs.Num(); ++i)
	{
		auto OutputJson = MakeShared<FJsonObject>();
		OutputJson->SetStringField(TEXT("name"), Outputs[i].OutputName.ToString());
		OutputJson->SetNumberField(TEXT("index"), i);
		OutputsArray.Add(MakeShared<FJsonValueObject>(OutputJson));
	}
	ResultJson->SetArrayField(TEXT("outputs"), OutputsArray);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: get_full_connection_graph
// Params: { "asset_path": "..." }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::GetFullConnectionGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Mat->GetExpressions();

	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (!Expr)
		{
			continue;
		}

		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = Expr->GetInput(i);
			if (!Input)
			{
				break;
			}
			if (!Input->Expression)
			{
				continue;
			}

			auto ConnJson = MakeShared<FJsonObject>();
			ConnJson->SetStringField(TEXT("from"), Input->Expression->GetName());
			ConnJson->SetNumberField(TEXT("from_output_index"), Input->OutputIndex);

			FString FromOutputName;
			const TArray<FExpressionOutput>& SourceOutputs = Input->Expression->Outputs;
			if (SourceOutputs.IsValidIndex(Input->OutputIndex))
			{
				FromOutputName = SourceOutputs[Input->OutputIndex].OutputName.ToString();
			}
			ConnJson->SetStringField(TEXT("from_output"), FromOutputName);
			ConnJson->SetStringField(TEXT("to"), Expr->GetName());
			ConnJson->SetStringField(TEXT("to_input"), Expr->GetInputName(i).ToString());

			ConnectionsArray.Add(MakeShared<FJsonValueObject>(ConnJson));
		}
	}

	// Material output connections
	TArray<TSharedPtr<FJsonValue>> MaterialOutputsArray;
	for (const FMaterialOutputEntry& Entry : MaterialOutputEntries)
	{
		FExpressionInput* Input = Mat->GetExpressionInputForProperty(Entry.Property);
		if (Input && Input->Expression)
		{
			auto OutJson = MakeShared<FJsonObject>();
			OutJson->SetStringField(TEXT("property"), Entry.Name);
			OutJson->SetStringField(TEXT("expression"), Input->Expression->GetName());
			OutJson->SetNumberField(TEXT("output_index"), Input->OutputIndex);
			MaterialOutputsArray.Add(MakeShared<FJsonValueObject>(OutJson));
		}
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetArrayField(TEXT("connections"), ConnectionsArray);
	ResultJson->SetArrayField(TEXT("material_outputs"), MaterialOutputsArray);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: disconnect_expression
// Params: { "asset_path": "...", "expression_name": "...", "input_name": "", "disconnect_outputs": false }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::DisconnectExpression(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExpressionName = Params->GetStringField(TEXT("expression_name"));
	FString InputName = Params->HasField(TEXT("input_name")) ? Params->GetStringField(TEXT("input_name")) : TEXT("");
	bool bDisconnectOutputs = Params->HasField(TEXT("disconnect_outputs")) ? Params->GetBoolField(TEXT("disconnect_outputs")) : false;

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	UMaterialExpression* TargetExpr = nullptr;
	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Mat->GetExpressions();
	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (Expr && Expr->GetName() == ExpressionName)
		{
			TargetExpr = Expr;
			break;
		}
	}

	if (!TargetExpr)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Expression '%s' not found in material '%s'"), *ExpressionName, *AssetPath));
	}

	Mat->Modify();

	TArray<TSharedPtr<FJsonValue>> DisconnectedArray;
	int32 DisconnectCount = 0;

	if (!bDisconnectOutputs)
	{
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = TargetExpr->GetInput(i);
			if (!Input)
			{
				break;
			}

			FString PinName = TargetExpr->GetInputName(i).ToString();
			if (InputName.IsEmpty() || PinName == InputName)
			{
				if (Input->Expression != nullptr)
				{
					auto DisconnJson = MakeShared<FJsonObject>();
					DisconnJson->SetStringField(TEXT("pin"), PinName);
					DisconnJson->SetStringField(TEXT("was_connected_to"), Input->Expression->GetName());
					DisconnectedArray.Add(MakeShared<FJsonValueObject>(DisconnJson));

					Input->Expression = nullptr;
					Input->OutputIndex = 0;
					DisconnectCount++;
				}
			}
		}
	}
	else
	{
		for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
		{
			if (!Expr)
			{
				continue;
			}
			for (int32 i = 0; ; ++i)
			{
				FExpressionInput* Input = Expr->GetInput(i);
				if (!Input)
				{
					break;
				}
				if (Input->Expression == TargetExpr)
				{
					auto DisconnJson = MakeShared<FJsonObject>();
					DisconnJson->SetStringField(TEXT("expression"), Expr->GetName());
					DisconnJson->SetStringField(TEXT("pin"), Expr->GetInputName(i).ToString());
					DisconnectedArray.Add(MakeShared<FJsonValueObject>(DisconnJson));

					Input->Expression = nullptr;
					Input->OutputIndex = 0;
					DisconnectCount++;
				}
			}
		}
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetArrayField(TEXT("disconnected"), DisconnectedArray);
	ResultJson->SetNumberField(TEXT("count"), DisconnectCount);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: begin_transaction
// Params: { "transaction_name": "..." }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::BeginTransaction(const TSharedPtr<FJsonObject>& Params)
{
	FString TransactionName = Params->GetStringField(TEXT("transaction_name"));
	GEditor->BeginTransaction(FText::FromString(TransactionName));

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("transaction"), TransactionName);
	ResultJson->SetStringField(TEXT("status"), TEXT("begun"));
	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: end_transaction
// Params: {}
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::EndTransaction(const TSharedPtr<FJsonObject>& Params)
{
	GEditor->EndTransaction();

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("ended"));
	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: build_material_graph
// Params: { "asset_path": "...", "graph_spec": { nodes, custom_hlsl_nodes, connections, outputs }, "clear_existing": false }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::BuildMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	bool bClearExisting = Params->HasField(TEXT("clear_existing")) ? Params->GetBoolField(TEXT("clear_existing")) : false;

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	// graph_spec can be passed as a nested object or as a JSON string
	TSharedPtr<FJsonObject> Spec;
	if (Params->HasTypedField<EJson::Object>(TEXT("graph_spec")))
	{
		Spec = Params->GetObjectField(TEXT("graph_spec"));
	}
	else if (Params->HasTypedField<EJson::String>(TEXT("graph_spec")))
	{
		FString GraphSpecJson = Params->GetStringField(TEXT("graph_spec"));
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GraphSpecJson);
		if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
		{
			return FMonolithActionResult::Error(TEXT("Failed to parse graph_spec JSON string"));
		}
	}
	else
	{
		return FMonolithActionResult::Error(TEXT("Missing 'graph_spec' parameter"));
	}

	TArray<TSharedPtr<FJsonValue>> ErrorsArray;
	int32 NodesCreated = 0;
	int32 ConnectionsMade = 0;

	GEditor->BeginTransaction(FText::FromString(TEXT("BuildMaterialGraph")));
	Mat->Modify();

	if (bClearExisting)
	{
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Mat);
	}

	TMap<FString, UMaterialExpression*> IdToExpr;

	// Phase 1 — Standard nodes
	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	if (Spec->TryGetArrayField(TEXT("nodes"), NodesArray))
	{
		for (const TSharedPtr<FJsonValue>& NodeVal : *NodesArray)
		{
			const TSharedPtr<FJsonObject>* NodeObjPtr = nullptr;
			if (!NodeVal || !NodeVal->TryGetObject(NodeObjPtr) || !NodeObjPtr)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& NodeObj = *NodeObjPtr;

			FString Id = NodeObj->GetStringField(TEXT("id"));
			FString ShortClass = NodeObj->GetStringField(TEXT("class"));

			FString FullClassName = ShortClass;
			if (!ShortClass.StartsWith(TEXT("MaterialExpression")))
			{
				FullClassName = FString::Printf(TEXT("MaterialExpression%s"), *ShortClass);
			}

			UClass* ExprClass = FindObject<UClass>(static_cast<UObject*>(nullptr), *FullClassName);
			if (!ExprClass)
			{
				auto ErrJson = MakeShared<FJsonObject>();
				ErrJson->SetStringField(TEXT("node_id"), Id);
				ErrJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Class '%s' not found"), *FullClassName));
				ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrJson));
				continue;
			}

			int32 PosX = 0, PosY = 0;
			const TArray<TSharedPtr<FJsonValue>>* PosArray = nullptr;
			if (NodeObj->TryGetArrayField(TEXT("pos"), PosArray) && PosArray->Num() >= 2)
			{
				PosX = static_cast<int32>((*PosArray)[0]->AsNumber());
				PosY = static_cast<int32>((*PosArray)[1]->AsNumber());
			}

			UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass, PosX, PosY);
			if (!NewExpr)
			{
				auto ErrJson = MakeShared<FJsonObject>();
				ErrJson->SetStringField(TEXT("node_id"), Id);
				ErrJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create expression of class '%s'"), *FullClassName));
				ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrJson));
				continue;
			}

			// Set properties
			const TSharedPtr<FJsonObject>* PropsObjPtr = nullptr;
			if (NodeObj->TryGetObjectField(TEXT("props"), PropsObjPtr) && PropsObjPtr)
			{
				const TSharedPtr<FJsonObject>& PropsObj = *PropsObjPtr;
				for (const auto& Pair : PropsObj->Values)
				{
					FProperty* Prop = NewExpr->GetClass()->FindPropertyByName(*Pair.Key);
					if (!Prop)
					{
						auto ErrJson = MakeShared<FJsonObject>();
						ErrJson->SetStringField(TEXT("node_id"), Id);
						ErrJson->SetStringField(TEXT("warning"), FString::Printf(TEXT("Property '%s' not found on '%s'"), *Pair.Key, *FullClassName));
						ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrJson));
						continue;
					}

					FString ValueStr = Pair.Value->AsString();
					void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(NewExpr);

					if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
					{
						FloatProp->SetPropertyValue(ValuePtr, static_cast<float>(Pair.Value->AsNumber()));
					}
					else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
					{
						DoubleProp->SetPropertyValue(ValuePtr, Pair.Value->AsNumber());
					}
					else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
					{
						IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(Pair.Value->AsNumber()));
					}
					else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
					{
						BoolProp->SetPropertyValue(ValuePtr, Pair.Value->AsBool());
					}
					else
					{
						Prop->ImportText_Direct(*ValueStr, ValuePtr, NewExpr, PPF_None);
					}
				}
			}

			IdToExpr.Add(Id, NewExpr);
			NodesCreated++;
		}
	}

	// Phase 2 — Custom HLSL nodes
	const TArray<TSharedPtr<FJsonValue>>* CustomArray = nullptr;
	if (Spec->TryGetArrayField(TEXT("custom_hlsl_nodes"), CustomArray))
	{
		for (const TSharedPtr<FJsonValue>& CustomVal : *CustomArray)
		{
			const TSharedPtr<FJsonObject>* CustomObjPtr = nullptr;
			if (!CustomVal || !CustomVal->TryGetObject(CustomObjPtr) || !CustomObjPtr)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& CustomObj = *CustomObjPtr;

			FString Id = CustomObj->GetStringField(TEXT("id"));

			int32 PosX = 0, PosY = 0;
			const TArray<TSharedPtr<FJsonValue>>* PosArray = nullptr;
			if (CustomObj->TryGetArrayField(TEXT("pos"), PosArray) && PosArray->Num() >= 2)
			{
				PosX = static_cast<int32>((*PosArray)[0]->AsNumber());
				PosY = static_cast<int32>((*PosArray)[1]->AsNumber());
			}

			UMaterialExpression* BaseExpr = UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionCustom::StaticClass(), PosX, PosY);

			UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(BaseExpr);
			if (!CustomExpr)
			{
				auto ErrJson = MakeShared<FJsonObject>();
				ErrJson->SetStringField(TEXT("node_id"), Id);
				ErrJson->SetStringField(TEXT("error"), TEXT("Failed to create Custom HLSL expression"));
				ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrJson));
				continue;
			}

			CustomExpr->Code = CustomObj->GetStringField(TEXT("code"));
			if (CustomObj->HasField(TEXT("description")))
			{
				CustomExpr->Description = CustomObj->GetStringField(TEXT("description"));
			}
			if (CustomObj->HasField(TEXT("output_type")))
			{
				CustomExpr->OutputType = ParseCustomOutputType(CustomObj->GetStringField(TEXT("output_type")));
			}

			const TArray<TSharedPtr<FJsonValue>>* InputsArray = nullptr;
			if (CustomObj->TryGetArrayField(TEXT("inputs"), InputsArray))
			{
				CustomExpr->Inputs.Empty();
				for (const TSharedPtr<FJsonValue>& InputVal : *InputsArray)
				{
					const TSharedPtr<FJsonObject>* InputObjPtr = nullptr;
					if (InputVal && InputVal->TryGetObject(InputObjPtr) && InputObjPtr)
					{
						FCustomInput NewInput;
						NewInput.InputName = *(*InputObjPtr)->GetStringField(TEXT("name"));
						CustomExpr->Inputs.Add(NewInput);
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* AddOutputsArray = nullptr;
			if (CustomObj->TryGetArrayField(TEXT("additional_outputs"), AddOutputsArray))
			{
				CustomExpr->AdditionalOutputs.Empty();
				for (const TSharedPtr<FJsonValue>& OutVal : *AddOutputsArray)
				{
					const TSharedPtr<FJsonObject>* OutObjPtr = nullptr;
					if (OutVal && OutVal->TryGetObject(OutObjPtr) && OutObjPtr)
					{
						FCustomOutput NewOutput;
						NewOutput.OutputName = *(*OutObjPtr)->GetStringField(TEXT("name"));
						if ((*OutObjPtr)->HasField(TEXT("type")))
						{
							NewOutput.OutputType = ParseCustomOutputType((*OutObjPtr)->GetStringField(TEXT("type")));
						}
						CustomExpr->AdditionalOutputs.Add(NewOutput);
					}
				}
			}

			CustomExpr->RebuildOutputs();
			IdToExpr.Add(Id, CustomExpr);
			NodesCreated++;
		}
	}

	// Phase 3 — Wire connections between expressions
	const TArray<TSharedPtr<FJsonValue>>* ConnsArray = nullptr;
	if (Spec->TryGetArrayField(TEXT("connections"), ConnsArray))
	{
		for (const TSharedPtr<FJsonValue>& ConnVal : *ConnsArray)
		{
			const TSharedPtr<FJsonObject>* ConnObjPtr = nullptr;
			if (!ConnVal || !ConnVal->TryGetObject(ConnObjPtr) || !ConnObjPtr)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& ConnObj = *ConnObjPtr;

			FString FromId = ConnObj->GetStringField(TEXT("from"));
			FString ToId = ConnObj->GetStringField(TEXT("to"));
			FString FromPin = ConnObj->HasField(TEXT("from_pin")) ? ConnObj->GetStringField(TEXT("from_pin")) : TEXT("");
			FString ToPin = ConnObj->HasField(TEXT("to_pin")) ? ConnObj->GetStringField(TEXT("to_pin")) : TEXT("");

			UMaterialExpression** FromPtr = IdToExpr.Find(FromId);
			UMaterialExpression** ToPtr = IdToExpr.Find(ToId);

			if (!FromPtr || !*FromPtr)
			{
				auto ErrJson = MakeShared<FJsonObject>();
				ErrJson->SetStringField(TEXT("connection"), FString::Printf(TEXT("%s -> %s"), *FromId, *ToId));
				ErrJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Source node '%s' not found"), *FromId));
				ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrJson));
				continue;
			}
			if (!ToPtr || !*ToPtr)
			{
				auto ErrJson = MakeShared<FJsonObject>();
				ErrJson->SetStringField(TEXT("connection"), FString::Printf(TEXT("%s -> %s"), *FromId, *ToId));
				ErrJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Target node '%s' not found"), *ToId));
				ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrJson));
				continue;
			}

			bool bConnected = UMaterialEditingLibrary::ConnectMaterialExpressions(*FromPtr, FromPin, *ToPtr, ToPin);
			if (bConnected)
			{
				ConnectionsMade++;
			}
			else
			{
				auto ErrJson = MakeShared<FJsonObject>();
				ErrJson->SetStringField(TEXT("connection"), FString::Printf(TEXT("%s.%s -> %s.%s"), *FromId, *FromPin, *ToId, *ToPin));
				ErrJson->SetStringField(TEXT("error"), TEXT("ConnectMaterialExpressions returned false"));
				ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrJson));
			}
		}
	}

	// Phase 4 — Wire material output properties
	const TArray<TSharedPtr<FJsonValue>>* OutputsArray = nullptr;
	if (Spec->TryGetArrayField(TEXT("outputs"), OutputsArray))
	{
		for (const TSharedPtr<FJsonValue>& OutVal : *OutputsArray)
		{
			const TSharedPtr<FJsonObject>* OutObjPtr = nullptr;
			if (!OutVal || !OutVal->TryGetObject(OutObjPtr) || !OutObjPtr)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& OutObj = *OutObjPtr;

			FString FromId = OutObj->GetStringField(TEXT("from"));
			FString FromPin = OutObj->HasField(TEXT("from_pin")) ? OutObj->GetStringField(TEXT("from_pin")) : TEXT("");
			FString ToProp = OutObj->GetStringField(TEXT("to_property"));

			UMaterialExpression** FromPtr = IdToExpr.Find(FromId);
			if (!FromPtr || !*FromPtr)
			{
				auto ErrJson = MakeShared<FJsonObject>();
				ErrJson->SetStringField(TEXT("output"), ToProp);
				ErrJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Source node '%s' not found"), *FromId));
				ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrJson));
				continue;
			}

			EMaterialProperty MatProp = ParseMaterialProperty(ToProp);
			if (MatProp == MP_MAX)
			{
				auto ErrJson = MakeShared<FJsonObject>();
				ErrJson->SetStringField(TEXT("output"), ToProp);
				ErrJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown material property '%s'"), *ToProp));
				ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrJson));
				continue;
			}

			bool bConnected = UMaterialEditingLibrary::ConnectMaterialProperty(*FromPtr, FromPin, MatProp);
			if (bConnected)
			{
				ConnectionsMade++;
			}
			else
			{
				auto ErrJson = MakeShared<FJsonObject>();
				ErrJson->SetStringField(TEXT("output"), ToProp);
				ErrJson->SetStringField(TEXT("error"), TEXT("ConnectMaterialProperty returned false"));
				ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrJson));
			}
		}
	}

	GEditor->EndTransaction();

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetBoolField(TEXT("has_errors"), ErrorsArray.Num() > 0);
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetNumberField(TEXT("nodes_created"), NodesCreated);
	ResultJson->SetNumberField(TEXT("connections_made"), ConnectionsMade);

	auto IdMapJson = MakeShared<FJsonObject>();
	for (const auto& Pair : IdToExpr)
	{
		if (Pair.Value)
		{
			IdMapJson->SetStringField(Pair.Key, Pair.Value->GetName());
		}
	}
	ResultJson->SetObjectField(TEXT("id_to_name"), IdMapJson);
	ResultJson->SetArrayField(TEXT("errors"), ErrorsArray);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: export_material_graph
// Params: { "asset_path": "...", "include_properties": true, "include_positions": true }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::ExportMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	bool bIncludeProperties = true;
	bool bIncludePositions = true;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_properties"), bIncludeProperties);
		Params->TryGetBoolField(TEXT("include_positions"), bIncludePositions);
	}

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Mat->GetExpressions();

	TMap<UMaterialExpression*, FString> ExprToId;
	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (Expr)
		{
			ExprToId.Add(Expr, Expr->GetName());
		}
	}

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> CustomHlslArray;

	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (!Expr)
		{
			continue;
		}

		const UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expr);

		if (CustomExpr)
		{
			auto NodeJson = MakeShared<FJsonObject>();
			NodeJson->SetStringField(TEXT("id"), Expr->GetName());

			if (bIncludePositions)
			{
				TArray<TSharedPtr<FJsonValue>> PosArr;
				PosArr.Add(MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorX));
				PosArr.Add(MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorY));
				NodeJson->SetArrayField(TEXT("pos"), PosArr);
			}

			NodeJson->SetStringField(TEXT("code"), CustomExpr->Code);
			NodeJson->SetStringField(TEXT("description"), CustomExpr->Description);
			NodeJson->SetStringField(TEXT("output_type"), CustomOutputTypeToString(CustomExpr->OutputType));

			TArray<TSharedPtr<FJsonValue>> InputsArr;
			for (const FCustomInput& CustInput : CustomExpr->Inputs)
			{
				auto InputJson = MakeShared<FJsonObject>();
				InputJson->SetStringField(TEXT("name"), CustInput.InputName.ToString());
				InputsArr.Add(MakeShared<FJsonValueObject>(InputJson));
			}
			NodeJson->SetArrayField(TEXT("inputs"), InputsArr);

			TArray<TSharedPtr<FJsonValue>> AddOutputsArr;
			for (const FCustomOutput& AddOut : CustomExpr->AdditionalOutputs)
			{
				auto OutJson = MakeShared<FJsonObject>();
				OutJson->SetStringField(TEXT("name"), AddOut.OutputName.ToString());
				OutJson->SetStringField(TEXT("type"), CustomOutputTypeToString(AddOut.OutputType));
				AddOutputsArr.Add(MakeShared<FJsonValueObject>(OutJson));
			}
			NodeJson->SetArrayField(TEXT("additional_outputs"), AddOutputsArr);

			CustomHlslArray.Add(MakeShared<FJsonValueObject>(NodeJson));
		}
		else
		{
			auto NodeJson = MakeShared<FJsonObject>();
			NodeJson->SetStringField(TEXT("id"), Expr->GetName());

			FString ClassName = Expr->GetClass()->GetName();
			if (ClassName.StartsWith(TEXT("MaterialExpression")))
			{
				ClassName = ClassName.Mid(18);
			}
			NodeJson->SetStringField(TEXT("class"), ClassName);

			if (bIncludePositions)
			{
				TArray<TSharedPtr<FJsonValue>> PosArr;
				PosArr.Add(MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorX));
				PosArr.Add(MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorY));
				NodeJson->SetArrayField(TEXT("pos"), PosArr);
			}

			if (bIncludeProperties)
			{
				auto PropsJson = MakeShared<FJsonObject>();
				for (TFieldIterator<FProperty> PropIt(Expr->GetClass()); PropIt; ++PropIt)
				{
					FProperty* Prop = *PropIt;
					if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
					{
						continue;
					}
					if (Prop->GetOwnerClass() == UMaterialExpression::StaticClass())
					{
						continue;
					}
					FString ValueStr;
					const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expr);
					Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
					if (!ValueStr.IsEmpty())
					{
						PropsJson->SetStringField(Prop->GetName(), ValueStr);
					}
				}
				NodeJson->SetObjectField(TEXT("props"), PropsJson);
			}

			NodesArray.Add(MakeShared<FJsonValueObject>(NodeJson));
		}
	}

	// Build connections
	TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (!Expr)
		{
			continue;
		}
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* ExprInput = Expr->GetInput(i);
			if (!ExprInput)
			{
				break;
			}
			if (!ExprInput->Expression)
			{
				continue;
			}

			auto ConnJson = MakeShared<FJsonObject>();
			FString* FromId = ExprToId.Find(ExprInput->Expression);
			ConnJson->SetStringField(TEXT("from"), FromId ? *FromId : ExprInput->Expression->GetName());

			FString FromPin;
			const TArray<FExpressionOutput>& SourceOutputs = ExprInput->Expression->Outputs;
			if (SourceOutputs.IsValidIndex(ExprInput->OutputIndex))
			{
				FromPin = SourceOutputs[ExprInput->OutputIndex].OutputName.ToString();
			}
			ConnJson->SetStringField(TEXT("from_pin"), FromPin);
			ConnJson->SetStringField(TEXT("to"), Expr->GetName());
			ConnJson->SetStringField(TEXT("to_pin"), Expr->GetInputName(i).ToString());

			ConnectionsArray.Add(MakeShared<FJsonValueObject>(ConnJson));
		}
	}

	// Build outputs
	TArray<TSharedPtr<FJsonValue>> OutputsArray;
	for (EMaterialProperty MatProp : AllMaterialProperties)
	{
		FExpressionInput* PropInput = Mat->GetExpressionInputForProperty(MatProp);
		if (PropInput && PropInput->Expression)
		{
			auto OutJson = MakeShared<FJsonObject>();
			FString* FromId = ExprToId.Find(PropInput->Expression);
			OutJson->SetStringField(TEXT("from"), FromId ? *FromId : PropInput->Expression->GetName());

			FString FromPin;
			const TArray<FExpressionOutput>& SourceOutputs = PropInput->Expression->Outputs;
			if (SourceOutputs.IsValidIndex(PropInput->OutputIndex))
			{
				FromPin = SourceOutputs[PropInput->OutputIndex].OutputName.ToString();
			}
			OutJson->SetStringField(TEXT("from_pin"), FromPin);
			OutJson->SetStringField(TEXT("to_property"), MaterialPropertyToString(MatProp));
			OutputsArray.Add(MakeShared<FJsonValueObject>(OutJson));
		}
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetArrayField(TEXT("nodes"), NodesArray);
	ResultJson->SetArrayField(TEXT("custom_hlsl_nodes"), CustomHlslArray);
	ResultJson->SetArrayField(TEXT("connections"), ConnectionsArray);
	ResultJson->SetArrayField(TEXT("outputs"), OutputsArray);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: import_material_graph
// Params: { "asset_path": "...", "graph_json": "...", "mode": "overwrite"|"merge" }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::ImportMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString GraphJson = Params->GetStringField(TEXT("graph_json"));
	FString Mode = Params->HasField(TEXT("mode")) ? Params->GetStringField(TEXT("mode")) : TEXT("overwrite");

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	if (Mode == TEXT("overwrite"))
	{
		// Build a params object for BuildMaterialGraph with clear_existing=true
		auto BuildParams = MakeShared<FJsonObject>();
		BuildParams->SetStringField(TEXT("asset_path"), AssetPath);
		BuildParams->SetStringField(TEXT("graph_spec"), GraphJson);
		BuildParams->SetBoolField(TEXT("clear_existing"), true);
		return BuildMaterialGraph(BuildParams);
	}
	else if (Mode == TEXT("merge"))
	{
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(GraphJson);
		TSharedPtr<FJsonObject> Spec;
		if (!FJsonSerializer::Deserialize(JsonReader, Spec) || !Spec.IsValid())
		{
			return FMonolithActionResult::Error(TEXT("Failed to parse graph_json for merge"));
		}

		auto OffsetNodePositions = [](const TArray<TSharedPtr<FJsonValue>>* ArrayPtr)
		{
			if (!ArrayPtr) return;
			for (const TSharedPtr<FJsonValue>& NodeVal : *ArrayPtr)
			{
				const TSharedPtr<FJsonObject>* NodeObjPtr = nullptr;
				if (NodeVal && NodeVal->TryGetObject(NodeObjPtr) && NodeObjPtr)
				{
					const TArray<TSharedPtr<FJsonValue>>* PosArray = nullptr;
					if ((*NodeObjPtr)->TryGetArrayField(TEXT("pos"), PosArray) && PosArray->Num() >= 2)
					{
						double OrigX = (*PosArray)[0]->AsNumber();
						double OrigY = (*PosArray)[1]->AsNumber();
						TArray<TSharedPtr<FJsonValue>> NewPos;
						NewPos.Add(MakeShared<FJsonValueNumber>(OrigX + 500.0));
						NewPos.Add(MakeShared<FJsonValueNumber>(OrigY));
						(*NodeObjPtr)->SetArrayField(TEXT("pos"), NewPos);
					}
				}
			}
		};

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		Spec->TryGetArrayField(TEXT("nodes"), NodesArr);
		OffsetNodePositions(NodesArr);

		const TArray<TSharedPtr<FJsonValue>>* CustomArr = nullptr;
		Spec->TryGetArrayField(TEXT("custom_hlsl_nodes"), CustomArr);
		OffsetNodePositions(CustomArr);

		auto BuildParams = MakeShared<FJsonObject>();
		BuildParams->SetStringField(TEXT("asset_path"), AssetPath);
		BuildParams->SetObjectField(TEXT("graph_spec"), Spec);
		BuildParams->SetBoolField(TEXT("clear_existing"), false);
		return BuildMaterialGraph(BuildParams);
	}
	else
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Unknown import mode '%s'. Use 'overwrite' or 'merge'."), *Mode));
	}
}

// ============================================================================
// Action: validate_material
// Params: { "asset_path": "...", "fix_issues": false }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::ValidateMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	bool bFixIssues = Params->HasField(TEXT("fix_issues")) ? Params->GetBoolField(TEXT("fix_issues")) : false;

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Mat->GetExpressions();
	TArray<TSharedPtr<FJsonValue>> IssuesArray;
	int32 FixedCount = 0;

	// BFS from material outputs to find reachable expressions
	TSet<UMaterialExpression*> ReachableSet;
	TArray<UMaterialExpression*> BfsQueue;

	for (EMaterialProperty Prop : AllMaterialProperties)
	{
		FExpressionInput* PropInput = Mat->GetExpressionInputForProperty(Prop);
		if (PropInput && PropInput->Expression)
		{
			if (!ReachableSet.Contains(PropInput->Expression))
			{
				ReachableSet.Add(PropInput->Expression);
				BfsQueue.Add(PropInput->Expression);
			}
		}
	}

	// Also seed from UMaterialExpressionCustomOutput subclasses — they are output terminals too
	for (UMaterialExpression* Expr : Expressions)
	{
		if (Expr && Expr->IsA<UMaterialExpressionCustomOutput>())
		{
			if (!ReachableSet.Contains(Expr))
			{
				ReachableSet.Add(Expr);
				BfsQueue.Add(Expr);
			}
		}
	}

	// Seed from UMaterialExpressionMaterialAttributeLayers — implicit output terminals for layer-blend materials
	for (UMaterialExpression* Expr : Expressions)
	{
		if (Expr && Expr->IsA<UMaterialExpressionMaterialAttributeLayers>())
		{
			if (!ReachableSet.Contains(Expr))
			{
				ReachableSet.Add(Expr);
				BfsQueue.Add(Expr);
			}
		}
	}

	while (BfsQueue.Num() > 0)
	{
		UMaterialExpression* Current = BfsQueue.Pop(EAllowShrinking::No);
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* NodeInput = Current->GetInput(i);
			if (!NodeInput)
			{
				break;
			}
			if (NodeInput->Expression && !ReachableSet.Contains(NodeInput->Expression))
			{
				ReachableSet.Add(NodeInput->Expression);
				BfsQueue.Add(NodeInput->Expression);
			}
		}
	}

	TMap<FString, TArray<UMaterialExpression*>> ParameterNames;
	TArray<UMaterialExpression*> IslandExprs;

	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (!Expr)
		{
			continue;
		}

		bool bIsReachable = ReachableSet.Contains(Expr);

		// Check: Disconnected islands (skip comments)
		if (!bIsReachable && !Cast<UMaterialExpressionComment>(Expr))
		{
			auto IssueJson = MakeShared<FJsonObject>();
			IssueJson->SetStringField(TEXT("severity"), TEXT("warning"));
			IssueJson->SetStringField(TEXT("type"), TEXT("island"));
			IssueJson->SetStringField(TEXT("expression"), Expr->GetName());
			IssueJson->SetStringField(TEXT("class"), Expr->GetClass()->GetName());

			bool bFixed = false;
			if (bFixIssues)
			{
				IslandExprs.Add(Expr);
				bFixed = true;
				FixedCount++;
			}
			IssueJson->SetBoolField(TEXT("fixed"), bFixed);
			IssuesArray.Add(MakeShared<FJsonValueObject>(IssueJson));
		}

		// Check: Broken texture refs
		if (const auto* TexBase = Cast<UMaterialExpressionTextureBase>(Expr))
		{
			if (!TexBase->Texture)
			{
				auto IssueJson = MakeShared<FJsonObject>();
				IssueJson->SetStringField(TEXT("severity"), TEXT("error"));
				IssueJson->SetStringField(TEXT("type"), TEXT("broken_texture_ref"));
				IssueJson->SetStringField(TEXT("expression"), Expr->GetName());
				IssueJson->SetBoolField(TEXT("fixed"), false);
				IssuesArray.Add(MakeShared<FJsonValueObject>(IssueJson));
			}
		}

		// Check: Missing material functions
		if (const auto* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
		{
			if (!FuncCall->MaterialFunction)
			{
				auto IssueJson = MakeShared<FJsonObject>();
				IssueJson->SetStringField(TEXT("severity"), TEXT("error"));
				IssueJson->SetStringField(TEXT("type"), TEXT("missing_material_function"));
				IssueJson->SetStringField(TEXT("expression"), Expr->GetName());
				IssueJson->SetBoolField(TEXT("fixed"), false);
				IssuesArray.Add(MakeShared<FJsonValueObject>(IssueJson));
			}
		}

		// Collect parameter names for duplicate detection
		if (const auto* Param = Cast<UMaterialExpressionParameter>(Expr))
		{
			ParameterNames.FindOrAdd(Param->ParameterName.ToString()).Add(Expr);
		}
		else if (const auto* TexParam = Cast<UMaterialExpressionTextureSampleParameter>(Expr))
		{
			ParameterNames.FindOrAdd(TexParam->ParameterName.ToString()).Add(Expr);
		}

		// Check: Unused parameters
		if (!bIsReachable)
		{
			bool bIsParam = Cast<UMaterialExpressionParameter>(Expr) != nullptr
				|| Cast<UMaterialExpressionTextureSampleParameter>(Expr) != nullptr;
			if (bIsParam)
			{
				auto IssueJson = MakeShared<FJsonObject>();
				IssueJson->SetStringField(TEXT("severity"), TEXT("warning"));
				IssueJson->SetStringField(TEXT("type"), TEXT("unused_parameter"));
				IssueJson->SetStringField(TEXT("expression"), Expr->GetName());
				IssueJson->SetBoolField(TEXT("fixed"), false);
				IssuesArray.Add(MakeShared<FJsonValueObject>(IssueJson));
			}
		}
	}

	// Check: Duplicate parameter names
	for (const auto& Pair : ParameterNames)
	{
		if (Pair.Value.Num() > 1)
		{
			auto IssueJson = MakeShared<FJsonObject>();
			IssueJson->SetStringField(TEXT("severity"), TEXT("warning"));
			IssueJson->SetStringField(TEXT("type"), TEXT("duplicate_parameter_name"));
			IssueJson->SetStringField(TEXT("parameter_name"), Pair.Key);
			IssueJson->SetNumberField(TEXT("count"), Pair.Value.Num());

			TArray<TSharedPtr<FJsonValue>> DupExprs;
			for (UMaterialExpression* DupExpr : Pair.Value)
			{
				DupExprs.Add(MakeShared<FJsonValueString>(DupExpr->GetName()));
			}
			IssueJson->SetArrayField(TEXT("expressions"), DupExprs);
			IssueJson->SetBoolField(TEXT("fixed"), false);
			IssuesArray.Add(MakeShared<FJsonValueObject>(IssueJson));
		}
	}

	// Check: Expression count warning
	if (Expressions.Num() > 200)
	{
		auto IssueJson = MakeShared<FJsonObject>();
		IssueJson->SetStringField(TEXT("severity"), TEXT("info"));
		IssueJson->SetStringField(TEXT("type"), TEXT("high_expression_count"));
		IssueJson->SetNumberField(TEXT("count"), Expressions.Num());
		IssueJson->SetBoolField(TEXT("fixed"), false);
		IssuesArray.Add(MakeShared<FJsonValueObject>(IssueJson));
	}

	// Apply fixes — delete island expressions
	if (bFixIssues && IslandExprs.Num() > 0)
	{
		GEditor->BeginTransaction(FText::FromString(TEXT("ValidateMaterial_FixIslands")));
		Mat->Modify();
		for (UMaterialExpression* IslandExpr : IslandExprs)
		{
			UMaterialEditingLibrary::DeleteMaterialExpression(Mat, IslandExpr);
		}
		GEditor->EndTransaction();
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetArrayField(TEXT("issues"), IssuesArray);
	ResultJson->SetNumberField(TEXT("issue_count"), IssuesArray.Num());
	ResultJson->SetNumberField(TEXT("fixed_count"), FixedCount);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: render_preview
// Params: { "asset_path": "...", "resolution": 256 }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::RenderPreview(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	int32 Resolution = Params->HasField(TEXT("resolution")) ? static_cast<int32>(Params->GetNumberField(TEXT("resolution"))) : 256;
	if (Resolution <= 0)
	{
		Resolution = 256;
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load asset at '%s'"), *AssetPath));
	}

	FObjectThumbnail Thumbnail;
	ThumbnailTools::RenderThumbnail(LoadedAsset, Resolution, Resolution,
		ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush, nullptr, &Thumbnail);

	if (Thumbnail.GetImageWidth() == 0 || Thumbnail.GetImageHeight() == 0)
	{
		return FMonolithActionResult::Error(TEXT("Thumbnail rendering produced an empty image"));
	}

	TArray<uint8>& ThumbData = Thumbnail.AccessImageData();
	int32 Width = Thumbnail.GetImageWidth();
	int32 Height = Thumbnail.GetImageHeight();

	TArray64<uint8> PngData;
	FImageView ImageView((void*)ThumbData.GetData(), Width, Height, ERawImageFormat::BGRA8);
	FImageUtils::CompressImage(PngData, TEXT(".png"), ImageView);

	if (PngData.Num() == 0)
	{
		return FMonolithActionResult::Error(TEXT("Failed to compress thumbnail to PNG"));
	}

	FString MaterialName = FPaths::GetBaseFilename(AssetPath);
	FString PreviewDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Monolith"), TEXT("previews"));
	IFileManager::Get().MakeDirectory(*PreviewDir, true);
	FString FilePath = FPaths::Combine(PreviewDir, FString::Printf(TEXT("%s_%d.png"), *MaterialName, Resolution));

	if (!FFileHelper::SaveArrayToFile(PngData, *FilePath))
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to save PNG to '%s'"), *FilePath));
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetStringField(TEXT("file_path"), FilePath);
	ResultJson->SetNumberField(TEXT("width"), Width);
	ResultJson->SetNumberField(TEXT("height"), Height);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: get_thumbnail
// Params: { "asset_path": "...", "resolution": 256, "save_to_file": false }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::GetThumbnail(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	int32 Resolution = Params->HasField(TEXT("resolution")) ? static_cast<int32>(Params->GetNumberField(TEXT("resolution"))) : 256;
	if (Resolution <= 0)
	{
		Resolution = 256;
	}

	bool bSaveToFile = false;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("save_to_file"), bSaveToFile);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load asset at '%s'"), *AssetPath));
	}

	FObjectThumbnail Thumbnail;
	ThumbnailTools::RenderThumbnail(LoadedAsset, Resolution, Resolution,
		ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush, nullptr, &Thumbnail);

	if (Thumbnail.GetImageWidth() == 0 || Thumbnail.GetImageHeight() == 0)
	{
		return FMonolithActionResult::Error(TEXT("Thumbnail rendering produced an empty image"));
	}

	TArray<uint8>& ThumbData = Thumbnail.AccessImageData();
	int32 Width = Thumbnail.GetImageWidth();
	int32 Height = Thumbnail.GetImageHeight();

	TArray64<uint8> PngData;
	FImageView ImageView((void*)ThumbData.GetData(), Width, Height, ERawImageFormat::BGRA8);
	FImageUtils::CompressImage(PngData, TEXT(".png"), ImageView);

	if (PngData.Num() == 0)
	{
		return FMonolithActionResult::Error(TEXT("Failed to compress thumbnail to PNG"));
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetNumberField(TEXT("width"), Width);
	ResultJson->SetNumberField(TEXT("height"), Height);

	if (bSaveToFile)
	{
		FString AssetName = FPaths::GetBaseFilename(AssetPath);
		FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Monolith"), TEXT("previews"));
		IFileManager::Get().MakeDirectory(*SaveDir, true);
		FString FullPath = FPaths::Combine(SaveDir, FString::Printf(TEXT("%s_%d.png"), *AssetName, Resolution));

		if (!FFileHelper::SaveArrayToFile(PngData, *FullPath))
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to save thumbnail to '%s'"), *FullPath));
		}

		ResultJson->SetStringField(TEXT("file_path"), FullPath);
	}
	else
	{
		FString Base64String = FBase64::Encode(PngData.GetData(), static_cast<uint32>(PngData.Num()));
		ResultJson->SetStringField(TEXT("format"), TEXT("png"));
		ResultJson->SetStringField(TEXT("encoding"), TEXT("base64"));
		ResultJson->SetStringField(TEXT("data"), Base64String);
	}

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: create_custom_hlsl_node
// Params: { "asset_path": "...", "code": "...", "description": "...", "output_type": "Float4",
//           "inputs": [...], "additional_outputs": [...], "pos_x": 0, "pos_y": 0 }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::CreateCustomHLSLNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString Code = Params->GetStringField(TEXT("code"));
	FString Description = Params->HasField(TEXT("description")) ? Params->GetStringField(TEXT("description")) : TEXT("");
	FString OutputType = Params->HasField(TEXT("output_type")) ? Params->GetStringField(TEXT("output_type")) : TEXT("");
	int32 PosX = Params->HasField(TEXT("pos_x")) ? static_cast<int32>(Params->GetNumberField(TEXT("pos_x"))) : 0;
	int32 PosY = Params->HasField(TEXT("pos_y")) ? static_cast<int32>(Params->GetNumberField(TEXT("pos_y"))) : 0;

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	GEditor->BeginTransaction(FText::FromString(TEXT("CreateCustomHLSLNode")));
	Mat->Modify();

	UMaterialExpression* BaseExpr = UMaterialEditingLibrary::CreateMaterialExpression(
		Mat, UMaterialExpressionCustom::StaticClass(), PosX, PosY);

	UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(BaseExpr);
	if (!CustomExpr)
	{
		GEditor->EndTransaction();
		return FMonolithActionResult::Error(TEXT("Failed to create Custom HLSL expression"));
	}

	CustomExpr->Code = Code;
	CustomExpr->Description = Description;

	if (!OutputType.IsEmpty())
	{
		CustomExpr->OutputType = ParseCustomOutputType(OutputType);
	}

	// Set inputs from JSON array
	const TArray<TSharedPtr<FJsonValue>>* InputsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("inputs"), InputsArray))
	{
		CustomExpr->Inputs.Empty();
		for (const TSharedPtr<FJsonValue>& InputVal : *InputsArray)
		{
			const TSharedPtr<FJsonObject>* InputObjPtr = nullptr;
			if (InputVal && InputVal->TryGetObject(InputObjPtr) && InputObjPtr)
			{
				FCustomInput NewInput;
				NewInput.InputName = *(*InputObjPtr)->GetStringField(TEXT("name"));
				CustomExpr->Inputs.Add(NewInput);
			}
		}
	}

	// Set additional outputs from JSON array
	const TArray<TSharedPtr<FJsonValue>>* AddOutputsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("additional_outputs"), AddOutputsArray))
	{
		CustomExpr->AdditionalOutputs.Empty();
		for (const TSharedPtr<FJsonValue>& OutVal : *AddOutputsArray)
		{
			const TSharedPtr<FJsonObject>* OutObjPtr = nullptr;
			if (OutVal && OutVal->TryGetObject(OutObjPtr) && OutObjPtr)
			{
				FCustomOutput NewOutput;
				NewOutput.OutputName = *(*OutObjPtr)->GetStringField(TEXT("name"));
				if ((*OutObjPtr)->HasField(TEXT("type")))
				{
					NewOutput.OutputType = ParseCustomOutputType((*OutObjPtr)->GetStringField(TEXT("type")));
				}
				CustomExpr->AdditionalOutputs.Add(NewOutput);
			}
		}
	}

	CustomExpr->RebuildOutputs();
	GEditor->EndTransaction();

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetStringField(TEXT("expression_name"), CustomExpr->GetName());
	ResultJson->SetStringField(TEXT("description"), CustomExpr->Description);
	ResultJson->SetStringField(TEXT("output_type"), CustomOutputTypeToString(CustomExpr->OutputType));
	ResultJson->SetNumberField(TEXT("input_count"), CustomExpr->Inputs.Num());
	ResultJson->SetNumberField(TEXT("additional_output_count"), CustomExpr->AdditionalOutputs.Num());
	ResultJson->SetNumberField(TEXT("pos_x"), PosX);
	ResultJson->SetNumberField(TEXT("pos_y"), PosY);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: get_layer_info
// Params: { "asset_path": "..." }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::GetLayerInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load asset at '%s'"), *AssetPath));
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);

	UMaterialFunctionMaterialLayer* Layer = Cast<UMaterialFunctionMaterialLayer>(LoadedAsset);
	UMaterialFunctionMaterialLayerBlend* LayerBlend = Cast<UMaterialFunctionMaterialLayerBlend>(LoadedAsset);
	UMaterialFunction* MatFunc = Cast<UMaterialFunction>(LoadedAsset);

	if (Layer)
	{
		ResultJson->SetStringField(TEXT("type"), TEXT("MaterialLayer"));
		MatFunc = Layer;
	}
	else if (LayerBlend)
	{
		ResultJson->SetStringField(TEXT("type"), TEXT("MaterialLayerBlend"));
		MatFunc = LayerBlend;
	}
	else if (MatFunc)
	{
		ResultJson->SetStringField(TEXT("type"), TEXT("MaterialFunction"));
	}
	else
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Asset '%s' is not a MaterialFunction, MaterialLayer, or MaterialLayerBlend"), *AssetPath));
	}

	ResultJson->SetStringField(TEXT("description"), MatFunc->Description);

	TArray<TSharedPtr<FJsonValue>> FuncExpressionsArray;
	TArray<TSharedPtr<FJsonValue>> FuncInputsArray;
	TArray<TSharedPtr<FJsonValue>> FuncOutputsArray;

	TConstArrayView<TObjectPtr<UMaterialExpression>> FuncExprs = MatFunc->GetExpressions();

	for (const TObjectPtr<UMaterialExpression>& Expr : FuncExprs)
	{
		if (!Expr)
		{
			continue;
		}

		if (const auto* FuncInput = Cast<UMaterialExpressionFunctionInput>(Expr))
		{
			auto InputJson = MakeShared<FJsonObject>();
			InputJson->SetStringField(TEXT("name"), FuncInput->InputName.ToString());
			InputJson->SetStringField(TEXT("expression_name"), FuncInput->GetName());
			InputJson->SetNumberField(TEXT("sort_priority"), FuncInput->SortPriority);
			FuncInputsArray.Add(MakeShared<FJsonValueObject>(InputJson));
		}

		if (const auto* FuncOutput = Cast<UMaterialExpressionFunctionOutput>(Expr))
		{
			auto OutputJson = MakeShared<FJsonObject>();
			OutputJson->SetStringField(TEXT("name"), FuncOutput->OutputName.ToString());
			OutputJson->SetStringField(TEXT("expression_name"), FuncOutput->GetName());
			OutputJson->SetNumberField(TEXT("sort_priority"), FuncOutput->SortPriority);
			FuncOutputsArray.Add(MakeShared<FJsonValueObject>(OutputJson));
		}

		auto ExprJson = MakeShared<FJsonObject>();
		ExprJson->SetStringField(TEXT("name"), Expr->GetName());
		ExprJson->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		ExprJson->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
		ExprJson->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);
		FuncExpressionsArray.Add(MakeShared<FJsonValueObject>(ExprJson));
	}

	ResultJson->SetArrayField(TEXT("inputs"), FuncInputsArray);
	ResultJson->SetArrayField(TEXT("outputs"), FuncOutputsArray);
	ResultJson->SetArrayField(TEXT("expressions"), FuncExpressionsArray);
	ResultJson->SetNumberField(TEXT("expression_count"), FuncExpressionsArray.Num());

	return FMonolithActionResult::Success(ResultJson);
}
