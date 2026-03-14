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
#include "Materials/MaterialInstanceConstant.h"
#include "EditorAssetLibrary.h"
#include "MaterialEditingLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "MaterialShared.h"
#include "RHIShaderPlatform.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "ObjectTools.h"
#include "ImageUtils.h"
#include "UObject/SavePackage.h"

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
			.Optional(TEXT("target_expression"), TEXT("string"), TEXT("Only disconnect from this specific downstream expression (requires disconnect_outputs=true)"))
			.Optional(TEXT("output_index"), TEXT("integer"), TEXT("Only disconnect connections using this output index"))
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

	// --- Wave 2: Asset creation & properties ---

	Registry.RegisterAction(TEXT("material"), TEXT("create_material"),
		TEXT("Create a new empty material asset at the specified path"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::CreateMaterial),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path e.g. /Game/Materials/M_MyMaterial"))
			.Optional(TEXT("blend_mode"), TEXT("string"), TEXT("Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite"), TEXT("Opaque"))
			.Optional(TEXT("shading_model"), TEXT("string"), TEXT("DefaultLit, Unlit, Subsurface, SubsurfaceProfile, ClearCoat, TwoSidedFoliage"), TEXT("DefaultLit"))
			.Optional(TEXT("material_domain"), TEXT("string"), TEXT("Surface, DeferredDecal, PostProcess, LightFunction, UI"), TEXT("Surface"))
			.Optional(TEXT("two_sided"), TEXT("bool"), TEXT("Enable two-sided rendering"), TEXT("false"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("create_material_instance"),
		TEXT("Create a material instance constant from a parent material"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::CreateMaterialInstance),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Path for the new instance e.g. /Game/Materials/MI_MyInstance"))
			.Required(TEXT("parent_material"), TEXT("string"), TEXT("Path to parent material or material instance"))
			.Optional(TEXT("scalar_parameters"), TEXT("object"), TEXT("Map of scalar param name to float value"))
			.Optional(TEXT("vector_parameters"), TEXT("object"), TEXT("Map of vector param name to {R,G,B,A} object"))
			.Optional(TEXT("texture_parameters"), TEXT("object"), TEXT("Map of texture param name to texture asset path"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("set_material_property"),
		TEXT("Set top-level material properties (blend mode, shading model, domain, two-sided, opacity mask clip, etc.)"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::SetMaterialProperty),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Optional(TEXT("blend_mode"), TEXT("string"), TEXT("Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite"))
			.Optional(TEXT("shading_model"), TEXT("string"), TEXT("DefaultLit, Unlit, Subsurface, SubsurfaceProfile, ClearCoat, TwoSidedFoliage"))
			.Optional(TEXT("material_domain"), TEXT("string"), TEXT("Surface, DeferredDecal, PostProcess, LightFunction, UI"))
			.Optional(TEXT("two_sided"), TEXT("bool"), TEXT("Enable two-sided rendering"))
			.Optional(TEXT("opacity_mask_clip_value"), TEXT("number"), TEXT("Clip value for masked blend mode"))
			.Optional(TEXT("dithered_lod_transition"), TEXT("bool"), TEXT("Enable dithered LOD transition"))
			.Optional(TEXT("used_with_skeletal_mesh"), TEXT("bool"), TEXT("Mark as used with skeletal meshes"))
			.Optional(TEXT("used_with_particle_sprites"), TEXT("bool"), TEXT("Mark as used with particle sprites"))
			.Optional(TEXT("used_with_niagara_sprites"), TEXT("bool"), TEXT("Mark as used with Niagara sprites"))
			.Optional(TEXT("used_with_niagara_meshes"), TEXT("bool"), TEXT("Mark as used with Niagara meshes"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("delete_expression"),
		TEXT("Delete a material expression node by name, cleaning up all connections"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::DeleteExpression),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Required(TEXT("expression_name"), TEXT("string"), TEXT("Name of the expression to delete"))
			.Build());

	// --- Wave 2B: Parameter management, recompile, duplicate ---

	Registry.RegisterAction(TEXT("material"), TEXT("get_material_parameters"),
		TEXT("List all parameters in a material or material instance with types, defaults, and groups"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::GetMaterialParameters),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material or material instance asset path"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("set_instance_parameter"),
		TEXT("Set a parameter override on an existing material instance"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::SetInstanceParameter),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material instance asset path"))
			.Required(TEXT("parameter_name"), TEXT("string"), TEXT("Parameter name to set"))
			.Optional(TEXT("scalar_value"), TEXT("number"), TEXT("Float value for scalar parameters"))
			.Optional(TEXT("vector_value"), TEXT("object"), TEXT("{R,G,B,A} object for vector parameters"))
			.Optional(TEXT("texture_value"), TEXT("string"), TEXT("Texture asset path for texture parameters"))
			.Optional(TEXT("switch_value"), TEXT("bool"), TEXT("Boolean for static switch parameters"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("recompile_material"),
		TEXT("Force recompile a material and return success/failure"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::RecompileMaterial),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("duplicate_material"),
		TEXT("Duplicate an existing material or material instance to a new path"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::DuplicateMaterial),
		FParamSchemaBuilder()
			.Required(TEXT("source_path"), TEXT("string"), TEXT("Path of the material to duplicate"))
			.Required(TEXT("dest_path"), TEXT("string"), TEXT("Destination path for the copy"))
			.Build());

	// --- Wave 2C: Advanced utilities ---

	Registry.RegisterAction(TEXT("material"), TEXT("get_compilation_stats"),
		TEXT("Get shader compilation statistics: instruction count, texture samplers, errors"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::GetCompilationStats),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("set_expression_property"),
		TEXT("Set a property value on an existing material expression node"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::SetExpressionProperty),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Required(TEXT("expression_name"), TEXT("string"), TEXT("Name of the expression node"))
			.Required(TEXT("property_name"), TEXT("string"), TEXT("Property to set (e.g. R, ParameterName, SamplerType, Texture)"))
			.Required(TEXT("value"), TEXT("string"), TEXT("Value as string (parsed via ImportText for complex types) or number"))
			.Build());

	Registry.RegisterAction(TEXT("material"), TEXT("connect_expressions"),
		TEXT("Connect an expression output to another expression input or a material output property"),
		FMonolithActionHandler::CreateStatic(&FMonolithMaterialActions::ConnectExpressions),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Material asset path"))
			.Required(TEXT("from_expression"), TEXT("string"), TEXT("Source expression name"))
			.Optional(TEXT("from_output"), TEXT("string"), TEXT("Source output pin name (empty = default)"))
			.Optional(TEXT("to_expression"), TEXT("string"), TEXT("Target expression name (for expr-to-expr)"))
			.Optional(TEXT("to_input"), TEXT("string"), TEXT("Target input pin name (empty = default)"))
			.Optional(TEXT("to_property"), TEXT("string"), TEXT("Material property name: BaseColor, Roughness, etc. (for expr-to-material)"))
			.Build());
}

// ============================================================================
// Helpers
// ============================================================================

UMaterial* FMonolithMaterialActions::LoadBaseMaterial(const FString& AssetPath)
{
	// Try UEditorAssetLibrary first (loads from disk)
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (LoadedAsset)
	{
		return Cast<UMaterial>(LoadedAsset);
	}

	// Fallback: check in-memory objects (e.g. freshly created, not yet saved)
	FString FullObjectPath = AssetPath;
	if (!FullObjectPath.Contains(TEXT(".")))
	{
		// Convert "/Game/Path/Name" to "/Game/Path/Name.Name"
		int32 LastSlash;
		if (FullObjectPath.FindLastChar('/', LastSlash))
		{
			FString ObjName = FullObjectPath.Mid(LastSlash + 1);
			FullObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *ObjName);
		}
	}
	UObject* Found = FindFirstObject<UMaterial>(*FullObjectPath, EFindFirstObjectOptions::NativeFirst);
	return Cast<UMaterial>(Found);
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
// Params: { "asset_path": "...", "expression_name": "...", "input_name": "", "disconnect_outputs": false, "target_expression": "", "output_index": -1 }
// When disconnect_outputs=true: disconnects other expressions/material outputs that reference expression_name
//   target_expression: filter to only disconnect from this downstream expression or material property name (e.g. "BaseColor")
//   output_index: filter to only disconnect connections using this output index
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::DisconnectExpression(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExpressionName = Params->GetStringField(TEXT("expression_name"));
	FString InputName = Params->HasField(TEXT("input_name")) ? Params->GetStringField(TEXT("input_name")) : TEXT("");
	bool bDisconnectOutputs = Params->HasField(TEXT("disconnect_outputs")) ? Params->GetBoolField(TEXT("disconnect_outputs")) : false;
	// Optional filter: only disconnect from a specific downstream expression (when disconnect_outputs=true)
	FString TargetDownstream = Params->HasField(TEXT("target_expression")) ? Params->GetStringField(TEXT("target_expression")) : TEXT("");
	// Optional filter: only disconnect a specific output index
	int32 TargetOutputIndex = Params->HasField(TEXT("output_index")) ? static_cast<int32>(Params->GetNumberField(TEXT("output_index"))) : -1;

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

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "DisconnectExpr", "Disconnect Expression"));
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
					// Filter by target downstream expression name if specified
					if (!TargetDownstream.IsEmpty() && Expr->GetName() != TargetDownstream) continue;
					// Filter by output index if specified
					if (TargetOutputIndex >= 0 && Input->OutputIndex != TargetOutputIndex) continue;

					auto DisconnJson = MakeShared<FJsonObject>();
					DisconnJson->SetStringField(TEXT("expression"), Expr->GetName());
					DisconnJson->SetStringField(TEXT("pin"), Expr->GetInputName(i).ToString());
					DisconnJson->SetNumberField(TEXT("output_index"), Input->OutputIndex);
					DisconnectedArray.Add(MakeShared<FJsonValueObject>(DisconnJson));

					Input->Expression = nullptr;
					Input->OutputIndex = 0;
					DisconnectCount++;
				}
			}
		}

		// Also check material output properties (expression -> material pin connections)
		for (const FMaterialOutputEntry& Entry : MaterialOutputEntries)
		{
			FExpressionInput* MatInput = Mat->GetExpressionInputForProperty(Entry.Property);
			if (!MatInput || MatInput->Expression != TargetExpr) continue;

			// Filter by target downstream: match against material property name (e.g. "BaseColor", "EmissiveColor")
			if (!TargetDownstream.IsEmpty() && TargetDownstream != Entry.Name) continue;
			// Filter by output index if specified
			if (TargetOutputIndex >= 0 && MatInput->OutputIndex != TargetOutputIndex) continue;

			auto DisconnJson = MakeShared<FJsonObject>();
			DisconnJson->SetStringField(TEXT("material_property"), Entry.Name);
			DisconnJson->SetNumberField(TEXT("output_index"), MatInput->OutputIndex);
			DisconnectedArray.Add(MakeShared<FJsonValueObject>(DisconnJson));

			MatInput->Expression = nullptr;
			MatInput->OutputIndex = 0;
			DisconnectCount++;
		}
	}

	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	GEditor->EndTransaction();

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

	// Bug fix: save material-level properties before clear — DeleteAllMaterialExpressions
	// triggers PostEditChange which can reset BlendMode and other scalar properties
	const EBlendMode SavedBlendMode = Mat->BlendMode;
	const EMaterialShadingModel SavedShadingModel = Mat->GetShadingModels().GetFirstShadingModel();
	const bool bSavedTwoSided = Mat->TwoSided;
	const float SavedOpacityMaskClipValue = Mat->OpacityMaskClipValue;

	if (bClearExisting)
	{
		// Bug fix: DeleteAllMaterialExpressions uses a range-for over TConstArrayView
		// while Remove() mutates the backing TArray — iterator invalidation skips ~half.
		// Copy to a local array first.
		TArray<UMaterialExpression*> ToDelete;
		for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
		{
			if (Expr)
			{
				ToDelete.Add(Expr);
			}
		}
		for (UMaterialExpression* Expr : ToDelete)
		{
			UMaterialEditingLibrary::DeleteMaterialExpression(Mat, Expr);
		}
	}

	// Restore material-level properties that may have been reset by PostEditChange
	Mat->BlendMode = SavedBlendMode;
	Mat->SetShadingModel(SavedShadingModel);
	Mat->TwoSided = bSavedTwoSided;
	Mat->OpacityMaskClipValue = SavedOpacityMaskClipValue;

	TMap<FString, UMaterialExpression*> IdToExpr;

	// Bug fix: seed remap table with pre-existing expressions so connections
	// can reference nodes that survived a partial clear or were already present
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (Expr)
		{
			IdToExpr.Add(Expr->GetName(), Expr.Get());
		}
	}

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

			// Try multiple lookup strategies for the expression class
			UClass* ExprClass = FindFirstObject<UClass>(*FullClassName, EFindFirstObjectOptions::NativeFirst);
			if (!ExprClass)
			{
				// Try with U prefix (UMaterialExpressionConstant)
				FString UClassName = FString::Printf(TEXT("U%s"), *FullClassName);
				ExprClass = FindFirstObject<UClass>(*UClassName, EFindFirstObjectOptions::NativeFirst);
			}
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
			// Bug fix: when graph_spec arrives as a pre-serialized JSON string, newlines
			// get double-encoded as literal \\n text. Unescape them.
			CustomExpr->Code.ReplaceInline(TEXT("\\n"), TEXT("\n"), ESearchCase::CaseSensitive);
			CustomExpr->Code.ReplaceInline(TEXT("\\t"), TEXT("\t"), ESearchCase::CaseSensitive);
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

			// Blend mode validation warnings
			{
				EBlendMode BM = Mat->BlendMode;
				if (MatProp == MP_Opacity && (BM == BLEND_Opaque || BM == BLEND_Masked))
				{
					auto WJ = MakeShared<FJsonObject>();
					WJ->SetStringField(TEXT("output"), ToProp);
					WJ->SetStringField(TEXT("warning"), TEXT("Opacity has no effect on Opaque/Masked materials"));
					ErrorsArray.Add(MakeShared<FJsonValueObject>(WJ));
				}
				if (MatProp == MP_OpacityMask && BM != BLEND_Masked)
				{
					auto WJ = MakeShared<FJsonObject>();
					WJ->SetStringField(TEXT("output"), ToProp);
					WJ->SetStringField(TEXT("warning"), TEXT("OpacityMask only affects Masked blend mode"));
					ErrorsArray.Add(MakeShared<FJsonValueObject>(WJ));
				}
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

	// Auto-recompile so the material is immediately usable
	UMaterialEditingLibrary::RecompileMaterial(Mat);

	GEditor->EndTransaction();

	auto ResultJson = MakeShared<FJsonObject>();
	// Separate actual errors from warnings (blend mode warnings are not errors)
	int32 ErrorCount = 0, WarningCount = 0;
	for (const TSharedPtr<FJsonValue>& Entry : ErrorsArray)
	{
		if (Entry->AsObject()->HasField(TEXT("warning"))) WarningCount++;
		else ErrorCount++;
	}
	ResultJson->SetBoolField(TEXT("has_errors"), ErrorCount > 0);
	ResultJson->SetBoolField(TEXT("has_warnings"), WarningCount > 0);
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetNumberField(TEXT("nodes_created"), NodesCreated);
	ResultJson->SetNumberField(TEXT("connections_made"), ConnectionsMade);
	ResultJson->SetBoolField(TEXT("recompiled"), true);

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

// ============================================================================
// Wave 2 — Helpers
// ============================================================================

static EMaterialDomain ParseMaterialDomain(const FString& Str)
{
	if (Str.Equals(TEXT("DeferredDecal"), ESearchCase::IgnoreCase)) return MD_DeferredDecal;
	if (Str.Equals(TEXT("PostProcess"), ESearchCase::IgnoreCase)) return MD_PostProcess;
	if (Str.Equals(TEXT("LightFunction"), ESearchCase::IgnoreCase)) return MD_LightFunction;
	if (Str.Equals(TEXT("UI"), ESearchCase::IgnoreCase)) return MD_UI;
	return MD_Surface;
}

static EBlendMode ParseBlendMode(const FString& Str)
{
	if (Str.Equals(TEXT("Masked"), ESearchCase::IgnoreCase)) return BLEND_Masked;
	if (Str.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase)) return BLEND_Translucent;
	if (Str.Equals(TEXT("Additive"), ESearchCase::IgnoreCase)) return BLEND_Additive;
	if (Str.Equals(TEXT("Modulate"), ESearchCase::IgnoreCase)) return BLEND_Modulate;
	if (Str.Equals(TEXT("AlphaComposite"), ESearchCase::IgnoreCase)) return BLEND_AlphaComposite;
	return BLEND_Opaque;
}

static EMaterialShadingModel ParseShadingModel(const FString& Str)
{
	if (Str.Equals(TEXT("Unlit"), ESearchCase::IgnoreCase)) return MSM_Unlit;
	if (Str.Equals(TEXT("Subsurface"), ESearchCase::IgnoreCase)) return MSM_Subsurface;
	if (Str.Equals(TEXT("SubsurfaceProfile"), ESearchCase::IgnoreCase)) return MSM_SubsurfaceProfile;
	if (Str.Equals(TEXT("ClearCoat"), ESearchCase::IgnoreCase)) return MSM_ClearCoat;
	if (Str.Equals(TEXT("TwoSidedFoliage"), ESearchCase::IgnoreCase)) return MSM_TwoSidedFoliage;
	return MSM_DefaultLit;
}

// ============================================================================
// Action: create_material
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::CreateMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	// Parse optional properties
	FString BlendModeStr = Params->HasField(TEXT("blend_mode")) ? Params->GetStringField(TEXT("blend_mode")) : TEXT("Opaque");
	FString ShadingModelStr = Params->HasField(TEXT("shading_model")) ? Params->GetStringField(TEXT("shading_model")) : TEXT("DefaultLit");
	FString DomainStr = Params->HasField(TEXT("material_domain")) ? Params->GetStringField(TEXT("material_domain")) : TEXT("Surface");
	bool bTwoSided = Params->HasField(TEXT("two_sided")) ? Params->GetBoolField(TEXT("two_sided")) : false;

	// Extract package path and asset name from the asset path
	FString PackagePath, AssetName;
	int32 LastSlash;
	if (AssetPath.FindLastChar('/', LastSlash))
	{
		PackagePath = AssetPath.Left(LastSlash);
		AssetName = AssetPath.Mid(LastSlash + 1);
	}
	else
	{
		return FMonolithActionResult::Error(TEXT("Invalid asset path — must contain at least one '/' (e.g. /Game/Materials/M_Name)"));
	}

	if (AssetName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Asset name is empty"));
	}

	// Check if asset already exists
	UObject* Existing = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (Existing)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Asset already exists at '%s'"), *AssetPath));
	}

	// Create package and material
	UPackage* Pkg = CreatePackage(*AssetPath);
	if (!Pkg)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to create package at '%s'"), *AssetPath));
	}

	UMaterial* NewMat = NewObject<UMaterial>(Pkg, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!NewMat)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create UMaterial object"));
	}

	// Set material properties
	NewMat->MaterialDomain = ParseMaterialDomain(DomainStr);
	NewMat->BlendMode = ParseBlendMode(BlendModeStr);
	NewMat->SetShadingModel(ParseShadingModel(ShadingModelStr));
	NewMat->TwoSided = bTwoSided;

	// Register with asset registry and mark dirty
	FAssetRegistryModule::AssetCreated(NewMat);
	Pkg->MarkPackageDirty();

	// Trigger initial compile
	NewMat->PreEditChange(nullptr);
	NewMat->PostEditChange();

	// Save to disk so LoadAsset can find it later
	FString PackageFilename = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Pkg, NewMat, *PackageFilename, SaveArgs);

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), NewMat->GetPathName());
	ResultJson->SetStringField(TEXT("asset_name"), AssetName);
	ResultJson->SetStringField(TEXT("blend_mode"), BlendModeStr);
	ResultJson->SetStringField(TEXT("shading_model"), ShadingModelStr);
	ResultJson->SetStringField(TEXT("material_domain"), DomainStr);
	ResultJson->SetBoolField(TEXT("two_sided"), bTwoSided);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: create_material_instance
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::CreateMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ParentPath = Params->GetStringField(TEXT("parent_material"));

	// Load parent material
	UObject* ParentObj = UEditorAssetLibrary::LoadAsset(ParentPath);
	UMaterialInterface* ParentMat = ParentObj ? Cast<UMaterialInterface>(ParentObj) : nullptr;
	if (!ParentMat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load parent material at '%s'"), *ParentPath));
	}

	// Check if asset already exists
	UObject* Existing = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (Existing)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Asset already exists at '%s'"), *AssetPath));
	}

	// Extract asset name
	FString AssetName;
	int32 LastSlash;
	if (!AssetPath.FindLastChar('/', LastSlash) || LastSlash == AssetPath.Len() - 1)
	{
		return FMonolithActionResult::Error(TEXT("Invalid asset path"));
	}
	AssetName = AssetPath.Mid(LastSlash + 1);

	// Create package and MIC
	UPackage* Pkg = CreatePackage(*AssetPath);
	if (!Pkg)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create package"));
	}

	UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(Pkg, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!MIC)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create UMaterialInstanceConstant"));
	}

	MIC->SetParentEditorOnly(ParentMat);

	// Apply scalar parameter overrides
	int32 ScalarCount = 0;
	const TSharedPtr<FJsonObject>* ScalarParams = nullptr;
	if (Params->TryGetObjectField(TEXT("scalar_parameters"), ScalarParams))
	{
		for (const auto& Pair : (*ScalarParams)->Values)
		{
			float Value = static_cast<float>(Pair.Value->AsNumber());
			MIC->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(*Pair.Key), Value);
			ScalarCount++;
		}
	}

	// Apply vector parameter overrides
	int32 VectorCount = 0;
	const TSharedPtr<FJsonObject>* VectorParams = nullptr;
	if (Params->TryGetObjectField(TEXT("vector_parameters"), VectorParams))
	{
		for (const auto& Pair : (*VectorParams)->Values)
		{
			const TSharedPtr<FJsonObject>* ColorObj = nullptr;
			if (Pair.Value->TryGetObject(ColorObj))
			{
				FLinearColor Color;
				Color.R = (*ColorObj)->HasField(TEXT("R")) ? static_cast<float>((*ColorObj)->GetNumberField(TEXT("R"))) : 0.f;
				Color.G = (*ColorObj)->HasField(TEXT("G")) ? static_cast<float>((*ColorObj)->GetNumberField(TEXT("G"))) : 0.f;
				Color.B = (*ColorObj)->HasField(TEXT("B")) ? static_cast<float>((*ColorObj)->GetNumberField(TEXT("B"))) : 0.f;
				Color.A = (*ColorObj)->HasField(TEXT("A")) ? static_cast<float>((*ColorObj)->GetNumberField(TEXT("A"))) : 1.f;
				MIC->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(*Pair.Key), Color);
				VectorCount++;
			}
		}
	}

	// Apply texture parameter overrides
	int32 TextureCount = 0;
	const TSharedPtr<FJsonObject>* TextureParams = nullptr;
	if (Params->TryGetObjectField(TEXT("texture_parameters"), TextureParams))
	{
		for (const auto& Pair : (*TextureParams)->Values)
		{
			FString TexPath = Pair.Value->AsString();
			UTexture* Tex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(TexPath));
			if (Tex)
			{
				MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(*Pair.Key), Tex);
				TextureCount++;
			}
		}
	}

	FAssetRegistryModule::AssetCreated(MIC);
	Pkg->MarkPackageDirty();

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), MIC->GetPathName());
	ResultJson->SetStringField(TEXT("asset_name"), AssetName);
	ResultJson->SetStringField(TEXT("parent_material"), ParentMat->GetPathName());
	ResultJson->SetNumberField(TEXT("scalar_overrides"), ScalarCount);
	ResultJson->SetNumberField(TEXT("vector_overrides"), VectorCount);
	ResultJson->SetNumberField(TEXT("texture_overrides"), TextureCount);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: set_material_property
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::SetMaterialProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	GEditor->BeginTransaction(FText::FromString(TEXT("SetMaterialProperty")));
	Mat->Modify();

	TArray<TSharedPtr<FJsonValue>> ChangedArray;
	int32 ChangeCount = 0;

	auto RecordChange = [&](const FString& PropName, const FString& Value)
	{
		auto ChangeJson = MakeShared<FJsonObject>();
		ChangeJson->SetStringField(TEXT("property"), PropName);
		ChangeJson->SetStringField(TEXT("value"), Value);
		ChangedArray.Add(MakeShared<FJsonValueObject>(ChangeJson));
		ChangeCount++;
	};

	if (Params->HasField(TEXT("blend_mode")))
	{
		FString Val = Params->GetStringField(TEXT("blend_mode"));
		Mat->BlendMode = ParseBlendMode(Val);
		RecordChange(TEXT("blend_mode"), Val);
	}
	if (Params->HasField(TEXT("shading_model")))
	{
		FString Val = Params->GetStringField(TEXT("shading_model"));
		Mat->SetShadingModel(ParseShadingModel(Val));
		RecordChange(TEXT("shading_model"), Val);
	}
	if (Params->HasField(TEXT("material_domain")))
	{
		FString Val = Params->GetStringField(TEXT("material_domain"));
		Mat->MaterialDomain = ParseMaterialDomain(Val);
		RecordChange(TEXT("material_domain"), Val);
	}
	if (Params->HasField(TEXT("two_sided")))
	{
		bool Val = Params->GetBoolField(TEXT("two_sided"));
		Mat->TwoSided = Val;
		RecordChange(TEXT("two_sided"), Val ? TEXT("true") : TEXT("false"));
	}
	if (Params->HasField(TEXT("opacity_mask_clip_value")))
	{
		float Val = static_cast<float>(Params->GetNumberField(TEXT("opacity_mask_clip_value")));
		Mat->OpacityMaskClipValue = Val;
		RecordChange(TEXT("opacity_mask_clip_value"), FString::SanitizeFloat(Val));
	}
	if (Params->HasField(TEXT("dithered_lod_transition")))
	{
		bool Val = Params->GetBoolField(TEXT("dithered_lod_transition"));
		Mat->DitheredLODTransition = Val;
		RecordChange(TEXT("dithered_lod_transition"), Val ? TEXT("true") : TEXT("false"));
	}
	if (Params->HasField(TEXT("used_with_skeletal_mesh")))
	{
		bool Val = Params->GetBoolField(TEXT("used_with_skeletal_mesh"));
		if (Val)
		{
			bool bRecompile = false;
			UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_SkeletalMesh, bRecompile);
		}
		RecordChange(TEXT("used_with_skeletal_mesh"), Val ? TEXT("true") : TEXT("false"));
	}
	if (Params->HasField(TEXT("used_with_particle_sprites")))
	{
		bool Val = Params->GetBoolField(TEXT("used_with_particle_sprites"));
		if (Val)
		{
			bool bRecompile = false;
			UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_ParticleSprites, bRecompile);
		}
		RecordChange(TEXT("used_with_particle_sprites"), Val ? TEXT("true") : TEXT("false"));
	}
	if (Params->HasField(TEXT("used_with_niagara_sprites")))
	{
		bool Val = Params->GetBoolField(TEXT("used_with_niagara_sprites"));
		if (Val)
		{
			bool bRecompile = false;
			UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_NiagaraSprites, bRecompile);
		}
		RecordChange(TEXT("used_with_niagara_sprites"), Val ? TEXT("true") : TEXT("false"));
	}
	if (Params->HasField(TEXT("used_with_niagara_meshes")))
	{
		bool Val = Params->GetBoolField(TEXT("used_with_niagara_meshes"));
		if (Val)
		{
			bool bRecompile = false;
			UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_NiagaraMeshParticles, bRecompile);
		}
		RecordChange(TEXT("used_with_niagara_meshes"), Val ? TEXT("true") : TEXT("false"));
	}

	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	GEditor->EndTransaction();

	// Bug fix: flush property changes to disk so subsequent LoadAsset calls
	// (e.g. get_compilation_stats) don't read stale on-disk values after GC
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetNumberField(TEXT("changes"), ChangeCount);
	ResultJson->SetArrayField(TEXT("changed"), ChangedArray);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: delete_expression
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::DeleteExpression(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExprName = Params->GetStringField(TEXT("expression_name"));

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	// Find the expression
	UMaterialExpression* TargetExpr = nullptr;
	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Mat->GetExpressions();
	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (Expr && Expr->GetName() == ExprName)
		{
			TargetExpr = Expr;
			break;
		}
	}

	if (!TargetExpr)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Expression '%s' not found in material '%s'"), *ExprName, *AssetPath));
	}

	GEditor->BeginTransaction(FText::FromString(TEXT("DeleteExpression")));
	Mat->Modify();

	FString ClassName = TargetExpr->GetClass()->GetName();
	UMaterialEditingLibrary::DeleteMaterialExpression(Mat, TargetExpr);

	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	GEditor->EndTransaction();

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetStringField(TEXT("deleted"), ExprName);
	ResultJson->SetStringField(TEXT("class"), ClassName);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: get_material_parameters
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::GetMaterialParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UMaterialInterface* MatInterface = LoadedAsset ? Cast<UMaterialInterface>(LoadedAsset) : nullptr;
	if (!MatInterface)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> ScalarArray, VectorArray, TextureArray, SwitchArray;

	// Scalar parameters
	TArray<FMaterialParameterInfo> ScalarInfos;
	TArray<FGuid> ScalarGuids;
	MatInterface->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);
	for (int32 i = 0; i < ScalarInfos.Num(); ++i)
	{
		float Value = 0.f;
		MatInterface->GetScalarParameterValue(ScalarInfos[i], Value);

		auto PJson = MakeShared<FJsonObject>();
		PJson->SetStringField(TEXT("name"), ScalarInfos[i].Name.ToString());
		PJson->SetNumberField(TEXT("value"), Value);
		PJson->SetStringField(TEXT("type"), TEXT("scalar"));
		ScalarArray.Add(MakeShared<FJsonValueObject>(PJson));
	}

	// Vector parameters
	TArray<FMaterialParameterInfo> VectorInfos;
	TArray<FGuid> VectorGuids;
	MatInterface->GetAllVectorParameterInfo(VectorInfos, VectorGuids);
	for (int32 i = 0; i < VectorInfos.Num(); ++i)
	{
		FLinearColor Value;
		MatInterface->GetVectorParameterValue(VectorInfos[i], Value);

		auto PJson = MakeShared<FJsonObject>();
		PJson->SetStringField(TEXT("name"), VectorInfos[i].Name.ToString());
		auto ColorJson = MakeShared<FJsonObject>();
		ColorJson->SetNumberField(TEXT("R"), Value.R);
		ColorJson->SetNumberField(TEXT("G"), Value.G);
		ColorJson->SetNumberField(TEXT("B"), Value.B);
		ColorJson->SetNumberField(TEXT("A"), Value.A);
		PJson->SetObjectField(TEXT("value"), ColorJson);
		PJson->SetStringField(TEXT("type"), TEXT("vector"));
		VectorArray.Add(MakeShared<FJsonValueObject>(PJson));
	}

	// Texture parameters
	TArray<FMaterialParameterInfo> TextureInfos;
	TArray<FGuid> TextureGuids;
	MatInterface->GetAllTextureParameterInfo(TextureInfos, TextureGuids);
	for (int32 i = 0; i < TextureInfos.Num(); ++i)
	{
		UTexture* Tex = nullptr;
		MatInterface->GetTextureParameterValue(TextureInfos[i], Tex);

		auto PJson = MakeShared<FJsonObject>();
		PJson->SetStringField(TEXT("name"), TextureInfos[i].Name.ToString());
		PJson->SetStringField(TEXT("value"), Tex ? Tex->GetPathName() : TEXT("None"));
		PJson->SetStringField(TEXT("type"), TEXT("texture"));
		TextureArray.Add(MakeShared<FJsonValueObject>(PJson));
	}

	// Static switch parameters
	TArray<FMaterialParameterInfo> SwitchInfos;
	TArray<FGuid> SwitchGuids;
	MatInterface->GetAllStaticSwitchParameterInfo(SwitchInfos, SwitchGuids);
	for (int32 i = 0; i < SwitchInfos.Num(); ++i)
	{
		bool Value = false;
		FGuid OutGuid;
		MatInterface->GetStaticSwitchParameterValue(SwitchInfos[i], Value, OutGuid);

		auto PJson = MakeShared<FJsonObject>();
		PJson->SetStringField(TEXT("name"), SwitchInfos[i].Name.ToString());
		PJson->SetBoolField(TEXT("value"), Value);
		PJson->SetStringField(TEXT("type"), TEXT("static_switch"));
		SwitchArray.Add(MakeShared<FJsonValueObject>(PJson));
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetArrayField(TEXT("scalar_parameters"), ScalarArray);
	ResultJson->SetArrayField(TEXT("vector_parameters"), VectorArray);
	ResultJson->SetArrayField(TEXT("texture_parameters"), TextureArray);
	ResultJson->SetArrayField(TEXT("static_switch_parameters"), SwitchArray);
	ResultJson->SetNumberField(TEXT("total_parameters"),
		ScalarArray.Num() + VectorArray.Num() + TextureArray.Num() + SwitchArray.Num());

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: set_instance_parameter
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::SetInstanceParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ParamName = Params->GetStringField(TEXT("parameter_name"));

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UMaterialInstanceConstant* MIC = LoadedAsset ? Cast<UMaterialInstanceConstant>(LoadedAsset) : nullptr;
	if (!MIC)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load material instance at '%s'"), *AssetPath));
	}

	MIC->Modify();
	FString SetType;
	FString SetValue;

	FMaterialParameterInfo ParamInfo(*ParamName);

	if (Params->HasField(TEXT("scalar_value")))
	{
		float Val = static_cast<float>(Params->GetNumberField(TEXT("scalar_value")));
		MIC->SetScalarParameterValueEditorOnly(ParamInfo, Val);
		SetType = TEXT("scalar");
		SetValue = FString::SanitizeFloat(Val);
	}
	else if (Params->HasField(TEXT("vector_value")))
	{
		const TSharedPtr<FJsonObject>* ColorObj = nullptr;
		if (Params->TryGetObjectField(TEXT("vector_value"), ColorObj))
		{
			FLinearColor Color;
			Color.R = (*ColorObj)->HasField(TEXT("R")) ? static_cast<float>((*ColorObj)->GetNumberField(TEXT("R"))) : 0.f;
			Color.G = (*ColorObj)->HasField(TEXT("G")) ? static_cast<float>((*ColorObj)->GetNumberField(TEXT("G"))) : 0.f;
			Color.B = (*ColorObj)->HasField(TEXT("B")) ? static_cast<float>((*ColorObj)->GetNumberField(TEXT("B"))) : 0.f;
			Color.A = (*ColorObj)->HasField(TEXT("A")) ? static_cast<float>((*ColorObj)->GetNumberField(TEXT("A"))) : 1.f;
			MIC->SetVectorParameterValueEditorOnly(ParamInfo, Color);
			SetType = TEXT("vector");
			SetValue = FString::Printf(TEXT("(%.3f, %.3f, %.3f, %.3f)"), Color.R, Color.G, Color.B, Color.A);
		}
	}
	else if (Params->HasField(TEXT("texture_value")))
	{
		FString TexPath = Params->GetStringField(TEXT("texture_value"));
		UTexture* Tex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(TexPath));
		if (!Tex)
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load texture at '%s'"), *TexPath));
		}
		MIC->SetTextureParameterValueEditorOnly(ParamInfo, Tex);
		SetType = TEXT("texture");
		SetValue = TexPath;
	}
	else if (Params->HasField(TEXT("switch_value")))
	{
		bool Val = Params->GetBoolField(TEXT("switch_value"));
		MIC->SetStaticSwitchParameterValueEditorOnly(ParamInfo, Val);
		SetType = TEXT("static_switch");
		SetValue = Val ? TEXT("true") : TEXT("false");
	}
	else
	{
		return FMonolithActionResult::Error(TEXT("Must provide one of: scalar_value, vector_value, texture_value, switch_value"));
	}

	MIC->MarkPackageDirty();

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetStringField(TEXT("parameter_name"), ParamName);
	ResultJson->SetStringField(TEXT("type"), SetType);
	ResultJson->SetStringField(TEXT("value"), SetValue);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: recompile_material
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::RecompileMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UMaterialInterface* MatInterface = LoadedAsset ? Cast<UMaterialInterface>(LoadedAsset) : nullptr;
	if (!MatInterface)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
	}

	// For base materials, trigger full recompile
	UMaterial* BaseMat = MatInterface->GetMaterial();
	if (BaseMat)
	{
		UMaterialEditingLibrary::RecompileMaterial(BaseMat);
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetStringField(TEXT("status"), TEXT("recompiled"));

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: duplicate_material
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::DuplicateMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath = Params->GetStringField(TEXT("source_path"));
	FString DestPath = Params->GetStringField(TEXT("dest_path"));

	// Check source exists
	UObject* SourceObj = UEditorAssetLibrary::LoadAsset(SourcePath);
	if (!SourceObj)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Source material not found at '%s'"), *SourcePath));
	}

	// Check dest doesn't exist
	if (UEditorAssetLibrary::LoadAsset(DestPath))
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Asset already exists at '%s'"), *DestPath));
	}

	UObject* DuplicatedObj = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);
	if (!DuplicatedObj)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to duplicate '%s' to '%s'"), *SourcePath, *DestPath));
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("source_path"), SourcePath);
	ResultJson->SetStringField(TEXT("dest_path"), DestPath);
	ResultJson->SetStringField(TEXT("asset_class"), SourceObj->GetClass()->GetName());

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: get_compilation_stats
// Params: { "asset_path": "..." }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::GetCompilationStats(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UMaterialInterface* MatInterface = LoadedAsset ? Cast<UMaterialInterface>(LoadedAsset) : nullptr;
	if (!MatInterface)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
	}

	UMaterial* BaseMat = MatInterface->GetMaterial();
	if (!BaseMat)
	{
		return FMonolithActionResult::Error(TEXT("Could not resolve base material"));
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);

	// Get material resource for the current shader platform
	const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel];
	FMaterialResource* MatResource = BaseMat->GetMaterialResource(ShaderPlatform);
	if (MatResource)
	{
		bool bIsCompiled = MatResource->IsGameThreadShaderMapComplete();
		ResultJson->SetBoolField(TEXT("is_compiled"), bIsCompiled);

#if WITH_EDITOR
		// Sampler count
		int32 SamplerCount = MatResource->GetSamplerUsage();
		ResultJson->SetNumberField(TEXT("num_samplers"), SamplerCount);

		// Estimated texture samples (VS + PS)
		uint32 VSSamples = 0, PSSamples = 0;
		MatResource->GetEstimatedNumTextureSamples(VSSamples, PSSamples);
		ResultJson->SetNumberField(TEXT("estimated_vs_texture_samples"), static_cast<double>(VSSamples));
		ResultJson->SetNumberField(TEXT("estimated_ps_texture_samples"), static_cast<double>(PSSamples));

		// User interpolator usage
		uint32 NumUsedUVScalars = 0, NumUsedCustomInterpolatorScalars = 0;
		MatResource->GetUserInterpolatorUsage(NumUsedUVScalars, NumUsedCustomInterpolatorScalars);
		ResultJson->SetNumberField(TEXT("used_uv_scalars"), static_cast<double>(NumUsedUVScalars));
		ResultJson->SetNumberField(TEXT("used_custom_interpolator_scalars"), static_cast<double>(NumUsedCustomInterpolatorScalars));
#endif

		// Compile errors
		const TArray<FString>& Errors = MatResource->GetCompileErrors();
		if (Errors.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ErrorsArray;
			for (const FString& Err : Errors)
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(Err));
			}
			ResultJson->SetArrayField(TEXT("compile_errors"), ErrorsArray);
		}
	}
	else
	{
		ResultJson->SetBoolField(TEXT("is_compiled"), false);
		ResultJson->SetStringField(TEXT("note"), TEXT("Material resource not available - try recompile_material first"));
	}

	// Instruction counts via UMaterialEditingLibrary::GetStatistics (uses FMaterialStatsUtils internally)
	FMaterialStatistics Stats = UMaterialEditingLibrary::GetStatistics(MatInterface);
	ResultJson->SetNumberField(TEXT("num_vertex_shader_instructions"), Stats.NumVertexShaderInstructions);
	ResultJson->SetNumberField(TEXT("num_pixel_shader_instructions"), Stats.NumPixelShaderInstructions);

	// Override is_compiled if we got valid instruction counts — shader map may not be loaded in memory
	// but stats are cached from a previous compilation. Without this, is_compiled=false is misleading.
	if (!ResultJson->GetBoolField(TEXT("is_compiled")) && (Stats.NumVertexShaderInstructions > 0 || Stats.NumPixelShaderInstructions > 0))
	{
		ResultJson->SetBoolField(TEXT("is_compiled"), true);
	}

	// Material properties (always available from the base material)
	ResultJson->SetStringField(TEXT("blend_mode"),
		BaseMat->BlendMode == BLEND_Opaque ? TEXT("Opaque") :
		BaseMat->BlendMode == BLEND_Masked ? TEXT("Masked") :
		BaseMat->BlendMode == BLEND_Translucent ? TEXT("Translucent") :
		BaseMat->BlendMode == BLEND_Additive ? TEXT("Additive") :
		BaseMat->BlendMode == BLEND_AlphaComposite ? TEXT("AlphaComposite") :
		BaseMat->BlendMode == BLEND_Modulate ? TEXT("Modulate") : TEXT("Other"));
	ResultJson->SetBoolField(TEXT("two_sided"), BaseMat->TwoSided);
	ResultJson->SetNumberField(TEXT("expression_count"), BaseMat->GetExpressions().Num());

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: set_expression_property
// Params: { "asset_path": "...", "expression_name": "...", "property_name": "...", "value": "..." }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::SetExpressionProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExprName = Params->GetStringField(TEXT("expression_name"));
	FString PropName = Params->GetStringField(TEXT("property_name"));
	FString ValueStr = Params->GetStringField(TEXT("value"));

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	// Find expression
	UMaterialExpression* TargetExpr = nullptr;
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (Expr && Expr->GetName() == ExprName)
		{
			TargetExpr = Expr;
			break;
		}
	}

	if (!TargetExpr)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Expression '%s' not found in material '%s'"), *ExprName, *AssetPath));
	}

	FProperty* Prop = TargetExpr->GetClass()->FindPropertyByName(*PropName);
	if (!Prop)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Property '%s' not found on expression '%s'"), *PropName, *ExprName));
	}

	GEditor->BeginTransaction(FText::FromString(TEXT("SetExpressionProperty")));
	TargetExpr->Modify();

	// Handle numeric types directly, everything else via ImportText
	bool bSuccess = false;

	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		float Val = FCString::Atof(*ValueStr);
		FloatProp->SetPropertyValue_InContainer(TargetExpr, Val);
		bSuccess = true;
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
	{
		double Val = FCString::Atod(*ValueStr);
		DoubleProp->SetPropertyValue_InContainer(TargetExpr, Val);
		bSuccess = true;
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
	{
		int32 Val = FCString::Atoi(*ValueStr);
		IntProp->SetPropertyValue_InContainer(TargetExpr, Val);
		bSuccess = true;
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		bool Val = ValueStr.Equals(TEXT("true"), ESearchCase::IgnoreCase) || ValueStr == TEXT("1");
		BoolProp->SetPropertyValue_InContainer(TargetExpr, Val);
		bSuccess = true;
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
	{
		NameProp->SetPropertyValue_InContainer(TargetExpr, FName(*ValueStr));
		bSuccess = true;
	}
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
	{
		StrProp->SetPropertyValue_InContainer(TargetExpr, ValueStr);
		bSuccess = true;
	}
	else
	{
		// Generic ImportText for structs, enums, object references, etc.
		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(TargetExpr);
		bSuccess = Prop->ImportText_Direct(*ValueStr, PropAddr, TargetExpr, PPF_None) != nullptr;
	}

	if (bSuccess)
	{
		// Pass the actual property so PostEditChangePropertyInternal calls
		// MaterialGraph->RebuildGraph() and the editor display updates correctly.
		FPropertyChangedEvent ChangeEvent(Prop);
		Mat->PreEditChange(Prop);
		Mat->PostEditChangeProperty(ChangeEvent);
	}

	GEditor->EndTransaction();

	if (!bSuccess)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to set property '%s' to '%s' on expression '%s'"), *PropName, *ValueStr, *ExprName));
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetStringField(TEXT("expression_name"), ExprName);
	ResultJson->SetStringField(TEXT("property_name"), PropName);
	ResultJson->SetStringField(TEXT("value"), ValueStr);

	return FMonolithActionResult::Success(ResultJson);
}

// ============================================================================
// Action: connect_expressions
// Params: { "asset_path", "from_expression", "from_output"?, "to_expression"?, "to_input"?, "to_property"? }
// ============================================================================

FMonolithActionResult FMonolithMaterialActions::ConnectExpressions(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString FromExprName = Params->GetStringField(TEXT("from_expression"));
	FString FromOutput = Params->HasField(TEXT("from_output")) ? Params->GetStringField(TEXT("from_output")) : TEXT("");
	FString ToExprName = Params->HasField(TEXT("to_expression")) ? Params->GetStringField(TEXT("to_expression")) : TEXT("");
	FString ToInput = Params->HasField(TEXT("to_input")) ? Params->GetStringField(TEXT("to_input")) : TEXT("");
	FString ToProperty = Params->HasField(TEXT("to_property")) ? Params->GetStringField(TEXT("to_property")) : TEXT("");

	if (ToExprName.IsEmpty() && ToProperty.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Must provide either to_expression or to_property"));
	}

	UMaterial* Mat = LoadBaseMaterial(AssetPath);
	if (!Mat)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load base material at '%s'"), *AssetPath));
	}

	// Find source expression (and optionally target expression)
	UMaterialExpression* FromExpr = nullptr;
	UMaterialExpression* ToExpr = nullptr;
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (Expr)
		{
			if (Expr->GetName() == FromExprName) FromExpr = Expr;
			if (!ToExprName.IsEmpty() && Expr->GetName() == ToExprName) ToExpr = Expr;
		}
	}

	if (!FromExpr)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Source expression '%s' not found"), *FromExprName));
	}

	GEditor->BeginTransaction(FText::FromString(TEXT("ConnectExpressions")));
	Mat->Modify();

	bool bConnected = false;
	FString ConnectionDesc;

	TArray<FString> Warnings;

	if (!ToProperty.IsEmpty())
	{
		// Blend mode validation: warn about irrelevant material output connections
		EMaterialProperty MatProp = ParseMaterialProperty(ToProperty);
		if (MatProp != MP_MAX)
		{
			EBlendMode BM = Mat->BlendMode;
			if (MatProp == MP_Opacity && (BM == BLEND_Opaque || BM == BLEND_Masked))
				Warnings.Add(TEXT("Opacity has no effect on Opaque/Masked materials. Set blend mode to Translucent/Additive first."));
			if (MatProp == MP_OpacityMask && BM != BLEND_Masked)
				Warnings.Add(TEXT("OpacityMask only affects Masked blend mode."));
			if (MatProp == MP_Refraction && BM == BLEND_Opaque)
				Warnings.Add(TEXT("Refraction has no effect on Opaque materials."));
		}

		// Connect to material output property
		bConnected = UMaterialEditingLibrary::ConnectMaterialProperty(FromExpr, FromOutput, MatProp);
		ConnectionDesc = FString::Printf(TEXT("%s -> %s"), *FromExprName, *ToProperty);
	}
	else if (ToExpr)
	{
		// Connect expression to expression
		bConnected = UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpr, FromOutput, ToExpr, ToInput);
		ConnectionDesc = FString::Printf(TEXT("%s -> %s"), *FromExprName, *ToExprName);
	}
	else
	{
		GEditor->EndTransaction();
		return FMonolithActionResult::Error(FString::Printf(TEXT("Target expression '%s' not found"), *ToExprName));
	}

	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	GEditor->EndTransaction();

	if (!bConnected)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Connection failed: %s"), *ConnectionDesc));
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
	ResultJson->SetStringField(TEXT("connection"), ConnectionDesc);
	ResultJson->SetBoolField(TEXT("connected"), true);
	if (Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnArr;
		for (const FString& W : Warnings) WarnArr.Add(MakeShared<FJsonValueString>(W));
		ResultJson->SetArrayField(TEXT("warnings"), WarnArr);
	}

	return FMonolithActionResult::Success(ResultJson);
}
