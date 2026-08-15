// Copyright (c) Monolith. Voxel Plugin 2 ground-height query, sourced from HEIGHTMAP DATA.
//
// voxel.query_height — answers "what is the ground Z at this world XY?" by reading the
// UVoxelHeightmap asset + its stamp transform, NEVER by tracing the physics scene.
//
// Why not a trace: VP2 runtime collision is invoker-driven. On a 4 km stamped map only the
// chunks near a collision invoker have physics bodies, so in the editor a downward trace either
// hits an unrelated blockout actor hundreds of metres up (and reports "success"), or hits nothing
// at all a few hundred metres from the origin. Every placement primitive built on a trace
// inherits that wrong answer. This action bypasses the physics scene entirely: the heightmap
// texture data is fully resident once the stamp exists, so it answers correctly everywhere the
// stamp covers, regardless of which chunks happen to have collision.
//
// Sampling mirrors VP2's own mesher path so the returned Z matches the rendered surface:
//   Voxel/Private/Heightmap/VoxelHeightmapStampImpl.isph  (world -> local -> texel, the guard band)
//   Voxel/Private/Heightmap/VoxelSampleHeightmap.isph      (bilinear + Catmull-Rom bicubic weights)
//   Voxel/Private/Heightmap/VoxelHeightmapStamp.cpp        (CombineScaleAndOffset, /MAX_uint16)
// i.e. world XY -> FVoxelHeightTransform::InverseTransform -> /ScaleXY + Size/2 -> interpolate
// -> value * FinalScaleZ + FinalOffsetZ -> TransformHeight -> ClampHeight.
//
// Query frame: VP2 requires the AVoxelWorld transform to be zero, so query space == world space
// and we pass Identity for WorldToQuery. If a Voxel World is found with a non-identity transform
// the result carries a warning, because then the rendered terrain no longer sits at these Zs.

#include "MonolithVoxelActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithParamSchema.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if WITH_EDITOR
#include "Editor.h"                 // GEditor
#include "EngineUtils.h"            // TActorIterator
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "VoxelWorld.h"                                // AVoxelWorld (query-frame sanity check)
#include "VoxelStampComponent.h"                       // UVoxelStampComponent::GetStamp
#include "VoxelStampTransform.h"                       // FVoxelHeightTransform
#include "VoxelHeightLayer.h"                          // UVoxelHeightLayer (name reporting)
#include "Heightmap/VoxelHeightmap.h"                  // UVoxelHeightmap
#include "Heightmap/VoxelHeightmap_Height.h"           // UVoxelHeightmap_Height::GetData
#include "Heightmap/VoxelHeightmap_HeightData.h"       // FVoxelHeightmap_HeightData
#include "Heightmap/VoxelHeightmapStamp.h"             // FVoxelHeightmapStamp
#endif

#if WITH_EDITOR
namespace MonolithVoxelHeightQuery
{
    // Hard ceiling on grid samples regardless of what the caller asks for. A 4 km map at 1 m
    // steps would be 16M samples; this keeps one call bounded and the JSON response sane.
    constexpr int32 AbsoluteMaxSamples = 40000;
    constexpr int32 DefaultMaxSamples = 4096;

    // One resolvable heightmap stamp in the level, flattened to everything sampling needs.
    struct FHeightStampEntry
    {
        FString ActorLabel;
        FString ActorName;
        FString HeightmapPath;
        FString LayerName;

        TSharedPtr<const FVoxelHeightmap_HeightData> Data;
        const uint16* U16 = nullptr;   // exactly one of U16/F32 is set, per Data->bIsUINT16
        const float* F32 = nullptr;

        FVoxelHeightTransform Transform;
        FVoxelBox WorldBounds;

        float ScaleXY = 100.f;
        float FinalScaleZ = 1.f;       // already folded: internal scale * asset ScaleZ, /MAX_uint16 for uint16 data
        float FinalOffsetZ = 0.f;
        bool bUseBicubic = true;
        int32 Priority = 0;
        EVoxelHeightBlendMode BlendMode = EVoxelHeightBlendMode::Override;
    };

    struct FGroundSample
    {
        bool bHit = false;
        double Z = 0.0;
        int32 StampIndex = INDEX_NONE;
        int32 NumContributors = 0;
    };

    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    TSharedPtr<FJsonValue> MakeVec(const double X, const double Y)
    {
        TArray<TSharedPtr<FJsonValue>> Arr;
        Arr.Add(MakeShared<FJsonValueNumber>(X));
        Arr.Add(MakeShared<FJsonValueNumber>(Y));
        return MakeShared<FJsonValueArray>(Arr);
    }
    TSharedPtr<FJsonValue> MakeVec(const double X, const double Y, const double Z)
    {
        TArray<TSharedPtr<FJsonValue>> Arr;
        Arr.Add(MakeShared<FJsonValueNumber>(X));
        Arr.Add(MakeShared<FJsonValueNumber>(Y));
        Arr.Add(MakeShared<FJsonValueNumber>(Z));
        return MakeShared<FJsonValueArray>(Arr);
    }

    FORCEINLINE float Texel(const FHeightStampEntry& Entry, const int32 X, const int32 Y)
    {
        const int64 Index = int64(X) + int64(Entry.Data->SizeX) * int64(Y);
        return Entry.U16 ? float(Entry.U16[Index]) : Entry.F32[Index];
    }

    // Catmull-Rom weights, byte-for-byte the p0..p3 multiplier polynomials in
    // VoxelSampleHeightmap.isph (the X and Y multipliers there expand to the same cubic).
    FORCEINLINE void CubicWeights(const float A, float(&OutW)[4])
    {
        OutW[0] = ((-0.5f * A + 1.0f) * A - 0.5f) * A;
        OutW[1] = ((1.5f * A - 2.5f) * A) * A + 1.0f;
        OutW[2] = ((-1.5f * A + 2.0f) * A + 0.5f) * A;
        OutW[3] = ((0.5f * A - 0.5f) * A) * A;
    }

    // Sample ONE stamp. Returns false when the point falls outside the heightmap footprint
    // (including VP2's 1-texel guard band, which the mesher also refuses to sample).
    bool SampleStamp(const FHeightStampEntry& Entry, const double WorldX, const double WorldY, float& OutZ)
    {
        const FVector2D LocalPosition = Entry.Transform.InverseTransform(FVector2D(WorldX, WorldY));

        const int32 SizeX = Entry.Data->SizeX;
        const int32 SizeY = Entry.Data->SizeY;

        const double PosX = LocalPosition.X / double(Entry.ScaleXY) + SizeX / 2.0;
        const double PosY = LocalPosition.Y / double(Entry.ScaleXY) + SizeY / 2.0;

        // Same guard as VoxelHeightmapStampImpl.isph — bicubic needs a 1-texel skirt on each side.
        if (PosX < 1.0 || PosY < 1.0 || PosX > SizeX - 2.0001 || PosY > SizeY - 2.0001)
        {
            return false;
        }

        const double MinXf = FMath::Floor(PosX);
        const double MinYf = FMath::Floor(PosY);
        const int32 MinX = int32(MinXf);
        const int32 MinY = int32(MinYf);
        const float AlphaX = float(FMath::Clamp(PosX - MinXf, 0.0, 1.0));
        const float AlphaY = float(FMath::Clamp(PosY - MinYf, 0.0, 1.0));

        float Raw;
        if (Entry.bUseBicubic)
        {
            float WX[4];
            float WY[4];
            CubicWeights(AlphaX, WX);
            CubicWeights(AlphaY, WY);

            Raw = 0.f;
            for (int32 Row = 0; Row < 4; Row++)
            {
                float RowValue = 0.f;
                for (int32 Col = 0; Col < 4; Col++)
                {
                    RowValue += WX[Col] * Texel(Entry, MinX - 1 + Col, MinY - 1 + Row);
                }
                Raw += WY[Row] * RowValue;
            }
        }
        else
        {
            const float V00 = Texel(Entry, MinX + 0, MinY + 0);
            const float V10 = Texel(Entry, MinX + 1, MinY + 0);
            const float V01 = Texel(Entry, MinX + 0, MinY + 1);
            const float V11 = Texel(Entry, MinX + 1, MinY + 1);
            Raw = FMath::Lerp(FMath::Lerp(V00, V10, AlphaX), FMath::Lerp(V01, V11, AlphaX), AlphaY);
        }

        if (!FMath::IsFinite(Raw))
        {
            return false;
        }

        // Height math stays in float on purpose: the mesher stores heights as float, so matching
        // its precision is what makes this Z agree with the rendered surface.
        float Height = Raw * Entry.FinalScaleZ + Entry.FinalOffsetZ;
        Height = Entry.Transform.TransformHeight<float>(Height, LocalPosition);
        Height = Entry.Transform.ClampHeight<float>(Height);

        if (!FMath::IsFinite(Height))
        {
            return false;
        }

        OutZ = Height;
        return true;
    }

    // Resolve the ground across every stamp covering the point.
    //
    // Multi-stamp rule (documented in the action description): stamps whose XY footprint contains
    // the point are applied in ascending Priority, mirroring the runtime's "higher priority applied
    // last"; Override replaces, Max takes the max, Min takes the min. Layer ordering and per-stamp
    // Smoothness are NOT modelled, so a result within `Smoothness` cm of a seam between two stamps
    // is a hard blend where the mesher would round it off. NumContributors > 1 flags that case.
    FGroundSample ResolveGround(const TArray<FHeightStampEntry>& Stamps, const double X, const double Y)
    {
        FGroundSample Out;
        for (int32 Index = 0; Index < Stamps.Num(); Index++)
        {
            float Z = 0.f;
            if (!SampleStamp(Stamps[Index], X, Y, Z))
            {
                continue;
            }

            Out.NumContributors++;

            if (!Out.bHit)
            {
                Out.bHit = true;
                Out.Z = Z;
                Out.StampIndex = Index;
                continue;
            }

            switch (Stamps[Index].BlendMode)
            {
            case EVoxelHeightBlendMode::Override:
                Out.Z = Z;
                Out.StampIndex = Index;
                break;
            case EVoxelHeightBlendMode::Max:
                if (Z > Out.Z)
                {
                    Out.Z = Z;
                    Out.StampIndex = Index;
                }
                break;
            case EVoxelHeightBlendMode::Min:
                if (Z < Out.Z)
                {
                    Out.Z = Z;
                    Out.StampIndex = Index;
                }
                break;
            }
        }
        return Out;
    }

    // XY distance from a point to a stamp's world AABB. Only used to explain a miss.
    double DistanceToFootprint(const FHeightStampEntry& Entry, const double X, const double Y)
    {
        const double DX = FMath::Max3(Entry.WorldBounds.Min.X - X, 0.0, X - Entry.WorldBounds.Max.X);
        const double DY = FMath::Max3(Entry.WorldBounds.Min.Y - Y, 0.0, Y - Entry.WorldBounds.Max.Y);
        return FMath::Sqrt(DX * DX + DY * DY);
    }

    // Walk the level and flatten every heightmap stamp we can actually sample.
    // OutSkipped explains stamps we found but had to drop, so "no data" is never silent.
    void CollectHeightStamps(
        UWorld* World,
        const FString& ActorFilter,
        TArray<FHeightStampEntry>& OutStamps,
        TArray<FString>& OutSkipped,
        TArray<FString>& OutWarnings)
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor))
            {
                continue;
            }

            TArray<UVoxelStampComponent*> Components;
            Actor->GetComponents<UVoxelStampComponent>(Components);
            if (Components.Num() == 0)
            {
                continue;
            }

            const FString Label = Actor->GetActorLabel();
            if (!ActorFilter.IsEmpty() &&
                Label != ActorFilter &&
                Actor->GetName() != ActorFilter)
            {
                continue;
            }

            for (UVoxelStampComponent* Component : Components)
            {
                if (!IsValid(Component))
                {
                    continue;
                }

                const FVoxelStampRef StampRef = Component->GetStamp();
                if (!StampRef.IsValid())
                {
                    continue;
                }

                FVoxelHeightmapStamp* Stamp = StampRef.As<FVoxelHeightmapStamp>();
                if (!Stamp)
                {
                    // Height-GRAPH stamps evaluate a procedural graph inside the mesher and expose
                    // no CPU-side height data, so they cannot be sampled here. Volume/sculpt stamps
                    // aren't heightfields at all. Surface these so their terrain isn't silently missing.
                    if (StampRef.IsA<FVoxelHeightStamp>())
                    {
                        OutSkipped.Add(FString::Printf(
                            TEXT("%s: %s is not a static-heightmap stamp (procedural height stamps have no sampleable data)"),
                            *Label,
                            StampRef.GetStruct() ? *StampRef.GetStruct()->GetName() : TEXT("stamp")));
                    }
                    continue;
                }

                // A stamp that doesn't affect shape contributes surface types or metadata only.
                if (!EnumHasAllFlags(Stamp->Behavior, EVoxelStampBehavior::AffectShape))
                {
                    OutSkipped.Add(FString::Printf(TEXT("%s: Behavior does not include AffectShape"), *Label));
                    continue;
                }
                if (!Stamp->LODRange.Contains(0))
                {
                    OutSkipped.Add(FString::Printf(TEXT("%s: LODRange excludes LOD 0"), *Label));
                    continue;
                }

                UVoxelHeightmap* Heightmap = Stamp->Heightmap;
                if (!Heightmap || !Heightmap->Height)
                {
                    OutSkipped.Add(FString::Printf(TEXT("%s: no UVoxelHeightmap assigned"), *Label));
                    continue;
                }

                // GetData() rebuilds from the source texture through the DDC if the cached data is
                // stale; on a loaded, rendering map it is already resident and this is a lookup.
                const TSharedPtr<const FVoxelHeightmap_HeightData> Data = Heightmap->Height->GetData();
                if (!Data || Data->SizeX < 4 || Data->SizeY < 4 || Data->RawData.Num() == 0)
                {
                    OutSkipped.Add(FString::Printf(TEXT("%s: heightmap has no built height data"), *Label));
                    continue;
                }

                FHeightStampEntry Entry;
                Entry.ActorLabel = Label;
                Entry.ActorName = Actor->GetName();
                Entry.HeightmapPath = Heightmap->GetPathName();
                if (Stamp->Layer)
                {
                    Entry.LayerName = Stamp->Layer->GetName();
                }
                Entry.Data = Data;
                Entry.ScaleXY = Heightmap->ScaleXY;
                Entry.bUseBicubic = Heightmap->Height->bUseBicubic;
                Entry.Priority = Stamp->Priority;
                Entry.BlendMode = Stamp->BlendMode;

                if (!(Entry.ScaleXY > 0.f))
                {
                    OutSkipped.Add(FString::Printf(TEXT("%s: heightmap ScaleXY is %g"), *Label, Entry.ScaleXY));
                    continue;
                }

                FVoxelUtilities::CombineScaleAndOffset(
                    Data->InternalScaleZ,
                    Data->InternalOffsetZ,
                    Heightmap->Height->ScaleZ,
                    Heightmap->Height->OffsetZ,
                    Entry.FinalScaleZ,
                    Entry.FinalOffsetZ);

                if (Data->bIsUINT16)
                {
                    Entry.FinalScaleZ /= MAX_uint16;
                    Entry.U16 = Data->RawData.View<uint16>().GetData();
                }
                else
                {
                    Entry.F32 = Data->RawData.View<float>().GetData();
                }

                // The component transform is what the editor shows and what the stamp syncs to
                // (UVoxelStampComponentBase::UpdateStamp writes Stamp->Transform from it). Warn if
                // the sync hasn't landed, since the mesher uses Stamp->Transform.
                const FTransform StampToWorld = Component->GetComponentTransform();
                if (!Stamp->Transform.Equals(StampToWorld))
                {
                    OutWarnings.Add(FString::Printf(
                        TEXT("%s: stamp transform is out of sync with its component; terrain may still be at the old transform"),
                        *Label));
                }

                // Local Z range of what sampling can actually return. Deliberately NOT
                // FVoxelHeightmapStampRuntime::GetLocalBounds(): that one applies the asset ScaleZ
                // without the /MAX_uint16 that the sampler applies, so for 16-bit-compressed
                // heightmaps its Z range is 65535x too tall. Using the sampler's own scale keeps
                // the clamp band (MinHeight/MaxHeight) honest.
                const double DataMinZ = double(Data->DataMin) * Entry.FinalScaleZ + Entry.FinalOffsetZ;
                const double DataMaxZ = double(Data->DataMax) * Entry.FinalScaleZ + Entry.FinalOffsetZ;

                const FVector2D HalfSize(Data->SizeX / 2.0, Data->SizeY / 2.0);
                const FVoxelBox LocalBounds = FVoxelBox2D(-HalfSize, HalfSize)
                    .Scale(double(Entry.ScaleXY))
                    .ToBox3D(FMath::Min(DataMinZ, DataMaxZ), FMath::Max(DataMinZ, DataMaxZ));

                Entry.Transform = FVoxelHeightTransform::Create(
                    StampToWorld,
                    FTransform::Identity, // query space == world space; VP2 pins the Voxel World to transform 0
                    LocalBounds,
                    Stamp->HeightPaddingMultiplier);

                Entry.WorldBounds = Entry.Transform.GetBounds(LocalBounds);

                OutStamps.Add(MoveTemp(Entry));
            }
        }

        // Runtime applies higher-priority stamps last; match that ordering for the blend.
        OutStamps.Sort([](const FHeightStampEntry& A, const FHeightStampEntry& B)
        {
            return A.Priority < B.Priority;
        });
    }

    TSharedPtr<FJsonObject> StampToJson(const FHeightStampEntry& Entry)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("actor"), Entry.ActorLabel);
        Obj->SetStringField(TEXT("actor_name"), Entry.ActorName);
        Obj->SetStringField(TEXT("heightmap"), Entry.HeightmapPath);
        if (!Entry.LayerName.IsEmpty())
        {
            Obj->SetStringField(TEXT("layer"), Entry.LayerName);
        }
        Obj->SetField(TEXT("size_px"), MakeVec(Entry.Data->SizeX, Entry.Data->SizeY));
        Obj->SetNumberField(TEXT("scale_xy"), Entry.ScaleXY);
        Obj->SetNumberField(TEXT("priority"), Entry.Priority);
        Obj->SetStringField(TEXT("blend_mode"),
            Entry.BlendMode == EVoxelHeightBlendMode::Override ? TEXT("Override") :
            Entry.BlendMode == EVoxelHeightBlendMode::Max ? TEXT("Max") : TEXT("Min"));
        Obj->SetBoolField(TEXT("bicubic"), Entry.bUseBicubic);
        Obj->SetBoolField(TEXT("uint16_data"), Entry.Data->bIsUINT16);
        Obj->SetField(TEXT("world_bounds_min"), MakeVec(Entry.WorldBounds.Min.X, Entry.WorldBounds.Min.Y, Entry.WorldBounds.Min.Z));
        Obj->SetField(TEXT("world_bounds_max"), MakeVec(Entry.WorldBounds.Max.X, Entry.WorldBounds.Max.Y, Entry.WorldBounds.Max.Z));
        return Obj;
    }

    // Miss payload: never emits a z, so a caller can never mistake "no data" for "ground at 0".
    void FillMiss(
        const TSharedPtr<FJsonObject>& Obj,
        const TArray<FHeightStampEntry>& Stamps,
        const double X,
        const double Y)
    {
        Obj->SetBoolField(TEXT("hit"), false);
        Obj->SetStringField(TEXT("reason"), Stamps.Num() == 0
            ? TEXT("no_heightmap_stamp_in_level")
            : TEXT("outside_heightmap_footprint"));

        int32 NearestIndex = INDEX_NONE;
        double NearestDistance = TNumericLimits<double>::Max();
        for (int32 Index = 0; Index < Stamps.Num(); Index++)
        {
            const double Distance = DistanceToFootprint(Stamps[Index], X, Y);
            if (Distance < NearestDistance)
            {
                NearestDistance = Distance;
                NearestIndex = Index;
            }
        }
        if (NearestIndex != INDEX_NONE)
        {
            Obj->SetStringField(TEXT("nearest_stamp"), Stamps[NearestIndex].ActorLabel);
            Obj->SetNumberField(TEXT("distance_to_footprint_cm"), NearestDistance);
        }
    }

    // Normal from finite differences of the resolved ground. Returns false if a neighbour missed,
    // in which case the caller omits the normal rather than guessing a flat one.
    bool NormalFromSlopes(
        const bool bHasX, const double DzDx,
        const bool bHasY, const double DzDy,
        FVector& OutNormal, double& OutSlopeDeg)
    {
        if (!bHasX || !bHasY)
        {
            return false;
        }
        OutNormal = FVector(-DzDx, -DzDy, 1.0).GetSafeNormal();
        if (OutNormal.IsNearlyZero())
        {
            return false;
        }
        OutSlopeDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(OutNormal.Z, -1.0, 1.0)));
        return true;
    }
}
#endif // WITH_EDITOR

// --- query_height ---

static FMonolithActionResult HandleQueryHeight(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelHeightQuery;

    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return FMonolithActionResult::Error(TEXT("No editor world (is a map open?)"));
    }

    // --- mode selection -----------------------------------------------------
    double PointX = 0, PointY = 0;
    const bool bHasX = Params->TryGetNumberField(TEXT("world_x"), PointX);
    const bool bHasY = Params->TryGetNumberField(TEXT("world_y"), PointY);

    double CenterX = 0, CenterY = 0;
    bool bHasCenter = false;
    const TArray<TSharedPtr<FJsonValue>>* CenterArray = nullptr;
    if (Params->TryGetArrayField(TEXT("center"), CenterArray) && CenterArray->Num() >= 2)
    {
        CenterX = (*CenterArray)[0]->AsNumber();
        CenterY = (*CenterArray)[1]->AsNumber();
        bHasCenter = true;
    }
    else
    {
        const TSharedPtr<FJsonObject>* CenterObject = nullptr;
        if (Params->TryGetObjectField(TEXT("center"), CenterObject) &&
            (*CenterObject)->TryGetNumberField(TEXT("x"), CenterX) &&
            (*CenterObject)->TryGetNumberField(TEXT("y"), CenterY))
        {
            bHasCenter = true;
        }
    }

    if (bHasCenter && (bHasX || bHasY))
    {
        return FMonolithActionResult::Error(TEXT("Pass either {world_x, world_y} (point) or {center, extent, step} (grid), not both"));
    }
    if (!bHasCenter && !(bHasX && bHasY))
    {
        return FMonolithActionResult::Error(TEXT("Required: {world_x, world_y} for a single sample, or {center:[x,y], extent, step} for a grid"));
    }

    double Extent = 0, Step = 0;
    if (bHasCenter)
    {
        if (!Params->TryGetNumberField(TEXT("extent"), Extent) || !(Extent > 0))
        {
            return FMonolithActionResult::Error(TEXT("extent (cm, half-size of the square grid) is required and must be > 0"));
        }
        if (!Params->TryGetNumberField(TEXT("step"), Step) || !(Step > 0))
        {
            return FMonolithActionResult::Error(TEXT("step (cm between samples) is required and must be > 0"));
        }
    }

    int32 MaxSamples = DefaultMaxSamples;
    Params->TryGetNumberField(TEXT("max_samples"), MaxSamples);
    MaxSamples = FMath::Clamp(MaxSamples, 4, AbsoluteMaxSamples);

    bool bIncludeNormals = true;
    Params->TryGetBoolField(TEXT("include_normals"), bIncludeNormals);

    double NormalDelta = 0;
    Params->TryGetNumberField(TEXT("normal_delta"), NormalDelta);

    // TryGet, not GetStringField: this action gets called in loops and GetStringField logs a
    // LogJson warning for every absent optional field.
    FString ActorFilter;
    Params->TryGetStringField(TEXT("stamp_actor"), ActorFilter);

    // --- gather stamps ------------------------------------------------------
    TArray<FString> Warnings;
    TArray<FString> Skipped;
    TArray<FHeightStampEntry> Stamps;
    CollectHeightStamps(World, ActorFilter, Stamps, Skipped, Warnings);

    int32 NumVoxelWorlds = 0;
    for (TActorIterator<AVoxelWorld> It(World); It; ++It)
    {
        NumVoxelWorlds++;
        if (!It->GetActorTransform().Equals(FTransform::Identity))
        {
            Warnings.Add(FString::Printf(
                TEXT("Voxel World '%s' is not at transform 0; VP2 renders terrain in that actor's frame, so these world Zs will not match the mesh"),
                *It->GetActorLabel()));
        }
    }
    if (NumVoxelWorlds == 0)
    {
        Warnings.Add(TEXT("No AVoxelWorld in the level: heights below come from the stamp data, but nothing is rendering or colliding"));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("source"), TEXT("heightmap_data"));
    Root->SetNumberField(TEXT("stamps_considered"), Stamps.Num());
    Root->SetNumberField(TEXT("voxel_worlds"), NumVoxelWorlds);

    TArray<TSharedPtr<FJsonValue>> StampArray;
    for (const FHeightStampEntry& Entry : Stamps)
    {
        StampArray.Add(MakeShared<FJsonValueObject>(StampToJson(Entry)));
    }
    Root->SetArrayField(TEXT("stamps"), StampArray);

    if (Skipped.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> SkippedArray;
        for (const FString& S : Skipped)
        {
            SkippedArray.Add(MakeShared<FJsonValueString>(S));
        }
        Root->SetArrayField(TEXT("skipped_stamps"), SkippedArray);
    }

    if (Stamps.Num() == 0 && !ActorFilter.IsEmpty())
    {
        Warnings.Add(FString::Printf(TEXT("stamp_actor '%s' matched no sampleable heightmap stamp"), *ActorFilter));
    }

    // --- point mode ---------------------------------------------------------
    if (!bHasCenter)
    {
        Root->SetStringField(TEXT("mode"), TEXT("point"));
        Root->SetField(TEXT("world_xy"), MakeVec(PointX, PointY));

        const FGroundSample Sample = ResolveGround(Stamps, PointX, PointY);
        if (!Sample.bHit)
        {
            FillMiss(Root, Stamps, PointX, PointY);
        }
        else
        {
            Root->SetBoolField(TEXT("hit"), true);
            Root->SetNumberField(TEXT("z"), Sample.Z);
            Root->SetStringField(TEXT("stamp"), Stamps[Sample.StampIndex].ActorLabel);
            Root->SetNumberField(TEXT("contributing_stamps"), Sample.NumContributors);
            if (Sample.NumContributors > 1)
            {
                Warnings.Add(TEXT("More than one stamp covers this point: blending is hard min/max/override here, the mesher additionally applies per-stamp Smoothness"));
            }

            if (bIncludeNormals)
            {
                // One texel of the resolving stamp is the natural probe distance: smaller and the
                // normal just reads interpolation noise.
                const double Delta = NormalDelta > 0
                    ? NormalDelta
                    : double(Stamps[Sample.StampIndex].ScaleXY);

                const FGroundSample XPlus = ResolveGround(Stamps, PointX + Delta, PointY);
                const FGroundSample XMinus = ResolveGround(Stamps, PointX - Delta, PointY);
                const FGroundSample YPlus = ResolveGround(Stamps, PointX, PointY + Delta);
                const FGroundSample YMinus = ResolveGround(Stamps, PointX, PointY - Delta);

                FVector Normal;
                double SlopeDeg = 0;
                if (NormalFromSlopes(
                        XPlus.bHit && XMinus.bHit, (XPlus.Z - XMinus.Z) / (2.0 * Delta),
                        YPlus.bHit && YMinus.bHit, (YPlus.Z - YMinus.Z) / (2.0 * Delta),
                        Normal, SlopeDeg))
                {
                    Root->SetField(TEXT("normal"), MakeVec(Normal.X, Normal.Y, Normal.Z));
                    Root->SetNumberField(TEXT("slope_deg"), SlopeDeg);
                    Root->SetNumberField(TEXT("normal_delta"), Delta);
                }
                else
                {
                    Root->SetStringField(TEXT("normal_note"), TEXT("Not computed: a neighbouring probe fell outside the heightmap footprint"));
                }
            }
        }

        if (Warnings.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> WarningArray;
            for (const FString& W : Warnings)
            {
                WarningArray.Add(MakeShared<FJsonValueString>(W));
            }
            Root->SetArrayField(TEXT("warnings"), WarningArray);
        }
        return FMonolithActionResult::Success(Root);
    }

    // --- grid mode ----------------------------------------------------------
    const double RequestedStep = Step;
    // Count in double first: a huge extent / tiny step would overflow int32 before the cap runs.
    const double RawCount = FMath::Floor((2.0 * Extent) / Step) + 1.0;
    int32 Count = int32(FMath::Clamp(RawCount, 2.0, double(AbsoluteMaxSamples)));
    bool bClamped = false;
    if (int64(Count) * int64(Count) > int64(MaxSamples))
    {
        // Preserve the requested extent (callers size nav bounds / spawn regions from it) and
        // coarsen the step instead of truncating coverage.
        Count = FMath::Max(2, FMath::FloorToInt(FMath::Sqrt(double(MaxSamples))));
        Step = (2.0 * Extent) / double(Count - 1);
        bClamped = true;
    }

    TArray<FGroundSample> Samples;
    Samples.SetNum(Count * Count);
    for (int32 IY = 0; IY < Count; IY++)
    {
        for (int32 IX = 0; IX < Count; IX++)
        {
            const double X = CenterX - Extent + IX * Step;
            const double Y = CenterY - Extent + IY * Step;
            Samples[IX + Count * IY] = ResolveGround(Stamps, X, Y);
        }
    }

    int32 NumHits = 0;
    int32 NumMultiStamp = 0;
    double ZMin = TNumericLimits<double>::Max();
    double ZMax = -TNumericLimits<double>::Max();
    double ZSum = 0;

    TArray<TSharedPtr<FJsonValue>> SampleArray;
    SampleArray.Reserve(Samples.Num());

    for (int32 IY = 0; IY < Count; IY++)
    {
        for (int32 IX = 0; IX < Count; IX++)
        {
            const int32 Index = IX + Count * IY;
            const FGroundSample& Sample = Samples[Index];
            const double X = CenterX - Extent + IX * Step;
            const double Y = CenterY - Extent + IY * Step;

            TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
            Obj->SetNumberField(TEXT("x"), X);
            Obj->SetNumberField(TEXT("y"), Y);
            Obj->SetNumberField(TEXT("ix"), IX);
            Obj->SetNumberField(TEXT("iy"), IY);

            if (!Sample.bHit)
            {
                FillMiss(Obj, Stamps, X, Y);
                SampleArray.Add(MakeShared<FJsonValueObject>(Obj));
                continue;
            }

            NumHits++;
            ZMin = FMath::Min(ZMin, Sample.Z);
            ZMax = FMath::Max(ZMax, Sample.Z);
            ZSum += Sample.Z;
            if (Sample.NumContributors > 1)
            {
                NumMultiStamp++;
            }

            Obj->SetBoolField(TEXT("hit"), true);
            Obj->SetNumberField(TEXT("z"), Sample.Z);
            Obj->SetStringField(TEXT("stamp"), Stamps[Sample.StampIndex].ActorLabel);

            // Normals come free from the neighbouring grid samples — no extra heightmap work.
            if (bIncludeNormals)
            {
                bool bSlopeX = false;
                bool bSlopeY = false;
                double DzDx = 0;
                double DzDy = 0;

                const FGroundSample* XPrev = IX > 0 ? &Samples[(IX - 1) + Count * IY] : nullptr;
                const FGroundSample* XNext = IX < Count - 1 ? &Samples[(IX + 1) + Count * IY] : nullptr;
                if (XPrev && XNext && XPrev->bHit && XNext->bHit)
                {
                    DzDx = (XNext->Z - XPrev->Z) / (2.0 * Step);
                    bSlopeX = true;
                }
                else if (XNext && XNext->bHit)
                {
                    DzDx = (XNext->Z - Sample.Z) / Step;
                    bSlopeX = true;
                }
                else if (XPrev && XPrev->bHit)
                {
                    DzDx = (Sample.Z - XPrev->Z) / Step;
                    bSlopeX = true;
                }

                const FGroundSample* YPrev = IY > 0 ? &Samples[IX + Count * (IY - 1)] : nullptr;
                const FGroundSample* YNext = IY < Count - 1 ? &Samples[IX + Count * (IY + 1)] : nullptr;
                if (YPrev && YNext && YPrev->bHit && YNext->bHit)
                {
                    DzDy = (YNext->Z - YPrev->Z) / (2.0 * Step);
                    bSlopeY = true;
                }
                else if (YNext && YNext->bHit)
                {
                    DzDy = (YNext->Z - Sample.Z) / Step;
                    bSlopeY = true;
                }
                else if (YPrev && YPrev->bHit)
                {
                    DzDy = (Sample.Z - YPrev->Z) / Step;
                    bSlopeY = true;
                }

                FVector Normal;
                double SlopeDeg = 0;
                if (NormalFromSlopes(bSlopeX, DzDx, bSlopeY, DzDy, Normal, SlopeDeg))
                {
                    Obj->SetField(TEXT("normal"), MakeVec(Normal.X, Normal.Y, Normal.Z));
                    Obj->SetNumberField(TEXT("slope_deg"), SlopeDeg);
                }
            }

            SampleArray.Add(MakeShared<FJsonValueObject>(Obj));
        }
    }

    Root->SetStringField(TEXT("mode"), TEXT("grid"));

    TSharedPtr<FJsonObject> GridObj = MakeShared<FJsonObject>();
    GridObj->SetField(TEXT("center"), MakeVec(CenterX, CenterY));
    GridObj->SetNumberField(TEXT("extent"), Extent);
    GridObj->SetNumberField(TEXT("step"), Step);
    GridObj->SetNumberField(TEXT("requested_step"), RequestedStep);
    GridObj->SetNumberField(TEXT("count_x"), Count);
    GridObj->SetNumberField(TEXT("count_y"), Count);
    GridObj->SetNumberField(TEXT("total_samples"), Count * Count);
    GridObj->SetNumberField(TEXT("max_samples"), MaxSamples);
    GridObj->SetBoolField(TEXT("clamped"), bClamped);
    Root->SetObjectField(TEXT("grid"), GridObj);

    Root->SetNumberField(TEXT("hits"), NumHits);
    Root->SetNumberField(TEXT("misses"), Count * Count - NumHits);
    if (NumHits > 0)
    {
        Root->SetNumberField(TEXT("z_min"), ZMin);
        Root->SetNumberField(TEXT("z_max"), ZMax);
        Root->SetNumberField(TEXT("z_mean"), ZSum / double(NumHits));
    }
    Root->SetArrayField(TEXT("samples"), SampleArray);

    if (bClamped)
    {
        Warnings.Add(FString::Printf(
            TEXT("Sample count clamped to max_samples (%d): step coarsened from %g to %g cm, extent preserved"),
            MaxSamples, RequestedStep, Step));
    }
    if (NumMultiStamp > 0)
    {
        Warnings.Add(FString::Printf(
            TEXT("%d samples are covered by more than one stamp: blending is hard min/max/override here, the mesher additionally applies per-stamp Smoothness"),
            NumMultiStamp));
    }
    if (Warnings.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> WarningArray;
        for (const FString& W : Warnings)
        {
            WarningArray.Add(MakeShared<FJsonValueString>(W));
        }
        Root->SetArrayField(TEXT("warnings"), WarningArray);
    }

    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Requires editor build"));
#endif
}

// --- Registration (called from FMonolithVoxelActions::RegisterActions) ---

void FMonolithVoxelActions::RegisterHeightQueryActions()
{
    FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

    Registry.RegisterAction(TEXT("voxel"), TEXT("query_height"),
        TEXT("Ground Z at world XY, read from the VP2 HEIGHTMAP DATA + stamp transform - not from a line trace. ")
        TEXT("Use this instead of a downward trace for placing spawns/pickups/waypoints/nav bounds: VP2 runtime collision is invoker-driven, ")
        TEXT("so most of a stamped map has no collision in the editor and traces hit blockout actors or nothing. ")
        TEXT("Single sample: {world_x, world_y}. Grid: {center:[x,y], extent, step} (extent = half-size in cm). ")
        TEXT("Every sample reports hit true/false - a miss NEVER returns a z, it returns a reason plus the nearest stamp and its distance. ")
        TEXT("Normals come from neighbouring samples (grid) or 4 probes (point) and are omitted when a neighbour falls outside the footprint. ")
        TEXT("Multiple stamps: only stamps whose XY footprint contains the point contribute; they are applied in ascending Priority with ")
        TEXT("Override/Max/Min, ignoring layer order and per-stamp Smoothness (flagged in warnings when it can matter)."),
        FMonolithActionHandler::CreateStatic(&HandleQueryHeight),
        FParamSchemaBuilder()
            .Optional(TEXT("world_x"), TEXT("number"), TEXT("Single-sample world X in cm (with world_y)"), { TEXT("x") })
            .Optional(TEXT("world_y"), TEXT("number"), TEXT("Single-sample world Y in cm (with world_x)"), { TEXT("y") })
            .Optional(TEXT("center"), TEXT("array"), TEXT("Grid centre [x, y] in cm (or {x, y}). Mutually exclusive with world_x/world_y"))
            .Optional(TEXT("extent"), TEXT("number"), TEXT("Grid half-size in cm; the grid spans center +/- extent on both axes. Required with center"))
            .Optional(TEXT("step"), TEXT("number"), TEXT("Grid spacing in cm. Required with center. Coarsened if the sample count would exceed max_samples"))
            .Optional(TEXT("max_samples"), TEXT("number"), TEXT("Cap on total grid samples (default 4096, hard max 40000). On overflow the step grows and extent is preserved; 'clamped' is reported"))
            .Optional(TEXT("include_normals"), TEXT("boolean"), TEXT("Return a surface normal + slope_deg per sample (default true)"))
            .Optional(TEXT("normal_delta"), TEXT("number"), TEXT("Point mode only: probe distance in cm for the normal (default: one heightmap texel, i.e. the stamp's ScaleXY)"))
            .Optional(TEXT("stamp_actor"), TEXT("string"), TEXT("Restrict sampling to one stamp actor (label or object name). Default: every heightmap stamp in the level"))
            .Build());
}
