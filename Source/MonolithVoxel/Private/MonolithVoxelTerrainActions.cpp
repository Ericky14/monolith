// Copyright (c) Monolith. Voxel Plugin 2 terrain-authoring actions (Phase 1).
//
// DRAFT — NOT YET COMPILED. Written against the VP2 source at Plugins/Voxel.
// Lines marked "// VERIFY:" use an enum value / include path / API name inferred from headers
// that must be confirmed by a build pass. Everything is #if WITH_EDITOR only.
//
// Adds mutation actions to the existing read-only `voxel` namespace (see MonolithVoxelActions.cpp):
//   voxel.create_voxel_world      - spawn + configure an AVoxelWorld, create its runtime
//   voxel.recreate_runtime        - destroy+recreate a world's runtime (regenerate)
//   voxel.duplicate_graph_template- copy a /Voxel/*Template graph into the project
//   voxel.add_height_stamp        - spawn an AVoxelStampActor from a Height Graph (drag-into-level equiv)
//   voxel.set_stamp_parameter     - set an exposed graph parameter on a placed stamp + refresh
//
// Design notes:
//  - The live editor is a single shared world; these run on the game thread inside the editor.
//  - VP2: a Voxel World renders NOTHING until a stamp exists, and its transform MUST be zero.
//    create_voxel_world therefore always spawns at the origin.

#include "MonolithVoxelActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithParamSchema.h"
#include "MonolithAssetUtils.h"
#include "Dom/JsonObject.h"

#if WITH_EDITOR
#include "Editor.h"                 // GEditor
#include "EngineUtils.h"            // TActorIterator
#include "Engine/World.h"
#include "AssetToolsModule.h"       // FAssetToolsModule (duplicate) - VERIFY: needs "AssetTools" in Build.cs
#include "IAssetTools.h"

#include "VoxelWorld.h"             // AVoxelWorld  (Voxel/Public)
#include "VoxelStampActor.h"        // AVoxelStampActor
#include "VoxelStampRef.h"          // FVoxelStampRef
#include "VoxelPinValue.h"          // FVoxelPinValue
#include "VoxelParameterOverridesOwner.h" // IVoxelParameterOverridesOwner::SetParameter

// Height-graph stamp construction path:
#include "Graphs/VoxelHeightGraph.h"          // UVoxelHeightGraph        - VERIFY: path
#include "Graphs/VoxelHeightGraphStamp.h"     // FVoxelHeightGraphStamp
#include "Graphs/VoxelHeightGraphStampRef.h"  // FVoxelHeightGraphStampRef
#include "Graphs/VoxelHeightGraphStamp_K2.h"  // UVoxelHeightGraphStamp_K2::Make
#include "VoxelHeightLayer.h"                  // UVoxelHeightLayer        - VERIFY: path

// Static-heightmap import + stamp path (Phase 2):
#include "Engine/Texture2D.h"                          // UTexture2D, TSF_G16
#include "Misc/FileHelper.h"                           // FFileHelper::LoadFileToArray
#include "UObject/Package.h"                           // CreatePackage
#include "AssetRegistry/AssetRegistryModule.h"         // FAssetRegistryModule::AssetCreated
#include "Heightmap/VoxelHeightmap.h"                  // UVoxelHeightmap
#include "Heightmap/VoxelHeightmap_Height.h"           // UVoxelHeightmap_Height, EVoxelTextureChannel
#include "Heightmap/VoxelHeightmapStamp.h"             // FVoxelHeightmapStamp, FVoxelHeightmapStampWeightmapSurfaceType
#include "Heightmap/VoxelHeightmapStampRef.h"          // FVoxelHeightmapStampRef
#include "Heightmap/VoxelHeightmapStamp_K2.h"          // UVoxelHeightmapStamp_K2::Make
#endif

#if WITH_EDITOR
namespace MonolithVoxelTerrainInternal
{
    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    // Find a placed actor by its editor label (preferred) or object name.
    template <typename TActor>
    TActor* FindActorByLabel(UWorld* World, const FString& Label)
    {
        if (!World || Label.IsEmpty())
            return nullptr;
        for (TActorIterator<TActor> It(World); It; ++It)
        {
            if (It->GetActorLabel() == Label || It->GetName() == Label)
                return *It;
        }
        return nullptr;
    }

    // Set a UObject* asset property by name via reflection, so we don't need the
    // concrete UVoxelMegaMaterial / UVoxelLayerStack headers here. Returns false on type mismatch.
    bool SetObjectPropertyByPath(UObject* Owner, FName PropName, const FString& AssetPath, FString& OutError)
    {
        if (AssetPath.IsEmpty())
            return true; // nothing to set
        UObject* Asset = FMonolithAssetUtils::LoadAssetByPath<UObject>(AssetPath);
        if (!Asset)
        {
            OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
            return false;
        }
        FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(Owner->GetClass()->FindPropertyByName(PropName));
        if (!Prop)
        {
            OutError = FString::Printf(TEXT("Property not found: %s"), *PropName.ToString());
            return false;
        }
        if (!Asset->IsA(Prop->PropertyClass))
        {
            OutError = FString::Printf(TEXT("Asset %s is not a %s"), *AssetPath, *Prop->PropertyClass->GetName());
            return false;
        }
        Prop->SetObjectPropertyValue_InContainer(Owner, Asset);
        return true;
    }

    // Build an FVoxelPinValue from a JSON value (float / bool / {x,y,z} vector).
    // Most graph params are float; extend as needed.
    bool MakePinValue(const TSharedPtr<FJsonValue>& JsonVal, FVoxelPinValue& OutValue, FString& OutError)
    {
        if (!JsonVal.IsValid())
        {
            OutError = TEXT("value is missing");
            return false;
        }
        if (JsonVal->Type == EJson::Boolean)
        {
            OutValue = FVoxelPinValue::Make(JsonVal->AsBool());
            return true;
        }
        if (JsonVal->Type == EJson::Number)
        {
            OutValue = FVoxelPinValue::Make(static_cast<float>(JsonVal->AsNumber())); // VERIFY: float vs double param types
            return true;
        }
        if (JsonVal->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject> Obj = JsonVal->AsObject();
            double X = 0, Y = 0, Z = 0;
            if (Obj->TryGetNumberField(TEXT("x"), X) &&
                Obj->TryGetNumberField(TEXT("y"), Y) &&
                Obj->TryGetNumberField(TEXT("z"), Z))
            {
                OutValue = FVoxelPinValue::Make(FVector(X, Y, Z));
                return true;
            }
        }
        OutError = TEXT("unsupported value type (expected number, bool, or {x,y,z})");
        return false;
    }
}
#endif // WITH_EDITOR

// --- create_voxel_world ---

static FMonolithActionResult HandleCreateVoxelWorld(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelTerrainInternal;
    UWorld* World = GetEditorWorld();
    if (!World)
        return FMonolithActionResult::Error(TEXT("No editor world (is a map open?)"));

    // VP2 requires the world transform to be zero.
    FActorSpawnParameters SpawnParams;
    AVoxelWorld* VW = World->SpawnActor<AVoxelWorld>(AVoxelWorld::StaticClass(), FTransform::Identity, SpawnParams);
    if (!VW)
        return FMonolithActionResult::Error(TEXT("Failed to spawn AVoxelWorld"));

    FString Label = Params->GetStringField(TEXT("label"));
    if (!Label.IsEmpty())
        VW->SetActorLabel(Label);

    // Scalar config (all confirmed public UPROPERTYs on AVoxelWorld)
    int32 VoxelSize = 0;
    if (Params->TryGetNumberField(TEXT("voxel_size"), VoxelSize) && VoxelSize > 0)
        VW->VoxelSize = VoxelSize;

    bool bBool = false;
    if (Params->TryGetBoolField(TEXT("enable_nanite"), bBool))
        VW->bEnableNanite = bBool;
    if (Params->TryGetBoolField(TEXT("enable_lumen"), bBool))
        VW->bEnableLumen = bBool;

    // Optional asset config via reflection (avoids concrete headers)
    FString Err;
    if (!SetObjectPropertyByPath(VW, TEXT("MegaMaterial"), Params->GetStringField(TEXT("mega_material")), Err))
        return FMonolithActionResult::Error(Err);
    if (!SetObjectPropertyByPath(VW, TEXT("LayerStack"), Params->GetStringField(TEXT("layer_stack")), Err))
        return FMonolithActionResult::Error(Err);

    VW->PostEditChange();
    VW->CreateRuntime(); // BlueprintCallable; safe to call in-editor
    VW->MarkPackageDirty();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("actor"), VW->GetActorLabel());
    Root->SetStringField(TEXT("actor_name"), VW->GetName());
    Root->SetNumberField(TEXT("voxel_size"), VW->VoxelSize);
    Root->SetBoolField(TEXT("runtime_created"), VW->IsRuntimeCreated());
    Root->SetStringField(TEXT("note"), TEXT("Voxel World renders nothing until a stamp is added (voxel.add_height_stamp)."));
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Requires editor build"));
#endif
}

// --- recreate_runtime (regenerate) ---

static FMonolithActionResult HandleRecreateRuntime(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelTerrainInternal;
    UWorld* World = GetEditorWorld();
    if (!World)
        return FMonolithActionResult::Error(TEXT("No editor world"));

    const FString Label = Params->GetStringField(TEXT("actor"));
    AVoxelWorld* VW = FindActorByLabel<AVoxelWorld>(World, Label);
    if (!VW)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel World not found: %s"), *Label));

    VW->DestroyRuntime();
    VW->CreateRuntime();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("actor"), VW->GetActorLabel());
    Root->SetBoolField(TEXT("runtime_created"), VW->IsRuntimeCreated());
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Requires editor build"));
#endif
}

// --- duplicate_graph_template ---

static FMonolithActionResult HandleDuplicateGraphTemplate(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    const FString SourcePath = Params->GetStringField(TEXT("source_path")); // e.g. /Voxel/HeightGraphTemplate
    const FString DestPackage = Params->GetStringField(TEXT("dest_package")); // e.g. /Game/SoulGame/Terrain
    const FString NewName = Params->GetStringField(TEXT("new_name"));         // e.g. VG_Reverie_Meadow
    if (SourcePath.IsEmpty() || DestPackage.IsEmpty() || NewName.IsEmpty())
        return FMonolithActionResult::Error(TEXT("source_path, dest_package, and new_name are required"));

    UObject* Src = FMonolithAssetUtils::LoadAssetByPath<UObject>(SourcePath);
    if (!Src)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Source not found: %s"), *SourcePath));

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
    UObject* Dup = AssetToolsModule.Get().DuplicateAsset(NewName, DestPackage, Src);
    if (!Dup)
        return FMonolithActionResult::Error(TEXT("DuplicateAsset failed (name already exists?)"));

    Dup->MarkPackageDirty();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("new_asset"), Dup->GetPathName());
    Root->SetStringField(TEXT("note"), TEXT("Not saved to disk yet - use editor.save_packages."));
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Requires editor build"));
#endif
}

// --- add_height_stamp ---

static FMonolithActionResult HandleAddHeightStamp(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelTerrainInternal;
    UWorld* World = GetEditorWorld();
    if (!World)
        return FMonolithActionResult::Error(TEXT("No editor world"));

    const FString GraphPath = Params->GetStringField(TEXT("graph_path"));
    if (GraphPath.IsEmpty())
        return FMonolithActionResult::Error(TEXT("graph_path is required (a UVoxelHeightGraph)"));

    UVoxelHeightGraph* Graph = FMonolithAssetUtils::LoadAssetByPath<UVoxelHeightGraph>(GraphPath);
    if (!Graph)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Height graph not found: %s"), *GraphPath));

    // Default height layer ships with the plugin.
    const FString LayerPath = Params->GetStringField(TEXT("layer_path")).IsEmpty()
        ? TEXT("/Voxel/Default/DefaultHeightLayer.DefaultHeightLayer") // VERIFY: exact object path
        : Params->GetStringField(TEXT("layer_path"));
    UVoxelHeightLayer* Layer = FMonolithAssetUtils::LoadAssetByPath<UVoxelHeightLayer>(LayerPath);

    // Placement (stamps CAN be moved - this is how you position terrain).
    double PX = 0, PY = 0, PZ = 0;
    Params->TryGetNumberField(TEXT("x"), PX);
    Params->TryGetNumberField(TEXT("y"), PY);
    Params->TryGetNumberField(TEXT("z"), PZ);
    const FTransform StampTransform(FVector(PX, PY, PZ));

    int32 Priority = 0;
    Params->TryGetNumberField(TEXT("priority"), Priority);

    // Build the stamp ref from the graph. Mirrors UVoxelHeightGraphStamp_K2::Make defaults.
    FVoxelHeightGraphStampRef StampRef;
    UVoxelHeightGraphStamp_K2::Make(
        StampRef,
        Graph,
        Layer,
        EVoxelHeightBlendMode::Override,   // Max | Min | Override (stamp defines terrain here)
        /*AdditionalLayers*/ {},
        /*HeightPaddingMultiplier*/ 0.1f,
        StampTransform,
        EVoxelStampBehavior::AffectAll,     // VERIFY: enum value name (meta default was "AffectAll")
        Priority,
        /*Smoothness*/ 100.f,
        /*MetadataOverrides*/ FVoxelMetadataOverrides(),
        /*StampSeed*/ FVoxelExposedSeed(),  // VERIFY: default-constructible
        /*LODRange*/ FInt32Interval(0, 32),
        /*bDisableStampSelection*/ false,
        /*bApplyOnVoid*/ true);

    if (!StampRef.IsValid())
        return FMonolithActionResult::Error(TEXT("Failed to build height graph stamp"));

    AVoxelStampActor* StampActor = World->SpawnActor<AVoxelStampActor>(AVoxelStampActor::StaticClass(), StampTransform);
    if (!StampActor)
        return FMonolithActionResult::Error(TEXT("Failed to spawn AVoxelStampActor"));

    const FString Label = Params->GetStringField(TEXT("label"));
    if (!Label.IsEmpty())
        StampActor->SetActorLabel(Label);

    StampActor->SetStamp(FVoxelStampRef(StampRef)); // FVoxelHeightGraphStampRef IS-A FVoxelStampRef
    StampActor->UpdateStamp();
    StampActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("actor"), StampActor->GetActorLabel());
    Root->SetStringField(TEXT("actor_name"), StampActor->GetName());
    Root->SetStringField(TEXT("graph"), Graph->GetPathName());
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Requires editor build"));
#endif
}

// --- set_stamp_parameter ---

static FMonolithActionResult HandleSetStampParameter(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelTerrainInternal;
    UWorld* World = GetEditorWorld();
    if (!World)
        return FMonolithActionResult::Error(TEXT("No editor world"));

    const FString Label = Params->GetStringField(TEXT("actor"));
    const FString ParamName = Params->GetStringField(TEXT("name"));
    if (Label.IsEmpty() || ParamName.IsEmpty())
        return FMonolithActionResult::Error(TEXT("actor and name are required"));

    AVoxelStampActor* StampActor = FindActorByLabel<AVoxelStampActor>(World, Label);
    if (!StampActor)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Stamp actor not found: %s"), *Label));

    FVoxelPinValue PinValue;
    FString Err;
    if (!MakePinValue(Params->TryGetField(TEXT("value")), PinValue, Err))
        return FMonolithActionResult::Error(FString::Printf(TEXT("value: %s"), *Err));

    // Cast the placed stamp to a height-graph stamp so we can reach its parameter overrides.
    FVoxelStampRef Ref = StampActor->GetStamp();
    FVoxelHeightGraphStamp* HeightStamp = Ref.As<FVoxelHeightGraphStamp>();
    if (!HeightStamp)
        return FMonolithActionResult::Error(TEXT("Stamp is not a Height Graph stamp (only height graphs supported in Phase 1)"));

    if (!HeightStamp->SetParameter(FName(*ParamName), PinValue, &Err))
        return FMonolithActionResult::Error(FString::Printf(TEXT("SetParameter failed: %s"), *Err));

    Ref.Update();                 // refresh the stamp data
    StampActor->UpdateStamp();    // refresh the actor/component
    StampActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("actor"), StampActor->GetActorLabel());
    Root->SetStringField(TEXT("parameter"), ParamName);
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Requires editor build"));
#endif
}

// --- import_heightmap (Phase 2: static 16-bit heightmap -> UVoxelHeightmap) ---
//
// Reads a raw little-endian unsigned 16-bit square heightmap (.r16) from disk, builds a G16
// grayscale UTexture2D asset, then a UVoxelHeightmap asset that references it with the given
// XY/Z scaling. This is the deterministic, re-runnable equivalent of the editor's
// "import RAW heightmap -> make Voxel Heightmap" flow. PNG 16-bit is also supported if it
// decodes to a square grayscale image, but .r16 is preferred (trivial, no decode).

static FMonolithActionResult HandleImportHeightmap(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    const FString RawPath = Params->GetStringField(TEXT("raw_path"));
    const FString DestPackage = Params->GetStringField(TEXT("dest_package"));
    const FString Name = Params->GetStringField(TEXT("name"));
    if (RawPath.IsEmpty() || DestPackage.IsEmpty() || Name.IsEmpty())
        return FMonolithActionResult::Error(TEXT("raw_path, dest_package, and name are required"));

    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *RawPath))
        return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to read raw file: %s"), *RawPath));
    if (Bytes.Num() % 2 != 0)
        return FMonolithActionResult::Error(TEXT("Raw file size is not a multiple of 2 (expected 16-bit R16)"));

    const int64 NumPixels = Bytes.Num() / 2;
    int32 Size = 0;
    Params->TryGetNumberField(TEXT("size"), Size);
    if (Size <= 0)
        Size = FMath::TruncToInt(FMath::Sqrt(double(NumPixels)));
    if (int64(Size) * int64(Size) != NumPixels)
        return FMonolithActionResult::Error(FString::Printf(
            TEXT("Raw file is not a square 16-bit heightmap (pixels=%lld, size=%d)"), NumPixels, Size));

    double ScaleXY = 100.0, ScaleZ = 40000.0, OffsetZ = 0.0;
    Params->TryGetNumberField(TEXT("scale_xy"), ScaleXY);
    Params->TryGetNumberField(TEXT("scale_z"), ScaleZ);
    Params->TryGetNumberField(TEXT("offset_z"), OffsetZ);

    // 1) G16 grayscale texture asset (mirrors UVoxelHeightmapRawFactory).
    FString TexName = Params->GetStringField(TEXT("texture_name"));
    if (TexName.IsEmpty())
        TexName = FString::Printf(TEXT("T_%s_Height"), *Name);
    const FString TexPkgPath = DestPackage / TexName;
    UPackage* TexPackage = CreatePackage(*TexPkgPath);
    if (!TexPackage)
        return FMonolithActionResult::Error(TEXT("Failed to create texture package"));
    UTexture2D* Texture = NewObject<UTexture2D>(TexPackage, *TexName, RF_Public | RF_Standalone);
    Texture->Source.Init(Size, Size, /*NumSlices*/ 1, /*NumMips*/ 1, TSF_G16, Bytes.GetData());
    Texture->SRGB = false;
    Texture->CompressionSettings = TC_Grayscale;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->NeverStream = true;
    Texture->PostEditChange();
    Texture->UpdateResource();
    FAssetRegistryModule::AssetCreated(Texture);
    Texture->MarkPackageDirty();

    // 2) UVoxelHeightmap asset referencing the texture.
    const FString HmPkgPath = DestPackage / Name;
    UPackage* HmPackage = CreatePackage(*HmPkgPath);
    if (!HmPackage)
        return FMonolithActionResult::Error(TEXT("Failed to create heightmap package"));
    UVoxelHeightmap* Heightmap = NewObject<UVoxelHeightmap>(HmPackage, *Name, RF_Public | RF_Standalone);
    Heightmap->ScaleXY = float(ScaleXY);

    UVoxelHeightmap_Height* Height = Heightmap->Height; // created as a default subobject in the ctor
    if (!Height)
        return FMonolithActionResult::Error(TEXT("UVoxelHeightmap has no Height subobject"));
    Height->Texture = Texture;
    Height->TextureChannel = EVoxelTextureChannel::R; // G16 single channel maps to R
    Height->ScaleZ = float(ScaleZ);
    Height->OffsetZ = float(OffsetZ);
    Height->bUseBicubic = true;

    // Force the height data to build now so it serializes with real RawData.
    Height->GetData();
    Heightmap->PostEditChange();
    FAssetRegistryModule::AssetCreated(Heightmap);
    Heightmap->MarkPackageDirty();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("heightmap"), Heightmap->GetPathName());
    Root->SetStringField(TEXT("texture"), Texture->GetPathName());
    Root->SetNumberField(TEXT("size"), Size);
    Root->SetNumberField(TEXT("scale_xy"), ScaleXY);
    Root->SetNumberField(TEXT("scale_z"), ScaleZ);
    Root->SetNumberField(TEXT("offset_z"), OffsetZ);
    Root->SetNumberField(TEXT("world_size_m"), (Size * ScaleXY) / 100.0);
    Root->SetStringField(TEXT("note"), TEXT("Not saved to disk yet - use editor.save_packages on both packages, then voxel.add_heightmap_stamp."));
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Requires editor build"));
#endif
}

// --- add_heightmap_stamp (Phase 2: place a static-heightmap stamp) ---

static FMonolithActionResult HandleAddHeightmapStamp(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelTerrainInternal;
    UWorld* World = GetEditorWorld();
    if (!World)
        return FMonolithActionResult::Error(TEXT("No editor world"));

    const FString HeightmapPath = Params->GetStringField(TEXT("heightmap_path"));
    if (HeightmapPath.IsEmpty())
        return FMonolithActionResult::Error(TEXT("heightmap_path is required (a UVoxelHeightmap)"));

    UVoxelHeightmap* Heightmap = FMonolithAssetUtils::LoadAssetByPath<UVoxelHeightmap>(HeightmapPath);
    if (!Heightmap)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Heightmap not found: %s"), *HeightmapPath));

    const FString LayerPath = Params->GetStringField(TEXT("layer_path")).IsEmpty()
        ? TEXT("/Voxel/Default/DefaultHeightLayer.DefaultHeightLayer")
        : Params->GetStringField(TEXT("layer_path"));
    UVoxelHeightLayer* Layer = FMonolithAssetUtils::LoadAssetByPath<UVoxelHeightLayer>(LayerPath);

    double PX = 0, PY = 0, PZ = 0;
    Params->TryGetNumberField(TEXT("x"), PX);
    Params->TryGetNumberField(TEXT("y"), PY);
    Params->TryGetNumberField(TEXT("z"), PZ);
    const FTransform StampTransform(FVector(PX, PY, PZ));

    int32 Priority = 0;
    Params->TryGetNumberField(TEXT("priority"), Priority);
    double Smoothness = 100.0;
    Params->TryGetNumberField(TEXT("smoothness"), Smoothness);

    FVoxelHeightmapStampRef StampRef;
    UVoxelHeightmapStamp_K2::Make(
        StampRef,
        Heightmap,
        /*DefaultSurfaceType*/ nullptr,
        /*WeightmapSurfaceTypes*/ {},
        Layer,
        EVoxelHeightBlendMode::Override,   // this stamp defines the terrain here
        /*AdditionalLayers*/ {},
        /*HeightPaddingMultiplier*/ 0.1f,
        StampTransform,
        EVoxelStampBehavior::AffectAll,
        Priority,
        float(Smoothness),
        /*MetadataOverrides*/ FVoxelMetadataOverrides(),
        /*StampSeed*/ FVoxelExposedSeed(),
        /*LODRange*/ FInt32Interval(0, 32),
        /*bDisableStampSelection*/ false,
        /*bApplyOnVoid*/ true);

    if (!StampRef.IsValid())
        return FMonolithActionResult::Error(TEXT("Failed to build heightmap stamp"));

    AVoxelStampActor* StampActor = World->SpawnActor<AVoxelStampActor>(AVoxelStampActor::StaticClass(), StampTransform);
    if (!StampActor)
        return FMonolithActionResult::Error(TEXT("Failed to spawn AVoxelStampActor"));

    const FString Label = Params->GetStringField(TEXT("label"));
    if (!Label.IsEmpty())
        StampActor->SetActorLabel(Label);

    StampActor->SetStamp(FVoxelStampRef(StampRef));
    StampActor->UpdateStamp();
    StampActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("actor"), StampActor->GetActorLabel());
    Root->SetStringField(TEXT("actor_name"), StampActor->GetName());
    Root->SetStringField(TEXT("heightmap"), Heightmap->GetPathName());
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Requires editor build"));
#endif
}

// --- Registration (called from FMonolithVoxelActions::RegisterActions) ---

void FMonolithVoxelActions::RegisterTerrainActions()
{
    FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

    Registry.RegisterAction(TEXT("voxel"), TEXT("create_voxel_world"),
        TEXT("Spawn and configure an AVoxelWorld at the origin (VP2 requires transform=0) and create its runtime. Renders nothing until a stamp is added."),
        FMonolithActionHandler::CreateStatic(&HandleCreateVoxelWorld),
        FParamSchemaBuilder()
            .Optional(TEXT("label"), TEXT("string"), TEXT("Actor label in the outliner"))
            .Optional(TEXT("voxel_size"), TEXT("number"), TEXT("Voxel size in cm (resolution/cost dial; default 100)"))
            .Optional(TEXT("enable_nanite"), TEXT("boolean"), TEXT("Nanite rendering (default true)"))
            .Optional(TEXT("enable_lumen"), TEXT("boolean"), TEXT("Lumen GI (default false)"))
            .Optional(TEXT("mega_material"), TEXT("string"), TEXT("UVoxelMegaMaterial asset path"))
            .Optional(TEXT("layer_stack"), TEXT("string"), TEXT("UVoxelLayerStack asset path"))
            .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("recreate_runtime"),
        TEXT("Destroy and recreate a Voxel World's runtime (force a regenerate)."),
        FMonolithActionHandler::CreateStatic(&HandleRecreateRuntime),
        FParamSchemaBuilder()
            .Required(TEXT("actor"), TEXT("string"), TEXT("Voxel World actor label or name"))
            .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("duplicate_graph_template"),
        TEXT("Duplicate a Voxel graph template (e.g. /Voxel/HeightGraphTemplate) into the project as a new asset."),
        FMonolithActionHandler::CreateStatic(&HandleDuplicateGraphTemplate),
        FParamSchemaBuilder()
            .Required(TEXT("source_path"), TEXT("string"), TEXT("Template graph path, e.g. /Voxel/HeightGraphTemplate"))
            .Required(TEXT("dest_package"), TEXT("string"), TEXT("Destination package, e.g. /Game/SoulGame/Terrain"))
            .Required(TEXT("new_name"), TEXT("string"), TEXT("New asset name, e.g. VG_Reverie_Meadow"))
            .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("add_height_stamp"),
        TEXT("Spawn an AVoxelStampActor from a Height Graph (equivalent to dragging the graph into the level). Terrain generates where the stamp is."),
        FMonolithActionHandler::CreateStatic(&HandleAddHeightStamp),
        FParamSchemaBuilder()
            .Required(TEXT("graph_path"), TEXT("string"), TEXT("UVoxelHeightGraph asset path"))
            .Optional(TEXT("label"), TEXT("string"), TEXT("Stamp actor label"))
            .Optional(TEXT("layer_path"), TEXT("string"), TEXT("UVoxelHeightLayer (default: DefaultHeightLayer)"))
            .Optional(TEXT("x"), TEXT("number"), TEXT("Stamp X location"))
            .Optional(TEXT("y"), TEXT("number"), TEXT("Stamp Y location"))
            .Optional(TEXT("z"), TEXT("number"), TEXT("Stamp Z location (raise/lower terrain)"))
            .Optional(TEXT("priority"), TEXT("number"), TEXT("Blend priority within the layer"))
            .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("set_stamp_parameter"),
        TEXT("Set an exposed graph parameter (noise scale, height, seed...) on a placed Height Graph stamp and refresh it."),
        FMonolithActionHandler::CreateStatic(&HandleSetStampParameter),
        FParamSchemaBuilder()
            .Required(TEXT("actor"), TEXT("string"), TEXT("Stamp actor label or name"))
            .Required(TEXT("name"), TEXT("string"), TEXT("Exposed parameter name (see voxel.get_parameters)"))
            .Required(TEXT("value"), TEXT("any"), TEXT("Value: number, boolean, or {x,y,z}"))
            .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("import_heightmap"),
        TEXT("Import a raw 16-bit square heightmap (.r16, little-endian unsigned) into a G16 UTexture2D + a UVoxelHeightmap asset. Deterministic, re-runnable. Then save + add_heightmap_stamp."),
        FMonolithActionHandler::CreateStatic(&HandleImportHeightmap),
        FParamSchemaBuilder()
            .Required(TEXT("raw_path"), TEXT("string"), TEXT("Absolute path to the .r16 file on disk"))
            .Required(TEXT("dest_package"), TEXT("string"), TEXT("Destination package folder, e.g. /Game/Aetherkin/World/Lumenhollow/Terrain"))
            .Required(TEXT("name"), TEXT("string"), TEXT("New UVoxelHeightmap asset name, e.g. VH_Lumenhollow"))
            .Optional(TEXT("texture_name"), TEXT("string"), TEXT("G16 texture asset name (default T_<name>_Height)"))
            .Optional(TEXT("size"), TEXT("number"), TEXT("Heightmap side length in px (default: inferred as sqrt(bytes/2))"))
            .Optional(TEXT("scale_xy"), TEXT("number"), TEXT("cm per pixel (default 100 = 1 m/px)"))
            .Optional(TEXT("scale_z"), TEXT("number"), TEXT("cm at normalized height 1.0 (default 40000 = 400 m)"))
            .Optional(TEXT("offset_z"), TEXT("number"), TEXT("Z offset in cm (default 0)"))
            .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("add_heightmap_stamp"),
        TEXT("Spawn an AVoxelStampActor from a static UVoxelHeightmap (import_heightmap output). Terrain generates where the stamp is placed."),
        FMonolithActionHandler::CreateStatic(&HandleAddHeightmapStamp),
        FParamSchemaBuilder()
            .Required(TEXT("heightmap_path"), TEXT("string"), TEXT("UVoxelHeightmap asset path"))
            .Optional(TEXT("label"), TEXT("string"), TEXT("Stamp actor label"))
            .Optional(TEXT("layer_path"), TEXT("string"), TEXT("UVoxelHeightLayer (default: DefaultHeightLayer)"))
            .Optional(TEXT("x"), TEXT("number"), TEXT("Stamp X location"))
            .Optional(TEXT("y"), TEXT("number"), TEXT("Stamp Y location"))
            .Optional(TEXT("z"), TEXT("number"), TEXT("Stamp Z location (raise/lower terrain)"))
            .Optional(TEXT("priority"), TEXT("number"), TEXT("Blend priority within the layer"))
            .Optional(TEXT("smoothness"), TEXT("number"), TEXT("Blend smoothness (default 100)"))
            .Build());
}
