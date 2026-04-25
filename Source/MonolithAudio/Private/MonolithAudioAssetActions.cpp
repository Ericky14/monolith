#include "MonolithAudioAssetActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithParamSchema.h"
#include "MonolithJsonUtils.h"

// Audio asset types
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundSubmix.h"

// Factories (AudioEditor module)
#include "Factories/SoundAttenuationFactory.h"
#include "Factories/SoundClassFactory.h"
#include "Factories/SoundMixFactory.h"
#include "Factories/SoundConcurrencyFactory.h"
#include "Factories/SoundSubmixFactory.h"

// Asset registry & utilities
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ============================================================================
// Helpers
// ============================================================================

bool FMonolithAudioAssetActions::SplitAssetPath(const FString& AssetPath, FString& OutPackagePath, FString& OutAssetName)
{
	int32 LastSlash;
	if (!AssetPath.FindLastChar('/', LastSlash) || LastSlash <= 0)
	{
		return false;
	}
	OutPackagePath = AssetPath.Left(LastSlash);
	OutAssetName = AssetPath.Mid(LastSlash + 1);
	return !OutAssetName.IsEmpty();
}

template<typename T>
T* FMonolithAudioAssetActions::LoadAudioAsset(const FString& AssetPath, FString& OutError)
{
	// Normalize short paths: /Game/Foo/Bar -> /Game/Foo/Bar.Bar
	// FSoftObjectPath requires the Package.AssetName format to resolve.
	FString NormalizedPath = AssetPath;
	if (!NormalizedPath.Contains(TEXT(".")))
	{
		int32 LastSlash;
		if (NormalizedPath.FindLastChar('/', LastSlash) && LastSlash >= 0)
		{
			FString AssetName = NormalizedPath.Mid(LastSlash + 1);
			if (!AssetName.IsEmpty())
			{
				NormalizedPath = NormalizedPath + TEXT(".") + AssetName;
			}
		}
	}

	// Registry-first loading (per CLAUDE.md lessons)
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(NormalizedPath));
	if (AssetData.IsValid())
	{
		UObject* Loaded = AssetData.GetAsset();
		T* Typed = Cast<T>(Loaded);
		if (!Typed)
		{
			OutError = FString::Printf(TEXT("Asset at '%s' is not a %s (found %s)"),
				*AssetPath, *T::StaticClass()->GetName(),
				Loaded ? *Loaded->GetClass()->GetName() : TEXT("null"));
		}
		return Typed;
	}

	// StaticLoadObject fallback (handles both short and long paths)
	UObject* Loaded = StaticLoadObject(T::StaticClass(), nullptr, *AssetPath);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Asset not found at '%s'"), *AssetPath);
		return nullptr;
	}
	T* Typed = Cast<T>(Loaded);
	if (!Typed)
	{
		OutError = FString::Printf(TEXT("Asset at '%s' is not a %s"), *AssetPath, *T::StaticClass()->GetName());
	}
	return Typed;
}

template<typename TFactory, typename TAsset>
TAsset* FMonolithAudioAssetActions::CreateAudioAsset(const FString& AssetPath, FString& OutError)
{
	FString PackagePath, AssetName;
	if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
	{
		OutError = TEXT("Invalid asset path — must contain at least one '/' (e.g. /Game/Audio/SA_MyAttenuation)");
		return nullptr;
	}

	// Check if asset already exists
	UObject* Existing = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (Existing)
	{
		OutError = FString::Printf(TEXT("Asset already exists at '%s'"), *AssetPath);
		return nullptr;
	}

	UPackage* Pkg = CreatePackage(*AssetPath);
	if (!Pkg)
	{
		OutError = FString::Printf(TEXT("Failed to create package at '%s'"), *AssetPath);
		return nullptr;
	}

	TFactory* Factory = NewObject<TFactory>();
	UObject* NewObj = Factory->FactoryCreateNew(
		TAsset::StaticClass(), Pkg, FName(*AssetName),
		RF_Public | RF_Standalone, nullptr, GWarn);

	TAsset* Asset = Cast<TAsset>(NewObj);
	if (!Asset)
	{
		OutError = TEXT("Factory failed to create asset");
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(Asset);
	Pkg->MarkPackageDirty();

	// Save to disk
	FString PackageFilename = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Pkg, Asset, *PackageFilename, SaveArgs);

	return Asset;
}

// ============================================================================
// Reflection: Struct <-> JSON
// ============================================================================

TSharedPtr<FJsonObject> FMonolithAudioAssetActions::StructToJson(const UStruct* StructDef, const void* StructData)
{
	auto Json = MakeShared<FJsonObject>();

	for (TFieldIterator<FProperty> PropIt(StructDef); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(StructData);
		const FString PropName = Prop->GetName();

		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			Json->SetBoolField(PropName, BoolProp->GetPropertyValue(ValuePtr));
		}
		else if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			Json->SetNumberField(PropName, FloatProp->GetPropertyValue(ValuePtr));
		}
		else if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			Json->SetNumberField(PropName, DoubleProp->GetPropertyValue(ValuePtr));
		}
		else if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			Json->SetNumberField(PropName, static_cast<double>(IntProp->GetPropertyValue(ValuePtr)));
		}
		else if (const FUInt32Property* UIntProp = CastField<FUInt32Property>(Prop))
		{
			Json->SetNumberField(PropName, static_cast<double>(UIntProp->GetPropertyValue(ValuePtr)));
		}
		else if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			Json->SetStringField(PropName, StrProp->GetPropertyValue(ValuePtr));
		}
		else if (const FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			Json->SetStringField(PropName, NameProp->GetPropertyValue(ValuePtr).ToString());
		}
		else if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const UEnum* Enum = EnumProp->GetEnum();
			FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
			int64 Val = UnderlyingProp->GetSignedIntPropertyValue(ValuePtr);
			FString EnumStr = Enum ? Enum->GetNameStringByValue(Val) : FString::FromInt(Val);
			Json->SetStringField(PropName, EnumStr);
		}
		else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (ByteProp->Enum)
			{
				uint8 Val = ByteProp->GetPropertyValue(ValuePtr);
				Json->SetStringField(PropName, ByteProp->Enum->GetNameStringByValue(Val));
			}
			else
			{
				Json->SetNumberField(PropName, static_cast<double>(ByteProp->GetPropertyValue(ValuePtr)));
			}
		}
		else if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
			Json->SetStringField(PropName, Obj ? Obj->GetPathName() : TEXT("None"));
		}
		else if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			// Recurse into nested structs
			TSharedPtr<FJsonObject> NestedJson = StructToJson(StructProp->Struct, ValuePtr);
			Json->SetObjectField(PropName, NestedJson);
		}
		else if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			// Serialize arrays of structs, objects, or primitives
			TArray<TSharedPtr<FJsonValue>> JsonArray;
			FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);

			for (int32 i = 0; i < ArrayHelper.Num(); ++i)
			{
				const void* ElemPtr = ArrayHelper.GetRawPtr(i);
				FProperty* InnerProp = ArrayProp->Inner;

				if (const FStructProperty* InnerStruct = CastField<FStructProperty>(InnerProp))
				{
					TSharedPtr<FJsonObject> ElemJson = StructToJson(InnerStruct->Struct, ElemPtr);
					JsonArray.Add(MakeShared<FJsonValueObject>(ElemJson));
				}
				else if (const FObjectPropertyBase* InnerObj = CastField<FObjectPropertyBase>(InnerProp))
				{
					UObject* Obj = InnerObj->GetObjectPropertyValue(ElemPtr);
					JsonArray.Add(MakeShared<FJsonValueString>(Obj ? Obj->GetPathName() : TEXT("None")));
				}
				else if (const FFloatProperty* InnerFloat = CastField<FFloatProperty>(InnerProp))
				{
					JsonArray.Add(MakeShared<FJsonValueNumber>(InnerFloat->GetPropertyValue(ElemPtr)));
				}
				else if (const FIntProperty* InnerInt = CastField<FIntProperty>(InnerProp))
				{
					JsonArray.Add(MakeShared<FJsonValueNumber>(static_cast<double>(InnerInt->GetPropertyValue(ElemPtr))));
				}
				else if (const FStrProperty* InnerStr = CastField<FStrProperty>(InnerProp))
				{
					JsonArray.Add(MakeShared<FJsonValueString>(InnerStr->GetPropertyValue(ElemPtr)));
				}
				else
				{
					// Fallback: export as string
					FString ExportStr;
					InnerProp->ExportTextItem_Direct(ExportStr, ElemPtr, nullptr, nullptr, PPF_None);
					JsonArray.Add(MakeShared<FJsonValueString>(ExportStr));
				}
			}
			Json->SetArrayField(PropName, JsonArray);
		}
		// Skip anything we can't serialize (delegates, maps, etc.)
	}

	return Json;
}

bool FMonolithAudioAssetActions::JsonToStruct(const TSharedPtr<FJsonObject>& Json, const UStruct* StructDef, void* StructData, FString& OutError)
{
	if (!Json.IsValid())
	{
		OutError = TEXT("JSON object is null");
		return false;
	}

	for (const auto& Pair : Json->Values)
	{
		const FString& FieldName = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonVal = Pair.Value;

		FProperty* Prop = StructDef->FindPropertyByName(FName(*FieldName));
		if (!Prop)
		{
			// Skip unknown fields silently — allows forward-compatible partial updates
			continue;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(StructData);

		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			bool bVal;
			if (JsonVal->TryGetBool(bVal))
			{
				BoolProp->SetPropertyValue(ValuePtr, bVal);
			}
		}
		else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			double DVal;
			if (JsonVal->TryGetNumber(DVal))
			{
				FloatProp->SetPropertyValue(ValuePtr, static_cast<float>(DVal));
			}
		}
		else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			double DVal;
			if (JsonVal->TryGetNumber(DVal))
			{
				DoubleProp->SetPropertyValue(ValuePtr, DVal);
			}
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			double DVal;
			if (JsonVal->TryGetNumber(DVal))
			{
				IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(DVal));
			}
		}
		else if (FUInt32Property* UIntProp = CastField<FUInt32Property>(Prop))
		{
			double DVal;
			if (JsonVal->TryGetNumber(DVal))
			{
				UIntProp->SetPropertyValue(ValuePtr, static_cast<uint32>(DVal));
			}
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			FString SVal;
			if (JsonVal->TryGetString(SVal))
			{
				StrProp->SetPropertyValue(ValuePtr, SVal);
			}
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			FString SVal;
			if (JsonVal->TryGetString(SVal))
			{
				NameProp->SetPropertyValue(ValuePtr, FName(*SVal));
			}
		}
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			FString SVal;
			if (JsonVal->TryGetString(SVal))
			{
				const UEnum* Enum = EnumProp->GetEnum();
				int64 EnumVal = Enum ? Enum->GetValueByNameString(SVal) : INDEX_NONE;
				if (EnumVal != INDEX_NONE)
				{
					FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
					UnderlyingProp->SetIntPropertyValue(ValuePtr, EnumVal);
				}
				else
				{
					OutError = FString::Printf(TEXT("Unknown enum value '%s' for property '%s'"), *SVal, *FieldName);
					return false;
				}
			}
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (ByteProp->Enum)
			{
				FString SVal;
				if (JsonVal->TryGetString(SVal))
				{
					int64 EnumVal = ByteProp->Enum->GetValueByNameString(SVal);
					if (EnumVal != INDEX_NONE)
					{
						ByteProp->SetPropertyValue(ValuePtr, static_cast<uint8>(EnumVal));
					}
					else
					{
						OutError = FString::Printf(TEXT("Unknown enum value '%s' for property '%s'"), *SVal, *FieldName);
						return false;
					}
				}
			}
			else
			{
				double DVal;
				if (JsonVal->TryGetNumber(DVal))
				{
					ByteProp->SetPropertyValue(ValuePtr, static_cast<uint8>(DVal));
				}
			}
		}
		else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			FString SVal;
			if (JsonVal->TryGetString(SVal))
			{
				if (SVal == TEXT("None") || SVal.IsEmpty())
				{
					ObjProp->SetObjectPropertyValue(ValuePtr, nullptr);
				}
				else
				{
					UObject* Loaded = StaticLoadObject(ObjProp->PropertyClass, nullptr, *SVal);
					if (Loaded)
					{
						ObjProp->SetObjectPropertyValue(ValuePtr, Loaded);
					}
					else
					{
						OutError = FString::Printf(TEXT("Could not load object '%s' for property '%s'"), *SVal, *FieldName);
						return false;
					}
				}
			}
		}
		else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			const TSharedPtr<FJsonObject>* NestedObj = nullptr;
			if (JsonVal->TryGetObject(NestedObj) && NestedObj && NestedObj->IsValid())
			{
				if (!JsonToStruct(*NestedObj, StructProp->Struct, ValuePtr, OutError))
				{
					return false;
				}
			}
		}
		// Arrays and other complex types: skip silently for now (partial update)
	}

	return true;
}

// ============================================================================
// Registration
// ============================================================================

void FMonolithAudioAssetActions::RegisterActions(FMonolithToolRegistry& Registry)
{
	// --- Sound Attenuation ---
	Registry.RegisterAction(TEXT("audio"), TEXT("create_sound_attenuation"),
		TEXT("Create a new USoundAttenuation asset with optional initial settings"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::CreateSoundAttenuation),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path (e.g. /Game/Audio/SA_MyAttenuation)"))
			.Optional(TEXT("settings"), TEXT("object"), TEXT("FSoundAttenuationSettings fields to set on creation"))
			.Build());

	Registry.RegisterAction(TEXT("audio"), TEXT("get_attenuation_settings"),
		TEXT("Get all FSoundAttenuationSettings fields from a USoundAttenuation asset"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::GetAttenuationSettings),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path of the USoundAttenuation"))
			.Build());

	Registry.RegisterAction(TEXT("audio"), TEXT("set_attenuation_settings"),
		TEXT("Set FSoundAttenuationSettings fields on a USoundAttenuation (partial update)"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::SetAttenuationSettings),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path of the USoundAttenuation"))
			.Required(TEXT("settings"), TEXT("object"), TEXT("FSoundAttenuationSettings fields to update"))
			.Build());

	// --- Sound Class ---
	Registry.RegisterAction(TEXT("audio"), TEXT("create_sound_class"),
		TEXT("Create a new USoundClass asset with optional parent and properties"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::CreateSoundClass),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path (e.g. /Game/Audio/SC_Ambient)"))
			.Optional(TEXT("parent_class"), TEXT("string"), TEXT("Asset path of parent USoundClass for hierarchy"))
			.Optional(TEXT("properties"), TEXT("object"), TEXT("FSoundClassProperties fields to set on creation"))
			.Build());

	Registry.RegisterAction(TEXT("audio"), TEXT("get_sound_class_properties"),
		TEXT("Get FSoundClassProperties + parent/children from a USoundClass asset"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::GetSoundClassProperties),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path of the USoundClass"))
			.Build());

	Registry.RegisterAction(TEXT("audio"), TEXT("set_sound_class_properties"),
		TEXT("Set FSoundClassProperties fields on a USoundClass (partial update). Optionally reparent."),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::SetSoundClassProperties),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path of the USoundClass"))
			.Optional(TEXT("properties"), TEXT("object"), TEXT("FSoundClassProperties fields to update"))
			.Optional(TEXT("parent_class"), TEXT("string"), TEXT("Asset path of new parent USoundClass (empty to clear)"))
			.Build());

	// --- Sound Mix ---
	Registry.RegisterAction(TEXT("audio"), TEXT("create_sound_mix"),
		TEXT("Create a new USoundMix asset with optional EQ settings and class adjusters"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::CreateSoundMix),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path (e.g. /Game/Audio/Mix_Combat)"))
			.Optional(TEXT("eq_settings"), TEXT("object"), TEXT("FAudioEQEffect fields (4-band EQ)"))
			.Optional(TEXT("class_effects"), TEXT("array"), TEXT("Array of FSoundClassAdjuster objects"))
			.Optional(TEXT("initial_delay"), TEXT("number"), TEXT("Delay before mix takes effect"))
			.Optional(TEXT("fade_in_time"), TEXT("number"), TEXT("Fade-in duration"))
			.Optional(TEXT("duration"), TEXT("number"), TEXT("Mix duration (-1 for infinite)"))
			.Optional(TEXT("fade_out_time"), TEXT("number"), TEXT("Fade-out duration"))
			.Build());

	Registry.RegisterAction(TEXT("audio"), TEXT("get_sound_mix_settings"),
		TEXT("Get EQ bands, class adjusters, and timing from a USoundMix asset"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::GetSoundMixSettings),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path of the USoundMix"))
			.Build());

	Registry.RegisterAction(TEXT("audio"), TEXT("set_sound_mix_settings"),
		TEXT("Set EQ settings, class adjusters, or timing on a USoundMix (partial update)"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::SetSoundMixSettings),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path of the USoundMix"))
			.Optional(TEXT("eq_settings"), TEXT("object"), TEXT("FAudioEQEffect fields to update"))
			.Optional(TEXT("class_effects"), TEXT("array"), TEXT("Array of FSoundClassAdjuster objects (replaces existing)"))
			.Optional(TEXT("initial_delay"), TEXT("number"), TEXT("Delay before mix takes effect"))
			.Optional(TEXT("fade_in_time"), TEXT("number"), TEXT("Fade-in duration"))
			.Optional(TEXT("duration"), TEXT("number"), TEXT("Mix duration (-1 for infinite)"))
			.Optional(TEXT("fade_out_time"), TEXT("number"), TEXT("Fade-out duration"))
			.Build());

	// --- Sound Concurrency ---
	Registry.RegisterAction(TEXT("audio"), TEXT("create_sound_concurrency"),
		TEXT("Create a new USoundConcurrency asset with optional settings"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::CreateSoundConcurrency),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path (e.g. /Game/Audio/Conc_Default)"))
			.Optional(TEXT("settings"), TEXT("object"), TEXT("FSoundConcurrencySettings fields"))
			.Build());

	Registry.RegisterAction(TEXT("audio"), TEXT("get_concurrency_settings"),
		TEXT("Get FSoundConcurrencySettings fields from a USoundConcurrency asset"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::GetConcurrencySettings),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path of the USoundConcurrency"))
			.Build());

	Registry.RegisterAction(TEXT("audio"), TEXT("set_concurrency_settings"),
		TEXT("Set FSoundConcurrencySettings fields on a USoundConcurrency (partial update)"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::SetConcurrencySettings),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path of the USoundConcurrency"))
			.Required(TEXT("settings"), TEXT("object"), TEXT("FSoundConcurrencySettings fields to update"))
			.Build());

	// --- Sound Submix ---
	Registry.RegisterAction(TEXT("audio"), TEXT("create_sound_submix"),
		TEXT("Create a new USoundSubmix asset with optional parent and effect chain"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::CreateSoundSubmix),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path (e.g. /Game/Audio/Submix_Reverb)"))
			.Optional(TEXT("parent_submix"), TEXT("string"), TEXT("Asset path of parent USoundSubmix"))
			.Build());

	Registry.RegisterAction(TEXT("audio"), TEXT("get_submix_properties"),
		TEXT("Get effect chain, volume, parent/children from a USoundSubmix asset"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::GetSubmixProperties),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path of the USoundSubmix"))
			.Build());

	Registry.RegisterAction(TEXT("audio"), TEXT("set_submix_properties"),
		TEXT("Set properties on a USoundSubmix (partial update)"),
		FMonolithActionHandler::CreateStatic(&FMonolithAudioAssetActions::SetSubmixProperties),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path of the USoundSubmix"))
			.Required(TEXT("properties"), TEXT("object"), TEXT("USoundSubmix properties to update"))
			.Build());
}

// ============================================================================
// Sound Attenuation
// ============================================================================

FMonolithActionResult FMonolithAudioAssetActions::CreateSoundAttenuation(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundAttenuation* Asset = CreateAudioAsset<USoundAttenuationFactory, USoundAttenuation>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	// Apply optional initial settings
	const TSharedPtr<FJsonObject>* SettingsJson = nullptr;
	if (Params->TryGetObjectField(TEXT("settings"), SettingsJson) && SettingsJson && SettingsJson->IsValid())
	{
		FString SetError;
		if (!JsonToStruct(*SettingsJson, FSoundAttenuationSettings::StaticStruct(), &Asset->Attenuation, SetError))
		{
			UE_LOG(LogMonolith, Warning, TEXT("create_sound_attenuation: partial settings error: %s"), *SetError);
		}
		Asset->GetPackage()->MarkPackageDirty();
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetObjectField(TEXT("settings"), StructToJson(FSoundAttenuationSettings::StaticStruct(), &Asset->Attenuation));
	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAudioAssetActions::GetAttenuationSettings(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundAttenuation* Asset = LoadAudioAsset<USoundAttenuation>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetObjectField(TEXT("settings"), StructToJson(FSoundAttenuationSettings::StaticStruct(), &Asset->Attenuation));
	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAudioAssetActions::SetAttenuationSettings(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	const TSharedPtr<FJsonObject>* SettingsJson = nullptr;
	if (!Params->TryGetObjectField(TEXT("settings"), SettingsJson) || !SettingsJson || !SettingsJson->IsValid())
	{
		return FMonolithActionResult::Error(TEXT("settings object is required"));
	}

	FString Error;
	USoundAttenuation* Asset = LoadAudioAsset<USoundAttenuation>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	Asset->Modify();
	if (!JsonToStruct(*SettingsJson, FSoundAttenuationSettings::StaticStruct(), &Asset->Attenuation, Error))
	{
		return FMonolithActionResult::Error(Error);
	}
	Asset->PostEditChange();
	Asset->GetPackage()->MarkPackageDirty();

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetObjectField(TEXT("settings"), StructToJson(FSoundAttenuationSettings::StaticStruct(), &Asset->Attenuation));
	return FMonolithActionResult::Success(Result);
}

// ============================================================================
// Sound Class
// ============================================================================

FMonolithActionResult FMonolithAudioAssetActions::CreateSoundClass(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundClass* Asset = CreateAudioAsset<USoundClassFactory, USoundClass>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	// Set parent class if specified
	FString ParentClassPath;
	if (Params->TryGetStringField(TEXT("parent_class"), ParentClassPath) && !ParentClassPath.IsEmpty())
	{
		FString LoadError;
		USoundClass* Parent = LoadAudioAsset<USoundClass>(ParentClassPath, LoadError);
		if (Parent)
		{
#if WITH_EDITOR
			Asset->SetParentClass(Parent);
#endif
		}
		else
		{
			UE_LOG(LogMonolith, Warning, TEXT("create_sound_class: could not load parent_class '%s': %s"), *ParentClassPath, *LoadError);
		}
	}

	// Apply optional properties
	const TSharedPtr<FJsonObject>* PropsJson = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsJson) && PropsJson && PropsJson->IsValid())
	{
		FString SetError;
		if (!JsonToStruct(*PropsJson, FSoundClassProperties::StaticStruct(), &Asset->Properties, SetError))
		{
			UE_LOG(LogMonolith, Warning, TEXT("create_sound_class: partial properties error: %s"), *SetError);
		}
		Asset->GetPackage()->MarkPackageDirty();
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetStringField(TEXT("parent_class"), Asset->ParentClass ? Asset->ParentClass->GetPathName() : TEXT("None"));
	Result->SetObjectField(TEXT("properties"), StructToJson(FSoundClassProperties::StaticStruct(), &Asset->Properties));
	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAudioAssetActions::GetSoundClassProperties(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundClass* Asset = LoadAudioAsset<USoundClass>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetStringField(TEXT("parent_class"), Asset->ParentClass ? Asset->ParentClass->GetPathName() : TEXT("None"));

	// Child classes
	TArray<TSharedPtr<FJsonValue>> ChildArray;
	for (USoundClass* Child : Asset->ChildClasses)
	{
		if (Child)
		{
			ChildArray.Add(MakeShared<FJsonValueString>(Child->GetPathName()));
		}
	}
	Result->SetArrayField(TEXT("child_classes"), ChildArray);

	Result->SetObjectField(TEXT("properties"), StructToJson(FSoundClassProperties::StaticStruct(), &Asset->Properties));
	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAudioAssetActions::SetSoundClassProperties(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundClass* Asset = LoadAudioAsset<USoundClass>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	Asset->Modify();

	// Reparent if requested
	FString ParentClassPath;
	if (Params->TryGetStringField(TEXT("parent_class"), ParentClassPath))
	{
		if (ParentClassPath.IsEmpty() || ParentClassPath == TEXT("None"))
		{
#if WITH_EDITOR
			Asset->SetParentClass(nullptr);
#endif
		}
		else
		{
			FString LoadError;
			USoundClass* Parent = LoadAudioAsset<USoundClass>(ParentClassPath, LoadError);
			if (Parent)
			{
#if WITH_EDITOR
				Asset->SetParentClass(Parent);
#endif
			}
			else
			{
				return FMonolithActionResult::Error(FString::Printf(TEXT("Could not load parent_class '%s': %s"), *ParentClassPath, *LoadError));
			}
		}
	}

	// Apply properties
	const TSharedPtr<FJsonObject>* PropsJson = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsJson) && PropsJson && PropsJson->IsValid())
	{
		if (!JsonToStruct(*PropsJson, FSoundClassProperties::StaticStruct(), &Asset->Properties, Error))
		{
			return FMonolithActionResult::Error(Error);
		}
	}

	Asset->PostEditChange();
	Asset->GetPackage()->MarkPackageDirty();

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetStringField(TEXT("parent_class"), Asset->ParentClass ? Asset->ParentClass->GetPathName() : TEXT("None"));
	Result->SetObjectField(TEXT("properties"), StructToJson(FSoundClassProperties::StaticStruct(), &Asset->Properties));
	return FMonolithActionResult::Success(Result);
}

// ============================================================================
// Sound Mix
// ============================================================================

static TSharedPtr<FJsonObject> SoundClassAdjusterToJson(const FSoundClassAdjuster& Adjuster)
{
	auto Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("SoundClass"), Adjuster.SoundClassObject ? Adjuster.SoundClassObject->GetPathName() : TEXT("None"));
	Json->SetNumberField(TEXT("VolumeAdjuster"), Adjuster.VolumeAdjuster);
	Json->SetNumberField(TEXT("PitchAdjuster"), Adjuster.PitchAdjuster);
	Json->SetNumberField(TEXT("LowPassFilterFrequency"), Adjuster.LowPassFilterFrequency);
	Json->SetBoolField(TEXT("bApplyToChildren"), Adjuster.bApplyToChildren);
	return Json;
}

static bool JsonToSoundClassAdjuster(const TSharedPtr<FJsonObject>& Json, FSoundClassAdjuster& OutAdjuster, FString& OutError)
{
	if (!Json.IsValid())
	{
		OutError = TEXT("Null class adjuster object");
		return false;
	}

	FString ClassPath;
	if (Json->TryGetStringField(TEXT("SoundClass"), ClassPath) && !ClassPath.IsEmpty() && ClassPath != TEXT("None"))
	{
		UObject* Loaded = StaticLoadObject(USoundClass::StaticClass(), nullptr, *ClassPath);
		if (!Loaded)
		{
			OutError = FString::Printf(TEXT("Could not load SoundClass '%s'"), *ClassPath);
			return false;
		}
		OutAdjuster.SoundClassObject = Cast<USoundClass>(Loaded);
	}

	double DVal;
	if (Json->TryGetNumberField(TEXT("VolumeAdjuster"), DVal)) OutAdjuster.VolumeAdjuster = static_cast<float>(DVal);
	if (Json->TryGetNumberField(TEXT("PitchAdjuster"), DVal)) OutAdjuster.PitchAdjuster = static_cast<float>(DVal);
	if (Json->TryGetNumberField(TEXT("LowPassFilterFrequency"), DVal)) OutAdjuster.LowPassFilterFrequency = static_cast<float>(DVal);

	bool bVal;
	if (Json->TryGetBoolField(TEXT("bApplyToChildren"), bVal)) OutAdjuster.bApplyToChildren = bVal;

	return true;
}

FMonolithActionResult FMonolithAudioAssetActions::CreateSoundMix(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundMix* Asset = CreateAudioAsset<USoundMixFactory, USoundMix>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	// Apply optional EQ settings
	const TSharedPtr<FJsonObject>* EqJson = nullptr;
	if (Params->TryGetObjectField(TEXT("eq_settings"), EqJson) && EqJson && EqJson->IsValid())
	{
		FString SetError;
		if (!JsonToStruct(*EqJson, FAudioEQEffect::StaticStruct(), &Asset->EQSettings, SetError))
		{
			UE_LOG(LogMonolith, Warning, TEXT("create_sound_mix: EQ settings error: %s"), *SetError);
		}
	}

	// Apply optional class effects
	const TArray<TSharedPtr<FJsonValue>>* ClassEffectsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("class_effects"), ClassEffectsArr) && ClassEffectsArr)
	{
		Asset->SoundClassEffects.Empty();
		for (const auto& Val : *ClassEffectsArr)
		{
			const TSharedPtr<FJsonObject>* AdjusterObj = nullptr;
			if (Val->TryGetObject(AdjusterObj) && AdjusterObj)
			{
				FSoundClassAdjuster Adjuster;
				FString AdjError;
				if (JsonToSoundClassAdjuster(*AdjusterObj, Adjuster, AdjError))
				{
					Asset->SoundClassEffects.Add(Adjuster);
				}
				else
				{
					UE_LOG(LogMonolith, Warning, TEXT("create_sound_mix: class effect error: %s"), *AdjError);
				}
			}
		}
	}

	// Timing fields
	double DVal;
	if (Params->TryGetNumberField(TEXT("initial_delay"), DVal)) Asset->InitialDelay = static_cast<float>(DVal);
	if (Params->TryGetNumberField(TEXT("fade_in_time"), DVal)) Asset->FadeInTime = static_cast<float>(DVal);
	if (Params->TryGetNumberField(TEXT("duration"), DVal)) Asset->Duration = static_cast<float>(DVal);
	if (Params->TryGetNumberField(TEXT("fade_out_time"), DVal)) Asset->FadeOutTime = static_cast<float>(DVal);

	Asset->GetPackage()->MarkPackageDirty();

	// Build result
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetObjectField(TEXT("eq_settings"), StructToJson(FAudioEQEffect::StaticStruct(), &Asset->EQSettings));

	TArray<TSharedPtr<FJsonValue>> AdjustersArr;
	for (const FSoundClassAdjuster& Adj : Asset->SoundClassEffects)
	{
		AdjustersArr.Add(MakeShared<FJsonValueObject>(SoundClassAdjusterToJson(Adj)));
	}
	Result->SetArrayField(TEXT("class_effects"), AdjustersArr);

	Result->SetNumberField(TEXT("initial_delay"), Asset->InitialDelay);
	Result->SetNumberField(TEXT("fade_in_time"), Asset->FadeInTime);
	Result->SetNumberField(TEXT("duration"), Asset->Duration);
	Result->SetNumberField(TEXT("fade_out_time"), Asset->FadeOutTime);
	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAudioAssetActions::GetSoundMixSettings(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundMix* Asset = LoadAudioAsset<USoundMix>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetObjectField(TEXT("eq_settings"), StructToJson(FAudioEQEffect::StaticStruct(), &Asset->EQSettings));

	TArray<TSharedPtr<FJsonValue>> AdjustersArr;
	for (const FSoundClassAdjuster& Adj : Asset->SoundClassEffects)
	{
		AdjustersArr.Add(MakeShared<FJsonValueObject>(SoundClassAdjusterToJson(Adj)));
	}
	Result->SetArrayField(TEXT("class_effects"), AdjustersArr);

	Result->SetNumberField(TEXT("initial_delay"), Asset->InitialDelay);
	Result->SetNumberField(TEXT("fade_in_time"), Asset->FadeInTime);
	Result->SetNumberField(TEXT("duration"), Asset->Duration);
	Result->SetNumberField(TEXT("fade_out_time"), Asset->FadeOutTime);
	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAudioAssetActions::SetSoundMixSettings(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundMix* Asset = LoadAudioAsset<USoundMix>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	Asset->Modify();

	// EQ settings
	const TSharedPtr<FJsonObject>* EqJson = nullptr;
	if (Params->TryGetObjectField(TEXT("eq_settings"), EqJson) && EqJson && EqJson->IsValid())
	{
		if (!JsonToStruct(*EqJson, FAudioEQEffect::StaticStruct(), &Asset->EQSettings, Error))
		{
			return FMonolithActionResult::Error(Error);
		}
	}

	// Class effects (replaces entire array)
	const TArray<TSharedPtr<FJsonValue>>* ClassEffectsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("class_effects"), ClassEffectsArr) && ClassEffectsArr)
	{
		Asset->SoundClassEffects.Empty();
		for (const auto& Val : *ClassEffectsArr)
		{
			const TSharedPtr<FJsonObject>* AdjusterObj = nullptr;
			if (Val->TryGetObject(AdjusterObj) && AdjusterObj)
			{
				FSoundClassAdjuster Adjuster;
				FString AdjError;
				if (JsonToSoundClassAdjuster(*AdjusterObj, Adjuster, AdjError))
				{
					Asset->SoundClassEffects.Add(Adjuster);
				}
				else
				{
					return FMonolithActionResult::Error(FString::Printf(TEXT("class_effects error: %s"), *AdjError));
				}
			}
		}
	}

	// Timing fields
	double DVal;
	if (Params->TryGetNumberField(TEXT("initial_delay"), DVal)) Asset->InitialDelay = static_cast<float>(DVal);
	if (Params->TryGetNumberField(TEXT("fade_in_time"), DVal)) Asset->FadeInTime = static_cast<float>(DVal);
	if (Params->TryGetNumberField(TEXT("duration"), DVal)) Asset->Duration = static_cast<float>(DVal);
	if (Params->TryGetNumberField(TEXT("fade_out_time"), DVal)) Asset->FadeOutTime = static_cast<float>(DVal);

	Asset->PostEditChange();
	Asset->GetPackage()->MarkPackageDirty();

	// Return updated state
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetObjectField(TEXT("eq_settings"), StructToJson(FAudioEQEffect::StaticStruct(), &Asset->EQSettings));

	TArray<TSharedPtr<FJsonValue>> AdjustersArr;
	for (const FSoundClassAdjuster& Adj : Asset->SoundClassEffects)
	{
		AdjustersArr.Add(MakeShared<FJsonValueObject>(SoundClassAdjusterToJson(Adj)));
	}
	Result->SetArrayField(TEXT("class_effects"), AdjustersArr);

	Result->SetNumberField(TEXT("initial_delay"), Asset->InitialDelay);
	Result->SetNumberField(TEXT("fade_in_time"), Asset->FadeInTime);
	Result->SetNumberField(TEXT("duration"), Asset->Duration);
	Result->SetNumberField(TEXT("fade_out_time"), Asset->FadeOutTime);
	return FMonolithActionResult::Success(Result);
}

// ============================================================================
// Sound Concurrency
// ============================================================================

FMonolithActionResult FMonolithAudioAssetActions::CreateSoundConcurrency(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundConcurrency* Asset = CreateAudioAsset<USoundConcurrencyFactory, USoundConcurrency>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	// Apply optional settings
	const TSharedPtr<FJsonObject>* SettingsJson = nullptr;
	if (Params->TryGetObjectField(TEXT("settings"), SettingsJson) && SettingsJson && SettingsJson->IsValid())
	{
		FString SetError;
		if (!JsonToStruct(*SettingsJson, FSoundConcurrencySettings::StaticStruct(), &Asset->Concurrency, SetError))
		{
			UE_LOG(LogMonolith, Warning, TEXT("create_sound_concurrency: settings error: %s"), *SetError);
		}
		Asset->GetPackage()->MarkPackageDirty();
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetObjectField(TEXT("settings"), StructToJson(FSoundConcurrencySettings::StaticStruct(), &Asset->Concurrency));
	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAudioAssetActions::GetConcurrencySettings(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundConcurrency* Asset = LoadAudioAsset<USoundConcurrency>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetObjectField(TEXT("settings"), StructToJson(FSoundConcurrencySettings::StaticStruct(), &Asset->Concurrency));
	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAudioAssetActions::SetConcurrencySettings(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	const TSharedPtr<FJsonObject>* SettingsJson = nullptr;
	if (!Params->TryGetObjectField(TEXT("settings"), SettingsJson) || !SettingsJson || !SettingsJson->IsValid())
	{
		return FMonolithActionResult::Error(TEXT("settings object is required"));
	}

	FString Error;
	USoundConcurrency* Asset = LoadAudioAsset<USoundConcurrency>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	Asset->Modify();
	if (!JsonToStruct(*SettingsJson, FSoundConcurrencySettings::StaticStruct(), &Asset->Concurrency, Error))
	{
		return FMonolithActionResult::Error(Error);
	}
	Asset->PostEditChange();
	Asset->GetPackage()->MarkPackageDirty();

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetObjectField(TEXT("settings"), StructToJson(FSoundConcurrencySettings::StaticStruct(), &Asset->Concurrency));
	return FMonolithActionResult::Success(Result);
}

// ============================================================================
// Sound Submix
// ============================================================================

FMonolithActionResult FMonolithAudioAssetActions::CreateSoundSubmix(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundSubmix* Asset = CreateAudioAsset<USoundSubmixFactory, USoundSubmix>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	// Set parent submix if specified
	FString ParentPath;
	if (Params->TryGetStringField(TEXT("parent_submix"), ParentPath) && !ParentPath.IsEmpty())
	{
		FString LoadError;
		USoundSubmix* Parent = LoadAudioAsset<USoundSubmix>(ParentPath, LoadError);
		if (Parent)
		{
			Asset->ParentSubmix = Parent;
			// Add this as child of parent
			if (!Parent->ChildSubmixes.Contains(Asset))
			{
				Parent->ChildSubmixes.Add(Asset);
				Parent->GetPackage()->MarkPackageDirty();
			}
		}
		else
		{
			UE_LOG(LogMonolith, Warning, TEXT("create_sound_submix: could not load parent_submix '%s': %s"), *ParentPath, *LoadError);
		}
	}

	Asset->GetPackage()->MarkPackageDirty();

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetStringField(TEXT("parent_submix"), Asset->ParentSubmix ? Asset->ParentSubmix->GetPathName() : TEXT("None"));

	// Effect chain
	TArray<TSharedPtr<FJsonValue>> EffectChainArr;
	for (const auto& Effect : Asset->SubmixEffectChain)
	{
		EffectChainArr.Add(MakeShared<FJsonValueString>(Effect ? Effect->GetPathName() : TEXT("None")));
	}
	Result->SetArrayField(TEXT("effect_chain"), EffectChainArr);

	// Children
	TArray<TSharedPtr<FJsonValue>> ChildrenArr;
	for (const auto& Child : Asset->ChildSubmixes)
	{
		if (Child)
		{
			ChildrenArr.Add(MakeShared<FJsonValueString>(Child->GetPathName()));
		}
	}
	Result->SetArrayField(TEXT("child_submixes"), ChildrenArr);

	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAudioAssetActions::GetSubmixProperties(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	FString Error;
	USoundSubmix* Asset = LoadAudioAsset<USoundSubmix>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetStringField(TEXT("parent_submix"), Asset->ParentSubmix ? Asset->ParentSubmix->GetPathName() : TEXT("None"));

	// Effect chain
	TArray<TSharedPtr<FJsonValue>> EffectChainArr;
	for (const auto& Effect : Asset->SubmixEffectChain)
	{
		EffectChainArr.Add(MakeShared<FJsonValueString>(Effect ? Effect->GetPathName() : TEXT("None")));
	}
	Result->SetArrayField(TEXT("effect_chain"), EffectChainArr);

	// Children
	TArray<TSharedPtr<FJsonValue>> ChildrenArr;
	for (const auto& Child : Asset->ChildSubmixes)
	{
		if (Child)
		{
			ChildrenArr.Add(MakeShared<FJsonValueString>(Child->GetPathName()));
		}
	}
	Result->SetArrayField(TEXT("child_submixes"), ChildrenArr);

	// Output volume
	Result->SetNumberField(TEXT("output_volume_db"), Asset->OutputVolumeModulation.Value);

	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAudioAssetActions::SetSubmixProperties(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	const TSharedPtr<FJsonObject>* PropsJson = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropsJson) || !PropsJson || !PropsJson->IsValid())
	{
		return FMonolithActionResult::Error(TEXT("properties object is required"));
	}

	FString Error;
	USoundSubmix* Asset = LoadAudioAsset<USoundSubmix>(AssetPath, Error);
	if (!Asset)
	{
		return FMonolithActionResult::Error(Error);
	}

	Asset->Modify();

	// Parent submix
	FString ParentPath;
	if ((*PropsJson)->TryGetStringField(TEXT("parent_submix"), ParentPath))
	{
		if (ParentPath.IsEmpty() || ParentPath == TEXT("None"))
		{
			// Remove from current parent's children
			if (Asset->ParentSubmix)
			{
				Asset->ParentSubmix->ChildSubmixes.Remove(Asset);
				Asset->ParentSubmix->GetPackage()->MarkPackageDirty();
			}
			Asset->ParentSubmix = nullptr;
		}
		else
		{
			FString LoadError;
			USoundSubmix* NewParent = LoadAudioAsset<USoundSubmix>(ParentPath, LoadError);
			if (NewParent)
			{
				// Remove from old parent
				if (Asset->ParentSubmix && Asset->ParentSubmix != NewParent)
				{
					Asset->ParentSubmix->ChildSubmixes.Remove(Asset);
					Asset->ParentSubmix->GetPackage()->MarkPackageDirty();
				}
				Asset->ParentSubmix = NewParent;
				if (!NewParent->ChildSubmixes.Contains(Asset))
				{
					NewParent->ChildSubmixes.Add(Asset);
					NewParent->GetPackage()->MarkPackageDirty();
				}
			}
			else
			{
				return FMonolithActionResult::Error(FString::Printf(TEXT("Could not load parent_submix '%s': %s"), *ParentPath, *LoadError));
			}
		}
	}

	// Output volume (modulation destination — set the Value field)
	double DVal;
	if ((*PropsJson)->TryGetNumberField(TEXT("output_volume"), DVal))
	{
		Asset->OutputVolumeModulation.Value = static_cast<float>(DVal);
	}

	Asset->PostEditChange();
	Asset->GetPackage()->MarkPackageDirty();

	// Return updated state
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetStringField(TEXT("parent_submix"), Asset->ParentSubmix ? Asset->ParentSubmix->GetPathName() : TEXT("None"));

	TArray<TSharedPtr<FJsonValue>> EffectChainArr;
	for (const auto& Effect : Asset->SubmixEffectChain)
	{
		EffectChainArr.Add(MakeShared<FJsonValueString>(Effect ? Effect->GetPathName() : TEXT("None")));
	}
	Result->SetArrayField(TEXT("effect_chain"), EffectChainArr);

	TArray<TSharedPtr<FJsonValue>> ChildrenArr;
	for (const auto& Child : Asset->ChildSubmixes)
	{
		if (Child)
		{
			ChildrenArr.Add(MakeShared<FJsonValueString>(Child->GetPathName()));
		}
	}
	Result->SetArrayField(TEXT("child_submixes"), ChildrenArr);
	Result->SetNumberField(TEXT("output_volume_db"), Asset->OutputVolumeModulation.Value);

	return FMonolithActionResult::Success(Result);
}
