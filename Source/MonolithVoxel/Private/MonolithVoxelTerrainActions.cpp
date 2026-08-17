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

// Volume-graph stamp construction path (tunnels/caves - true 3D SDF carving):
#include "Graphs/VoxelVolumeGraph.h"          // UVoxelVolumeGraph
#include "Graphs/VoxelVolumeGraphStamp.h"     // FVoxelVolumeGraphStamp
#include "Graphs/VoxelVolumeGraphStampRef.h"  // FVoxelVolumeGraphStampRef
#include "Graphs/VoxelVolumeGraphStamp_K2.h"  // UVoxelVolumeGraphStamp_K2::Make
#include "VoxelVolumeLayer.h"                  // UVoxelVolumeLayer
#include "VoxelVolumeBlendMode.h"              // EVoxelVolumeBlendMode

// Static-heightmap import + stamp path (Phase 2):
#include "Engine/Texture2D.h"                          // UTexture2D, TSF_G16
#include "Misc/FileHelper.h"                           // FFileHelper::LoadFileToArray
#include "UObject/Package.h"                           // CreatePackage
#include "AssetRegistry/AssetRegistryModule.h"         // FAssetRegistryModule::AssetCreated
#include "Heightmap/VoxelHeightmap.h"                  // UVoxelHeightmap
#include "Heightmap/VoxelHeightmap_Height.h"           // UVoxelHeightmap_Height, EVoxelTextureChannel
#include "Heightmap/VoxelHeightmap_Weight.h"           // UVoxelHeightmap_Weight
#include "Heightmap/VoxelHeightmapStamp.h"             // FVoxelHeightmapStamp, FVoxelHeightmapStampWeightmapSurfaceType
#include "Heightmap/VoxelHeightmapStampRef.h"          // FVoxelHeightmapStampRef
#include "Heightmap/VoxelHeightmapStamp_K2.h"          // UVoxelHeightmapStamp_K2::Make
#include "Surface/VoxelSurfaceTypeInterface.h"         // UVoxelSurfaceTypeInterface

// Instanced-mesh scatter (foliage/rocks/props) — the saveable HISM path Python can't do:
#include "Engine/StaticMesh.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Serialization/JsonSerializer.h"              // FJsonSerializer + TJsonReaderFactory
#include "GameFramework/Actor.h"
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

// --- add_volume_stamp ---
//
// Volume stamps are how anything a heightfield cannot express gets carved: tunnels,
// caves, overhangs. Subtractive blend = SmoothMax(Old, -New, Smoothness) - true SDF
// difference, and unlike height/Override stamps the Smoothness IS live here.

static FMonolithActionResult HandleAddVolumeStamp(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelTerrainInternal;
    UWorld* World = GetEditorWorld();
    if (!World)
        return FMonolithActionResult::Error(TEXT("No editor world"));

    const FString GraphPath = Params->GetStringField(TEXT("graph_path"));
    if (GraphPath.IsEmpty())
        return FMonolithActionResult::Error(TEXT("graph_path is required (a UVoxelVolumeGraph)"));

    UVoxelVolumeGraph* Graph = FMonolithAssetUtils::LoadAssetByPath<UVoxelVolumeGraph>(GraphPath);
    if (!Graph)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Volume graph not found: %s"), *GraphPath));

    const FString LayerPath = Params->GetStringField(TEXT("layer_path")).IsEmpty()
        ? TEXT("/Voxel/Default/DefaultVolumeLayer.DefaultVolumeLayer") // path from the K2 Make meta
        : Params->GetStringField(TEXT("layer_path"));
    UVoxelVolumeLayer* Layer = FMonolithAssetUtils::LoadAssetByPath<UVoxelVolumeLayer>(LayerPath);
    if (!Layer)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Volume layer not found: %s"), *LayerPath));

    // Blend mode by name - Subtractive is the tunnel/cave case and the default.
    FString BlendStr = Params->GetStringField(TEXT("blend_mode"));
    if (BlendStr.IsEmpty()) BlendStr = TEXT("Subtractive");
    EVoxelVolumeBlendMode BlendMode;
    if (BlendStr == TEXT("Subtractive"))     BlendMode = EVoxelVolumeBlendMode::Subtractive;
    else if (BlendStr == TEXT("Additive"))   BlendMode = EVoxelVolumeBlendMode::Additive;
    else if (BlendStr == TEXT("Intersect"))  BlendMode = EVoxelVolumeBlendMode::Intersect;
    else if (BlendStr == TEXT("Override"))   BlendMode = EVoxelVolumeBlendMode::Override;
    else
        return FMonolithActionResult::Error(TEXT("blend_mode must be Subtractive|Additive|Intersect|Override"));

    // Full placement: position + yaw + non-uniform scale. A tunnel is a unit shape in
    // the graph, oriented along its bearing and sized by the stamp transform.
    double PX = 0, PY = 0, PZ = 0, Yaw = 0, SX = 1, SY = 1, SZ = 1;
    Params->TryGetNumberField(TEXT("x"), PX);
    Params->TryGetNumberField(TEXT("y"), PY);
    Params->TryGetNumberField(TEXT("z"), PZ);
    Params->TryGetNumberField(TEXT("yaw"), Yaw);
    Params->TryGetNumberField(TEXT("scale_x"), SX);
    Params->TryGetNumberField(TEXT("scale_y"), SY);
    Params->TryGetNumberField(TEXT("scale_z"), SZ);
    const FTransform StampTransform(FRotator(0, Yaw, 0), FVector(PX, PY, PZ), FVector(SX, SY, SZ));

    int32 Priority = 0;
    Params->TryGetNumberField(TEXT("priority"), Priority);
    double Smoothness = 100.0;
    Params->TryGetNumberField(TEXT("smoothness"), Smoothness);

    // Research REC: tunnels default to LODRange {0,3}. Past LOD3 (8 m cells at
    // VoxelSize 100) the bore cannot be resolved anyway - it seals shut - so
    // evaluating the stamp there is pure waste AND causes a visible mouth pop.
    int32 LODMin = 0, LODMax = 3;
    Params->TryGetNumberField(TEXT("lod_min"), LODMin);
    Params->TryGetNumberField(TEXT("lod_max"), LODMax);

    // bApplyOnVoid semantics (learned the expensive way): "void" means "no previous
    // stamp IN THIS LAYER". A Subtractive bore in an otherwise-empty volume layer
    // with bApplyOnVoid=false applies NOWHERE - the terrain comes from the HEIGHT
    // layer, which does not count. VP2's own K2 default is true; keep it.
    bool bApplyOnVoid = true;
    Params->TryGetBoolField(TEXT("apply_on_void"), bApplyOnVoid);

    FVoxelVolumeGraphStampRef StampRef;
    UVoxelVolumeGraphStamp_K2::Make(
        StampRef,
        Graph,
        Layer,
        BlendMode,
        /*AdditionalLayers*/ {},
        /*BoundsExtensionMultiplier*/ 1.0f,
        /*MaximumBoundsExtension*/ 1000.0f,
        StampTransform,
        EVoxelStampBehavior::AffectAll,
        Priority,
        (float)Smoothness,
        /*MetadataOverrides*/ FVoxelMetadataOverrides(),
        /*StampSeed*/ FVoxelExposedSeed(),
        FInt32Interval(LODMin, LODMax),
        /*bDisableStampSelection*/ false,
        /*bApplyOnVoid*/ bApplyOnVoid);

    if (!StampRef.IsValid())
        return FMonolithActionResult::Error(TEXT("Failed to build volume graph stamp"));

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
    Root->SetStringField(TEXT("graph"), Graph->GetPathName());
    Root->SetStringField(TEXT("blend_mode"), BlendStr);
    Root->SetNumberField(TEXT("lod_min"), LODMin);
    Root->SetNumberField(TEXT("lod_max"), LODMax);
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

// --- add_heightmap_weight (Phase 2: mask -> surface-type weightmap layer) ---
//
// Appends a UVoxelHeightmap_Weight to a UVoxelHeightmap: a grayscale mask texture that
// paints a Surface Type onto the terrain where the mask is white (AlphaBlended over the
// base DefaultSurfaceType). This is how the Lumenhollow 13 control masks drive per-zone
// materials (cliff rock, road cobble, wet bank). Weights is a public but non-EditAnywhere
// UPROPERTY, so it can only be populated from C++ (not editor Python).

static FMonolithActionResult HandleAddHeightmapWeight(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    const FString HmPath = Params->GetStringField(TEXT("heightmap_path"));
    const FString MaskPath = Params->GetStringField(TEXT("mask_texture_path"));
    const FString StPath = Params->GetStringField(TEXT("surface_type_path"));
    if (HmPath.IsEmpty() || MaskPath.IsEmpty() || StPath.IsEmpty())
        return FMonolithActionResult::Error(TEXT("heightmap_path, mask_texture_path, and surface_type_path are required"));

    UVoxelHeightmap* Hm = FMonolithAssetUtils::LoadAssetByPath<UVoxelHeightmap>(HmPath);
    if (!Hm)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Heightmap not found: %s"), *HmPath));
    UTexture2D* Mask = FMonolithAssetUtils::LoadAssetByPath<UTexture2D>(MaskPath);
    if (!Mask)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Mask texture not found: %s"), *MaskPath));
    UVoxelSurfaceTypeInterface* St = FMonolithAssetUtils::LoadAssetByPath<UVoxelSurfaceTypeInterface>(StPath);
    if (!St)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Surface type not found: %s"), *StPath));

    bool bClear = false;
    if (Params->TryGetBoolField(TEXT("clear_existing"), bClear) && bClear)
        Hm->Weights.Reset();

    double Strength = 1.0;
    Params->TryGetNumberField(TEXT("strength"), Strength);
    bool bBicubic = false;
    Params->TryGetBoolField(TEXT("use_bicubic"), bBicubic);

    UVoxelHeightmap_Weight* W = NewObject<UVoxelHeightmap_Weight>(Hm);
    W->Texture = Mask;
    W->TextureChannel = EVoxelTextureChannel::R;
    W->SurfaceType = St;
    W->Strength = float(Strength);
    W->bUseBicubic = bBicubic;
    W->GetData(); // force-build so weight data serializes

    Hm->Weights.Add(W);
    Hm->MarkPackageDirty();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("weight_index"), Hm->Weights.Num() - 1);
    Root->SetNumberField(TEXT("total_weights"), Hm->Weights.Num());
    Root->SetStringField(TEXT("surface_type"), St->GetName());
    Root->SetStringField(TEXT("note"), TEXT("Not saved yet - editor.save_packages the heightmap, then voxel.recreate_runtime."));
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Requires editor build"));
#endif
}

// --- scatter_instanced (instanced foliage/props via a saveable HISM) ---
//
// Builds a UHierarchicalInstancedStaticMeshComponent from a JSON file of transforms and
// bulk-adds all instances. This is the correct C++ component lifecycle (NewObject ->
// SetRootComponent -> RegisterComponent -> AddInstanceComponent -> AddInstances) that the
// editor Python API does NOT expose (no RegisterComponent/AddInstanceComponent/add_component_
// by_class), so dense instanced scatter (grass, flowers, rocks) can only be done here.
// transforms_path -> JSON array of {x,y,z, yaw?, pitch?, roll?, s? | sx,sy,sz?}. World-space.

static FMonolithActionResult HandleScatterInstanced(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelTerrainInternal;
    UWorld* World = GetEditorWorld();
    if (!World)
        return FMonolithActionResult::Error(TEXT("No editor world"));

    const FString MeshPath = Params->GetStringField(TEXT("mesh_path"));
    const FString TransformsPath = Params->GetStringField(TEXT("transforms_path"));
    if (MeshPath.IsEmpty() || TransformsPath.IsEmpty())
        return FMonolithActionResult::Error(TEXT("mesh_path and transforms_path are required"));

    UStaticMesh* Mesh = FMonolithAssetUtils::LoadAssetByPath<UStaticMesh>(MeshPath);
    if (!Mesh)
        return FMonolithActionResult::Error(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));

    FString JsonStr;
    if (!FFileHelper::LoadFileToString(JsonStr, *TransformsPath))
        return FMonolithActionResult::Error(FString::Printf(TEXT("Cannot read transforms_path: %s"), *TransformsPath));

    TArray<TSharedPtr<FJsonValue>> Arr;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(Reader, Arr))
        return FMonolithActionResult::Error(TEXT("transforms_path must be a JSON array of {x,y,z,...}"));

    TArray<FTransform> Instances;
    Instances.Reserve(Arr.Num());
    for (const TSharedPtr<FJsonValue>& V : Arr)
    {
        const TSharedPtr<FJsonObject>* Obj;
        if (!V.IsValid() || !V->TryGetObject(Obj))
            continue;
        double X = 0, Y = 0, Z = 0, Yaw = 0, Pitch = 0, Roll = 0, S = 1, Sx = 0, Sy = 0, Sz = 0;
        (*Obj)->TryGetNumberField(TEXT("x"), X);
        (*Obj)->TryGetNumberField(TEXT("y"), Y);
        (*Obj)->TryGetNumberField(TEXT("z"), Z);
        (*Obj)->TryGetNumberField(TEXT("yaw"), Yaw);
        (*Obj)->TryGetNumberField(TEXT("pitch"), Pitch);
        (*Obj)->TryGetNumberField(TEXT("roll"), Roll);
        (*Obj)->TryGetNumberField(TEXT("s"), S);
        const bool bNonUniform = (*Obj)->TryGetNumberField(TEXT("sx"), Sx) && (*Obj)->TryGetNumberField(TEXT("sy"), Sy) && (*Obj)->TryGetNumberField(TEXT("sz"), Sz);
        const FVector Scale = bNonUniform ? FVector(Sx, Sy, Sz) : FVector(S, S, S);
        Instances.Add(FTransform(FRotator(Pitch, Yaw, Roll), FVector(X, Y, Z), Scale));
    }
    if (Instances.Num() == 0)
        return FMonolithActionResult::Error(TEXT("No transforms parsed from JSON"));

    AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
    if (!Actor)
        return FMonolithActionResult::Error(TEXT("Failed to spawn scatter actor"));

    UHierarchicalInstancedStaticMeshComponent* HISM =
        NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor, TEXT("ScatterHISM"));
    HISM->SetStaticMesh(Mesh);

    bool bCollision = false;
    Params->TryGetBoolField(TEXT("collision"), bCollision);
    HISM->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    bool bCastShadow = true;
    Params->TryGetBoolField(TEXT("cast_shadow"), bCastShadow);
    HISM->SetCastShadow(bCastShadow);

    Actor->SetRootComponent(HISM);
    HISM->OnComponentCreated();
    HISM->RegisterComponent();
    Actor->AddInstanceComponent(HISM);

    HISM->AddInstances(Instances, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);

    const FString Label = Params->GetStringField(TEXT("label"));
    if (!Label.IsEmpty())
        Actor->SetActorLabel(Label);
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("actor"), Actor->GetActorLabel());
    Root->SetStringField(TEXT("actor_name"), Actor->GetName());
    Root->SetNumberField(TEXT("instances"), HISM->GetInstanceCount());
    Root->SetStringField(TEXT("mesh"), Mesh->GetPathName());
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

    Registry.RegisterAction(TEXT("voxel"), TEXT("add_volume_stamp"),
        TEXT("Spawn an AVoxelStampActor from a VOLUME Graph (3D SDF - tunnels/caves/overhangs a heightfield cannot express). Default blend Subtractive, LODRange {0,3} (bores seal shut past LOD3 anyway)."),
        FMonolithActionHandler::CreateStatic(&HandleAddVolumeStamp),
        FParamSchemaBuilder()
            .Required(TEXT("graph_path"), TEXT("string"), TEXT("UVoxelVolumeGraph asset path"))
            .Optional(TEXT("label"), TEXT("string"), TEXT("Stamp actor label"))
            .Optional(TEXT("layer_path"), TEXT("string"), TEXT("UVoxelVolumeLayer (default: DefaultVolumeLayer)"))
            .Optional(TEXT("blend_mode"), TEXT("string"), TEXT("Subtractive (default) | Additive | Intersect | Override"))
            .Optional(TEXT("x"), TEXT("number"), TEXT("Stamp X location (cm)"))
            .Optional(TEXT("y"), TEXT("number"), TEXT("Stamp Y location (cm)"))
            .Optional(TEXT("z"), TEXT("number"), TEXT("Stamp Z location (cm)"))
            .Optional(TEXT("yaw"), TEXT("number"), TEXT("Yaw degrees (orient a bore along its bearing)"))
            .Optional(TEXT("scale_x"), TEXT("number"), TEXT("X scale (default 1)"))
            .Optional(TEXT("scale_y"), TEXT("number"), TEXT("Y scale (default 1)"))
            .Optional(TEXT("scale_z"), TEXT("number"), TEXT("Z scale (default 1)"))
            .Optional(TEXT("priority"), TEXT("number"), TEXT("Blend priority within the volume layer"))
            .Optional(TEXT("smoothness"), TEXT("number"), TEXT("SDF blend radius cm (default 100 - LIVE for volume stamps)"))
            .Optional(TEXT("lod_min"), TEXT("number"), TEXT("Min LOD (default 0)"))
            .Optional(TEXT("lod_max"), TEXT("number"), TEXT("Max LOD (default 3)"))
            .Optional(TEXT("apply_on_void"), TEXT("boolean"), TEXT("Default true. 'Void' = no previous stamp in THIS layer - false makes a lone Subtractive bore apply NOWHERE (height-layer terrain does not count)"))
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

    Registry.RegisterAction(TEXT("voxel"), TEXT("scatter_instanced"),
        TEXT("Build a saveable HierarchicalInstancedStaticMeshComponent from a JSON file of world-space transforms (bulk instances). The instanced-mesh scatter path editor Python can't do. transforms_path JSON = array of {x,y,z, yaw?, pitch?, roll?, s? | sx,sy,sz?}."),
        FMonolithActionHandler::CreateStatic(&HandleScatterInstanced),
        FParamSchemaBuilder()
            .Required(TEXT("mesh_path"), TEXT("string"), TEXT("UStaticMesh asset path to instance"))
            .Required(TEXT("transforms_path"), TEXT("string"), TEXT("Absolute path to a JSON array file of instance transforms (world-space)"))
            .Optional(TEXT("label"), TEXT("string"), TEXT("Scatter actor label"))
            .Optional(TEXT("collision"), TEXT("boolean"), TEXT("Query collision on instances (default false)"))
            .Optional(TEXT("cast_shadow"), TEXT("boolean"), TEXT("Instances cast shadows (default true)"))
            .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("add_heightmap_weight"),
        TEXT("Append a weightmap layer to a UVoxelHeightmap: a grayscale mask texture that paints a Surface Type onto the terrain where white (over the base DefaultSurfaceType). Populates the C++-only Weights array. Then save + recreate_runtime."),
        FMonolithActionHandler::CreateStatic(&HandleAddHeightmapWeight),
        FParamSchemaBuilder()
            .Required(TEXT("heightmap_path"), TEXT("string"), TEXT("UVoxelHeightmap asset path"))
            .Required(TEXT("mask_texture_path"), TEXT("string"), TEXT("Grayscale mask UTexture2D path"))
            .Required(TEXT("surface_type_path"), TEXT("string"), TEXT("UVoxelSurfaceTypeAsset/Interface path to paint where the mask is white"))
            .Optional(TEXT("strength"), TEXT("number"), TEXT("Weight strength (default 1.0)"))
            .Optional(TEXT("use_bicubic"), TEXT("boolean"), TEXT("Bicubic mask sampling (default false)"))
            .Optional(TEXT("clear_existing"), TEXT("boolean"), TEXT("Empty the Weights array first (default false)"))
            .Build());
}
