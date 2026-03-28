#if WITH_GEOMETRYSCRIPT

#include "MonolithMeshProceduralActions.h"
#include "MonolithMeshHandlePool.h"
#include "MonolithMeshUtils.h"
#include "MonolithToolRegistry.h"
#include "MonolithParamSchema.h"
#include "MonolithJsonUtils.h"
#include "MonolithAssetUtils.h"

#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"

#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/MeshDeformFunctions.h"
#include "GeometryScript/MeshTransformFunctions.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Editor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "Math/RandomStream.h"

using namespace UE::Geometry;

UMonolithMeshHandlePool* FMonolithMeshProceduralActions::Pool = nullptr;

void FMonolithMeshProceduralActions::SetHandlePool(UMonolithMeshHandlePool* InPool)
{
	Pool = InPool;
}

// ============================================================================
// Registration
// ============================================================================

void FMonolithMeshProceduralActions::RegisterActions(FMonolithToolRegistry& Registry)
{
	Registry.RegisterAction(TEXT("mesh"), TEXT("create_parametric_mesh"),
		TEXT("Generate blockout-quality parametric furniture/props from boolean operations on primitives. "
			"Types: chair, table, desk, shelf, cabinet, bed, door_frame, window_frame, stairs, ramp, pillar, counter, toilet, sink, bathtub"),
		FMonolithActionHandler::CreateStatic(&FMonolithMeshProceduralActions::CreateParametricMesh),
		FParamSchemaBuilder()
			.Required(TEXT("type"), TEXT("string"), TEXT("Furniture type: chair, table, desk, shelf, cabinet, bed, door_frame, window_frame, stairs, ramp, pillar, counter, toilet, sink, bathtub"))
			.Optional(TEXT("dimensions"), TEXT("object"), TEXT("{ width, depth, height } in cm — defaults vary per type"))
			.Optional(TEXT("params"), TEXT("object"), TEXT("Type-specific params: seat_height, back_height, leg_thickness, stair_count, stair_depth, shelf_count, bowl_radius, etc."))
			.Optional(TEXT("handle"), TEXT("string"), TEXT("Save result to a mesh handle (for further operations)"))
			.Optional(TEXT("save_path"), TEXT("string"), TEXT("Asset path to save as StaticMesh (e.g. /Game/Blockout/SM_Chair_01)"))
			.Optional(TEXT("overwrite"), TEXT("boolean"), TEXT("Allow overwriting existing asset at save_path"), TEXT("false"))
			.Optional(TEXT("place_in_scene"), TEXT("boolean"), TEXT("Spawn the mesh as an actor in the current level"), TEXT("false"))
			.Optional(TEXT("location"), TEXT("array"), TEXT("World location [x, y, z] for scene placement"))
			.Optional(TEXT("rotation"), TEXT("array"), TEXT("World rotation [pitch, yaw, roll] for scene placement"))
			.Optional(TEXT("label"), TEXT("string"), TEXT("Actor label when placing in scene"))
			.Build());

	Registry.RegisterAction(TEXT("mesh"), TEXT("create_horror_prop"),
		TEXT("Generate horror-specific procedural props: barricade, debris_pile, cage, coffin, gurney, broken_wall, vent_grate"),
		FMonolithActionHandler::CreateStatic(&FMonolithMeshProceduralActions::CreateHorrorProp),
		FParamSchemaBuilder()
			.Required(TEXT("type"), TEXT("string"), TEXT("Horror prop type: barricade, debris_pile, cage, coffin, gurney, broken_wall, vent_grate"))
			.Optional(TEXT("dimensions"), TEXT("object"), TEXT("{ width, depth, height } in cm — defaults vary per type"))
			.Optional(TEXT("params"), TEXT("object"), TEXT("Type-specific params: board_count, bar_count, noise_scale, hole_radius, slot_count, gap_ratio, etc."))
			.Optional(TEXT("seed"), TEXT("integer"), TEXT("Random seed for procedural variation"), TEXT("0"))
			.Optional(TEXT("handle"), TEXT("string"), TEXT("Save result to a mesh handle (for further operations)"))
			.Optional(TEXT("save_path"), TEXT("string"), TEXT("Asset path to save as StaticMesh"))
			.Optional(TEXT("overwrite"), TEXT("boolean"), TEXT("Allow overwriting existing asset at save_path"), TEXT("false"))
			.Optional(TEXT("place_in_scene"), TEXT("boolean"), TEXT("Spawn the mesh as an actor in the current level"), TEXT("false"))
			.Optional(TEXT("location"), TEXT("array"), TEXT("World location [x, y, z] for scene placement"))
			.Optional(TEXT("rotation"), TEXT("array"), TEXT("World rotation [pitch, yaw, roll] for scene placement"))
			.Optional(TEXT("label"), TEXT("string"), TEXT("Actor label when placing in scene"))
			.Build());
}

// ============================================================================
// Shared helpers
// ============================================================================

static const FString GS_ERROR = TEXT("Enable the GeometryScripting plugin in your .uproject to use procedural geometry.");

void FMonolithMeshProceduralActions::ParseDimensions(const TSharedPtr<FJsonObject>& Params,
	float& Width, float& Depth, float& Height,
	float DefaultWidth, float DefaultDepth, float DefaultHeight)
{
	Width = DefaultWidth;
	Depth = DefaultDepth;
	Height = DefaultHeight;

	const TSharedPtr<FJsonObject>* DimObj = nullptr;
	if (Params->TryGetObjectField(TEXT("dimensions"), DimObj) && DimObj && (*DimObj).IsValid())
	{
		if ((*DimObj)->HasField(TEXT("width")))  Width  = static_cast<float>((*DimObj)->GetNumberField(TEXT("width")));
		if ((*DimObj)->HasField(TEXT("depth")))  Depth  = static_cast<float>((*DimObj)->GetNumberField(TEXT("depth")));
		if ((*DimObj)->HasField(TEXT("height"))) Height = static_cast<float>((*DimObj)->GetNumberField(TEXT("height")));
	}
}

TSharedPtr<FJsonObject> FMonolithMeshProceduralActions::ParseSubParams(const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonObject>* SubObj = nullptr;
	if (Params->TryGetObjectField(TEXT("params"), SubObj) && SubObj && (*SubObj).IsValid())
	{
		return *SubObj;
	}
	return MakeShared<FJsonObject>();
}

void FMonolithMeshProceduralActions::CleanupMesh(UDynamicMesh* Mesh, bool bHadBooleans)
{
	if (!Mesh) return;

	if (bHadBooleans)
	{
		// After booleans: recompute normals with split by angle
		FGeometryScriptSplitNormalsOptions SplitOpts;
		SplitOpts.bSplitByOpeningAngle = true;
		SplitOpts.OpeningAngleDeg = 15.0f;
		FGeometryScriptCalculateNormalsOptions CalcOpts;
		UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(Mesh, SplitOpts, CalcOpts);
	}
	else
	{
		// Additive-only: self-union to merge overlapping geometry + clean normals
		FGeometryScriptMeshSelfUnionOptions SelfUnionOpts;
		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshSelfUnion(Mesh, SelfUnionOpts);

		FGeometryScriptSplitNormalsOptions SplitOpts;
		SplitOpts.bSplitByOpeningAngle = true;
		SplitOpts.OpeningAngleDeg = 15.0f;
		FGeometryScriptCalculateNormalsOptions CalcOpts;
		UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(Mesh, SplitOpts, CalcOpts);
	}
}

bool FMonolithMeshProceduralActions::SaveMeshToAsset(UDynamicMesh* Mesh, const FString& SavePath, bool bOverwrite, FString& OutError)
{
	if (!Pool)
	{
		OutError = GS_ERROR;
		return false;
	}

	// Create a temporary handle, save it, then release
	FString TempHandle = FString::Printf(TEXT("__proc_save_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
	FString CreateError;

	// We can't use Pool->CreateHandle for an already-built mesh, so we use the save pipeline directly.
	// Reuse the handle pool's SaveHandle which converts DynamicMesh -> MeshDescription -> StaticMesh.
	// But we need a handle entry. Create internal handle, copy mesh data in, save, release.
	if (!Pool->CreateHandle(TempHandle, TEXT("internal:procedural_save"), CreateError))
	{
		OutError = CreateError;
		return false;
	}

	UDynamicMesh* TempMesh = Pool->GetHandle(TempHandle, CreateError);
	if (!TempMesh)
	{
		Pool->ReleaseHandle(TempHandle);
		OutError = TEXT("Failed to get temporary handle mesh");
		return false;
	}

	// Copy geometry into the temp handle
	TempMesh->SetMesh(Mesh->GetMeshRef());

	// Save via pool
	if (!Pool->SaveHandle(TempHandle, SavePath, bOverwrite, OutError))
	{
		Pool->ReleaseHandle(TempHandle);
		return false;
	}

	Pool->ReleaseHandle(TempHandle);
	return true;
}

AActor* FMonolithMeshProceduralActions::PlaceMeshInScene(const FString& AssetPath, const FVector& Location, const FRotator& Rotation, const FString& Label)
{
	UWorld* World = MonolithMeshUtils::GetEditorWorld();
	if (!World) return nullptr;

	UStaticMesh* SM = FMonolithAssetUtils::LoadAssetByPath<UStaticMesh>(AssetPath);
	if (!SM) return nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, Rotation, SpawnParams);
	if (!Actor) return nullptr;

	Actor->GetStaticMeshComponent()->SetStaticMesh(SM);

	if (!Label.IsEmpty())
	{
		Actor->SetActorLabel(Label);
	}

	return Actor;
}

// ============================================================================
// create_parametric_mesh
// ============================================================================

FMonolithActionResult FMonolithMeshProceduralActions::CreateParametricMesh(const TSharedPtr<FJsonObject>& Params)
{
	if (!Pool)
	{
		return FMonolithActionResult::Error(GS_ERROR);
	}

	FString Type;
	if (!Params->TryGetStringField(TEXT("type"), Type))
	{
		return FMonolithActionResult::Error(TEXT("Missing required param: type"));
	}
	Type = Type.ToLower().TrimStartAndEnd();

	// Parse dimensions with type-appropriate defaults
	float Width, Depth, Height;

	// Type-specific dimension defaults (cm)
	if      (Type == TEXT("chair"))        ParseDimensions(Params, Width, Depth, Height, 45, 45, 90);
	else if (Type == TEXT("table"))        ParseDimensions(Params, Width, Depth, Height, 120, 75, 75);
	else if (Type == TEXT("desk"))         ParseDimensions(Params, Width, Depth, Height, 120, 60, 75);
	else if (Type == TEXT("shelf"))        ParseDimensions(Params, Width, Depth, Height, 80, 30, 180);
	else if (Type == TEXT("cabinet"))      ParseDimensions(Params, Width, Depth, Height, 60, 45, 90);
	else if (Type == TEXT("bed"))          ParseDimensions(Params, Width, Depth, Height, 100, 200, 55);
	else if (Type == TEXT("door_frame"))   ParseDimensions(Params, Width, Depth, Height, 100, 20, 210);
	else if (Type == TEXT("window_frame")) ParseDimensions(Params, Width, Depth, Height, 120, 20, 100);
	else if (Type == TEXT("stairs"))       ParseDimensions(Params, Width, Depth, Height, 100, 30, 20);
	else if (Type == TEXT("ramp"))         ParseDimensions(Params, Width, Depth, Height, 100, 200, 100);
	else if (Type == TEXT("pillar"))       ParseDimensions(Params, Width, Depth, Height, 30, 30, 300);
	else if (Type == TEXT("counter"))      ParseDimensions(Params, Width, Depth, Height, 200, 60, 90);
	else if (Type == TEXT("toilet"))       ParseDimensions(Params, Width, Depth, Height, 40, 65, 40);
	else if (Type == TEXT("sink"))         ParseDimensions(Params, Width, Depth, Height, 60, 45, 85);
	else if (Type == TEXT("bathtub"))      ParseDimensions(Params, Width, Depth, Height, 75, 170, 60);
	else
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Unknown type '%s'. Valid: chair, table, desk, shelf, cabinet, bed, door_frame, window_frame, stairs, ramp, pillar, counter, toilet, sink, bathtub"), *Type));
	}

	auto SubParams = ParseSubParams(Params);

	// Create working mesh
	UDynamicMesh* Mesh = NewObject<UDynamicMesh>(Pool);
	if (!Mesh)
	{
		return FMonolithActionResult::Error(TEXT("Failed to allocate UDynamicMesh"));
	}

	FString BuildError;
	bool bHadBooleans = false;

	// Dispatch to type-specific builder
	if (Type == TEXT("chair"))
	{
		float SeatH  = SubParams->HasField(TEXT("seat_height"))   ? static_cast<float>(SubParams->GetNumberField(TEXT("seat_height")))   : 45.0f;
		float BackH  = SubParams->HasField(TEXT("back_height"))   ? static_cast<float>(SubParams->GetNumberField(TEXT("back_height")))   : Height - SeatH;
		float LegT   = SubParams->HasField(TEXT("leg_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("leg_thickness"))) : 4.0f;
		if (!BuildChair(Mesh, Width, Depth, Height, SeatH, BackH, LegT, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("table"))
	{
		float LegT  = SubParams->HasField(TEXT("leg_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("leg_thickness"))) : 5.0f;
		float TopT  = SubParams->HasField(TEXT("top_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("top_thickness"))) : 4.0f;
		if (!BuildTable(Mesh, Width, Depth, Height, LegT, TopT, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("desk"))
	{
		float LegT  = SubParams->HasField(TEXT("leg_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("leg_thickness"))) : 5.0f;
		float TopT  = SubParams->HasField(TEXT("top_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("top_thickness"))) : 4.0f;
		bool bDrawer = SubParams->HasField(TEXT("has_drawer")) ? SubParams->GetBoolField(TEXT("has_drawer")) : true;
		if (!BuildDesk(Mesh, Width, Depth, Height, LegT, TopT, bDrawer, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("shelf"))
	{
		int32 ShelfN = SubParams->HasField(TEXT("shelf_count"))     ? static_cast<int32>(SubParams->GetNumberField(TEXT("shelf_count")))     : 4;
		float BoardT = SubParams->HasField(TEXT("board_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("board_thickness"))) : 2.0f;
		if (!BuildShelf(Mesh, Width, Depth, Height, ShelfN, BoardT, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("cabinet"))
	{
		float WallT    = SubParams->HasField(TEXT("wall_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("wall_thickness"))) : 3.0f;
		float RecessD  = SubParams->HasField(TEXT("recess_depth"))   ? static_cast<float>(SubParams->GetNumberField(TEXT("recess_depth")))   : Depth - WallT * 2;
		bHadBooleans = true;
		if (!BuildCabinet(Mesh, Width, Depth, Height, WallT, RecessD, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("bed"))
	{
		float MattH = SubParams->HasField(TEXT("mattress_height"))  ? static_cast<float>(SubParams->GetNumberField(TEXT("mattress_height")))  : 20.0f;
		float HeadH = SubParams->HasField(TEXT("headboard_height")) ? static_cast<float>(SubParams->GetNumberField(TEXT("headboard_height"))) : 50.0f;
		if (!BuildBed(Mesh, Width, Depth, Height, MattH, HeadH, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("door_frame"))
	{
		float FrameT = SubParams->HasField(TEXT("frame_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("frame_thickness"))) : 10.0f;
		float FrameD = SubParams->HasField(TEXT("frame_depth"))     ? static_cast<float>(SubParams->GetNumberField(TEXT("frame_depth")))     : Depth;
		bHadBooleans = true;
		if (!BuildDoorFrame(Mesh, Width, Height, FrameT, FrameD, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("window_frame"))
	{
		float FrameT = SubParams->HasField(TEXT("frame_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("frame_thickness"))) : 8.0f;
		float FrameD = SubParams->HasField(TEXT("frame_depth"))     ? static_cast<float>(SubParams->GetNumberField(TEXT("frame_depth")))     : Depth;
		float SillH  = SubParams->HasField(TEXT("sill_height"))     ? static_cast<float>(SubParams->GetNumberField(TEXT("sill_height")))     : 90.0f;
		bHadBooleans = true;
		if (!BuildWindowFrame(Mesh, Width, Height, FrameT, FrameD, SillH, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("stairs"))
	{
		int32 StepN  = SubParams->HasField(TEXT("stair_count")) ? static_cast<int32>(SubParams->GetNumberField(TEXT("stair_count"))) : 8;
		float StepH  = SubParams->HasField(TEXT("step_height")) ? static_cast<float>(SubParams->GetNumberField(TEXT("step_height"))) : Height;
		float StepD  = SubParams->HasField(TEXT("step_depth"))  ? static_cast<float>(SubParams->GetNumberField(TEXT("step_depth")))  : Depth;
		bool bFloat  = SubParams->HasField(TEXT("floating"))    ? SubParams->GetBoolField(TEXT("floating")) : false;
		if (!BuildStairs(Mesh, Width, StepH, StepD, StepN, bFloat, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("ramp"))
	{
		if (!BuildRamp(Mesh, Width, Depth, Height, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("pillar"))
	{
		float Radius = Width * 0.5f;
		int32 Sides  = SubParams->HasField(TEXT("sides"))  ? static_cast<int32>(SubParams->GetNumberField(TEXT("sides")))  : 12;
		bool bRound  = SubParams->HasField(TEXT("round"))  ? SubParams->GetBoolField(TEXT("round")) : true;
		if (!BuildPillar(Mesh, Radius, Height, Sides, bRound, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("counter"))
	{
		float TopT = SubParams->HasField(TEXT("top_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("top_thickness"))) : 5.0f;
		if (!BuildCounter(Mesh, Width, Depth, Height, TopT, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("toilet"))
	{
		float BowlD = SubParams->HasField(TEXT("bowl_depth")) ? static_cast<float>(SubParams->GetNumberField(TEXT("bowl_depth"))) : 15.0f;
		bHadBooleans = true;
		if (!BuildToilet(Mesh, Width, Depth, Height, BowlD, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("sink"))
	{
		float BowlR = SubParams->HasField(TEXT("bowl_radius")) ? static_cast<float>(SubParams->GetNumberField(TEXT("bowl_radius"))) : FMath::Min(Width, Depth) * 0.35f;
		float BowlD = SubParams->HasField(TEXT("bowl_depth"))  ? static_cast<float>(SubParams->GetNumberField(TEXT("bowl_depth")))  : 12.0f;
		bHadBooleans = true;
		if (!BuildSink(Mesh, Width, Depth, Height, BowlR, BowlD, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("bathtub"))
	{
		float WallT = SubParams->HasField(TEXT("wall_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("wall_thickness"))) : 5.0f;
		bHadBooleans = true;
		if (!BuildBathtub(Mesh, Width, Depth, Height, WallT, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}

	// Cleanup pass
	CleanupMesh(Mesh, bHadBooleans);

	int32 TriCount = Mesh->GetTriangleCount();

	// Build result
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("type"), Type);
	Result->SetNumberField(TEXT("triangle_count"), TriCount);
	Result->SetBoolField(TEXT("had_booleans"), bHadBooleans);

	// Handle pool storage
	FString HandleName;
	if (Params->TryGetStringField(TEXT("handle"), HandleName) && !HandleName.IsEmpty())
	{
		FString CreateErr;
		if (!Pool->CreateHandle(HandleName, FString::Printf(TEXT("internal:parametric:%s"), *Type), CreateErr))
		{
			return FMonolithActionResult::Error(CreateErr);
		}
		UDynamicMesh* HandleMesh = Pool->GetHandle(HandleName, CreateErr);
		if (HandleMesh)
		{
			HandleMesh->SetMesh(Mesh->GetMeshRef());
		}
		Result->SetStringField(TEXT("handle"), HandleName);
	}

	// Save to asset
	FString SavePath;
	if (Params->TryGetStringField(TEXT("save_path"), SavePath) && !SavePath.IsEmpty())
	{
		bool bOverwrite = Params->HasField(TEXT("overwrite")) ? Params->GetBoolField(TEXT("overwrite")) : false;
		FString SaveErr;
		if (!SaveMeshToAsset(Mesh, SavePath, bOverwrite, SaveErr))
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Mesh generated (%d tris) but save failed: %s"), TriCount, *SaveErr));
		}
		Result->SetStringField(TEXT("save_path"), SavePath);

		// Place in scene if requested
		bool bPlace = Params->HasField(TEXT("place_in_scene")) ? Params->GetBoolField(TEXT("place_in_scene")) : false;
		if (bPlace)
		{
			FVector Location = FVector::ZeroVector;
			MonolithMeshUtils::ParseVector(Params, TEXT("location"), Location);
			FRotator Rotation = FRotator::ZeroRotator;
			MonolithMeshUtils::ParseRotator(Params, TEXT("rotation"), Rotation);
			FString Label;
			Params->TryGetStringField(TEXT("label"), Label);

			AActor* Actor = PlaceMeshInScene(SavePath, Location, Rotation, Label);
			if (Actor)
			{
				Result->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
			}
		}
	}

	return FMonolithActionResult::Success(Result);
}

// ============================================================================
// create_horror_prop
// ============================================================================

FMonolithActionResult FMonolithMeshProceduralActions::CreateHorrorProp(const TSharedPtr<FJsonObject>& Params)
{
	if (!Pool)
	{
		return FMonolithActionResult::Error(GS_ERROR);
	}

	FString Type;
	if (!Params->TryGetStringField(TEXT("type"), Type))
	{
		return FMonolithActionResult::Error(TEXT("Missing required param: type"));
	}
	Type = Type.ToLower().TrimStartAndEnd();

	int32 Seed = Params->HasField(TEXT("seed")) ? static_cast<int32>(Params->GetNumberField(TEXT("seed"))) : 0;

	float Width, Depth, Height;

	if      (Type == TEXT("barricade"))   ParseDimensions(Params, Width, Depth, Height, 120, 10, 200);
	else if (Type == TEXT("debris_pile")) ParseDimensions(Params, Width, Depth, Height, 150, 150, 60);
	else if (Type == TEXT("cage"))        ParseDimensions(Params, Width, Depth, Height, 100, 100, 200);
	else if (Type == TEXT("coffin"))      ParseDimensions(Params, Width, Depth, Height, 60, 200, 50);
	else if (Type == TEXT("gurney"))      ParseDimensions(Params, Width, Depth, Height, 70, 190, 90);
	else if (Type == TEXT("broken_wall")) ParseDimensions(Params, Width, Depth, Height, 300, 25, 250);
	else if (Type == TEXT("vent_grate"))  ParseDimensions(Params, Width, Depth, Height, 60, 5, 40);
	else
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Unknown horror prop type '%s'. Valid: barricade, debris_pile, cage, coffin, gurney, broken_wall, vent_grate"), *Type));
	}

	auto SubParams = ParseSubParams(Params);

	UDynamicMesh* Mesh = NewObject<UDynamicMesh>(Pool);
	if (!Mesh)
	{
		return FMonolithActionResult::Error(TEXT("Failed to allocate UDynamicMesh"));
	}

	FString BuildError;
	bool bHadBooleans = false;

	if (Type == TEXT("barricade"))
	{
		int32 BoardN  = SubParams->HasField(TEXT("board_count")) ? static_cast<int32>(SubParams->GetNumberField(TEXT("board_count"))) : 5;
		float GapR    = SubParams->HasField(TEXT("gap_ratio"))   ? static_cast<float>(SubParams->GetNumberField(TEXT("gap_ratio")))   : 0.3f;
		if (!BuildBarricade(Mesh, Width, Height, Depth, BoardN, GapR, Seed, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("debris_pile"))
	{
		float Radius  = FMath::Max(Width, Depth) * 0.5f;
		int32 PieceN  = SubParams->HasField(TEXT("piece_count")) ? static_cast<int32>(SubParams->GetNumberField(TEXT("piece_count"))) : 12;
		if (!BuildDebrisPile(Mesh, Radius, Height, PieceN, Seed, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("cage"))
	{
		int32 BarN   = SubParams->HasField(TEXT("bar_count"))  ? static_cast<int32>(SubParams->GetNumberField(TEXT("bar_count")))  : 8;
		float BarR   = SubParams->HasField(TEXT("bar_radius")) ? static_cast<float>(SubParams->GetNumberField(TEXT("bar_radius"))) : 1.5f;
		if (!BuildCage(Mesh, Width, Depth, Height, BarN, BarR, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("coffin"))
	{
		float WallT  = SubParams->HasField(TEXT("wall_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("wall_thickness"))) : 3.0f;
		float LidGap = SubParams->HasField(TEXT("lid_gap"))        ? static_cast<float>(SubParams->GetNumberField(TEXT("lid_gap")))        : 2.0f;
		bHadBooleans = true;
		if (!BuildCoffin(Mesh, Width, Depth, Height, WallT, LidGap, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("gurney"))
	{
		float LegR = SubParams->HasField(TEXT("leg_radius")) ? static_cast<float>(SubParams->GetNumberField(TEXT("leg_radius"))) : 2.0f;
		if (!BuildGurney(Mesh, Width, Depth, Height, LegR, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("broken_wall"))
	{
		float NoiseS = SubParams->HasField(TEXT("noise_scale"))  ? static_cast<float>(SubParams->GetNumberField(TEXT("noise_scale")))  : 0.3f;
		float HoleR  = SubParams->HasField(TEXT("hole_radius"))  ? static_cast<float>(SubParams->GetNumberField(TEXT("hole_radius")))  : 60.0f;
		bHadBooleans = true;
		if (!BuildBrokenWall(Mesh, Width, Height, Depth, NoiseS, HoleR, Seed, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}
	else if (Type == TEXT("vent_grate"))
	{
		int32 SlotN   = SubParams->HasField(TEXT("slot_count"))      ? static_cast<int32>(SubParams->GetNumberField(TEXT("slot_count")))      : 6;
		float FrameT  = SubParams->HasField(TEXT("frame_thickness")) ? static_cast<float>(SubParams->GetNumberField(TEXT("frame_thickness"))) : 3.0f;
		if (!BuildVentGrate(Mesh, Width, Height, Depth, SlotN, FrameT, BuildError))
			return FMonolithActionResult::Error(BuildError);
	}

	CleanupMesh(Mesh, bHadBooleans);

	int32 TriCount = Mesh->GetTriangleCount();

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("type"), Type);
	Result->SetNumberField(TEXT("triangle_count"), TriCount);
	Result->SetNumberField(TEXT("seed"), Seed);
	Result->SetBoolField(TEXT("had_booleans"), bHadBooleans);

	// Handle pool storage
	FString HandleName;
	if (Params->TryGetStringField(TEXT("handle"), HandleName) && !HandleName.IsEmpty())
	{
		FString CreateErr;
		if (!Pool->CreateHandle(HandleName, FString::Printf(TEXT("internal:horror:%s"), *Type), CreateErr))
		{
			return FMonolithActionResult::Error(CreateErr);
		}
		UDynamicMesh* HandleMesh = Pool->GetHandle(HandleName, CreateErr);
		if (HandleMesh)
		{
			HandleMesh->SetMesh(Mesh->GetMeshRef());
		}
		Result->SetStringField(TEXT("handle"), HandleName);
	}

	// Save to asset
	FString SavePath;
	if (Params->TryGetStringField(TEXT("save_path"), SavePath) && !SavePath.IsEmpty())
	{
		bool bOverwrite = Params->HasField(TEXT("overwrite")) ? Params->GetBoolField(TEXT("overwrite")) : false;
		FString SaveErr;
		if (!SaveMeshToAsset(Mesh, SavePath, bOverwrite, SaveErr))
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Mesh generated (%d tris) but save failed: %s"), TriCount, *SaveErr));
		}
		Result->SetStringField(TEXT("save_path"), SavePath);

		bool bPlace = Params->HasField(TEXT("place_in_scene")) ? Params->GetBoolField(TEXT("place_in_scene")) : false;
		if (bPlace)
		{
			FVector Location = FVector::ZeroVector;
			MonolithMeshUtils::ParseVector(Params, TEXT("location"), Location);
			FRotator Rotation = FRotator::ZeroRotator;
			MonolithMeshUtils::ParseRotator(Params, TEXT("rotation"), Rotation);
			FString Label;
			Params->TryGetStringField(TEXT("label"), Label);

			AActor* Actor = PlaceMeshInScene(SavePath, Location, Rotation, Label);
			if (Actor)
			{
				Result->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
			}
		}
	}

	return FMonolithActionResult::Success(Result);
}

// ============================================================================
// Parametric Furniture Builders
// ============================================================================

bool FMonolithMeshProceduralActions::BuildChair(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float SeatHeight, float BackHeight, float LegThickness, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;
	float SeatT = 3.0f; // seat thickness
	float HalfLeg = LegThickness * 0.5f;

	// Seat (Origin=Base, so Z is bottom of box)
	FTransform SeatXf(FRotator::ZeroRotator, FVector(0, 0, SeatHeight - SeatT), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, SeatXf, Width, Depth, SeatT, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Back rest
	float BackZ = SeatHeight;
	FTransform BackXf(FRotator::ZeroRotator, FVector(0, -Depth * 0.5f + HalfLeg, BackZ), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, BackXf, Width, LegThickness, BackHeight, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// 4 Legs (cylinders, Origin=Base)
	float LegInsetX = Width * 0.5f - HalfLeg - 1.0f;
	float LegInsetY = Depth * 0.5f - HalfLeg - 1.0f;

	FVector2D LegPositions[] = {
		{ -LegInsetX, -LegInsetY },
		{  LegInsetX, -LegInsetY },
		{ -LegInsetX,  LegInsetY },
		{  LegInsetX,  LegInsetY }
	};

	for (const auto& Pos : LegPositions)
	{
		FTransform LegXf(FRotator::ZeroRotator, FVector(Pos.X, Pos.Y, 0), FVector::OneVector);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(Mesh, Opts, LegXf, HalfLeg, SeatHeight, 8, 0, true, EGeometryScriptPrimitiveOriginMode::Base);
	}

	return true;
}

bool FMonolithMeshProceduralActions::BuildTable(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float LegThickness, float TopThickness, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;
	float HalfLeg = LegThickness * 0.5f;
	float LegHeight = Height - TopThickness;

	// Table top
	FTransform TopXf(FRotator::ZeroRotator, FVector(0, 0, LegHeight), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, TopXf, Width, Depth, TopThickness, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// 4 Legs
	float InX = Width * 0.5f - HalfLeg - 2.0f;
	float InY = Depth * 0.5f - HalfLeg - 2.0f;
	FVector2D Corners[] = { {-InX,-InY}, {InX,-InY}, {-InX,InY}, {InX,InY} };

	for (const auto& C : Corners)
	{
		FTransform LegXf(FRotator::ZeroRotator, FVector(C.X, C.Y, 0), FVector::OneVector);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, LegXf, LegThickness, LegThickness, LegHeight, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
	}

	return true;
}

bool FMonolithMeshProceduralActions::BuildDesk(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float LegThickness, float TopThickness, bool bHasDrawer, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;
	float LegHeight = Height - TopThickness;

	// Top
	FTransform TopXf(FRotator::ZeroRotator, FVector(0, 0, LegHeight), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, TopXf, Width, Depth, TopThickness, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// 4 Legs
	float HalfLeg = LegThickness * 0.5f;
	float InX = Width * 0.5f - HalfLeg - 2.0f;
	float InY = Depth * 0.5f - HalfLeg - 2.0f;
	FVector2D Corners[] = { {-InX,-InY}, {InX,-InY}, {-InX,InY}, {InX,InY} };

	for (const auto& C : Corners)
	{
		FTransform LegXf(FRotator::ZeroRotator, FVector(C.X, C.Y, 0), FVector::OneVector);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, LegXf, LegThickness, LegThickness, LegHeight, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
	}

	// Modesty panel (back)
	float PanelHeight = LegHeight * 0.6f;
	FTransform PanelXf(FRotator::ZeroRotator, FVector(0, -Depth * 0.5f + 1.0f, LegHeight - PanelHeight), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, PanelXf, Width, 2.0f, PanelHeight, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Optional drawer box (right side)
	if (bHasDrawer)
	{
		float DrawerW = Width * 0.35f;
		float DrawerH = LegHeight * 0.25f;
		FTransform DrawerXf(FRotator::ZeroRotator, FVector(Width * 0.5f - DrawerW * 0.5f - LegThickness, 0, LegHeight - DrawerH - TopThickness), FVector::OneVector);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, DrawerXf, DrawerW, Depth - LegThickness * 2, DrawerH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
	}

	return true;
}

bool FMonolithMeshProceduralActions::BuildShelf(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	int32 ShelfCount, float BoardThickness, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;
	ShelfCount = FMath::Clamp(ShelfCount, 2, 20);

	// Two side panels
	float SideThickness = 2.0f;
	FTransform LeftXf(FRotator::ZeroRotator, FVector(-Width * 0.5f + SideThickness * 0.5f, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, LeftXf, SideThickness, Depth, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
	FTransform RightXf(FRotator::ZeroRotator, FVector(Width * 0.5f - SideThickness * 0.5f, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, RightXf, SideThickness, Depth, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Shelves using AppendMeshRepeated
	float InnerWidth = Width - SideThickness * 2;
	float Spacing = (Height - BoardThickness) / FMath::Max(1, ShelfCount - 1);

	// Create one shelf template
	UDynamicMesh* ShelfTemplate = NewObject<UDynamicMesh>(Pool);
	FTransform ShelfBaseXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(ShelfTemplate, Opts, ShelfBaseXf, InnerWidth, Depth, BoardThickness, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// First shelf at z=0
	FTransform FirstXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, ShelfTemplate, FirstXf);

	// Repeated shelves above
	if (ShelfCount > 1)
	{
		FTransform StepXf(FRotator::ZeroRotator, FVector(0, 0, Spacing), FVector::OneVector);
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshRepeated(
			Mesh, ShelfTemplate, StepXf, ShelfCount - 1, true);
	}

	// Back panel
	FTransform BackXf(FRotator::ZeroRotator, FVector(0, -Depth * 0.5f + 0.5f, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, BackXf, InnerWidth, 1.0f, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	return true;
}

bool FMonolithMeshProceduralActions::BuildCabinet(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float WallThickness, float RecessDepth, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;

	// Outer box
	FTransform OuterXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, OuterXf, Width, Depth, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Inner recess (boolean subtract)
	UDynamicMesh* Cutter = NewObject<UDynamicMesh>(Pool);
	float CutW = Width - WallThickness * 2;
	float CutH = Height - WallThickness * 2;
	float CutD = FMath::Min(RecessDepth, Depth - WallThickness);

	// Position cutter at front face, inset by wall thickness
	FTransform CutXf(FRotator::ZeroRotator, FVector(0, Depth * 0.5f - CutD * 0.5f, WallThickness), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Cutter, Opts, CutXf, CutW, CutD, CutH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	FGeometryScriptMeshBooleanOptions BoolOpts;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Mesh, FTransform::Identity, Cutter, FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract, BoolOpts);

	return true;
}

bool FMonolithMeshProceduralActions::BuildBed(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float MattressHeight, float HeadboardHeight, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;
	float FrameH = Height - MattressHeight;

	// Frame
	FTransform FrameXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, FrameXf, Width, Depth, FrameH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Mattress (slightly inset)
	float MattInset = 3.0f;
	FTransform MattXf(FRotator::ZeroRotator, FVector(0, 0, FrameH), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, MattXf, Width - MattInset * 2, Depth - MattInset * 2, MattressHeight, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Headboard
	FTransform HeadXf(FRotator::ZeroRotator, FVector(0, -Depth * 0.5f + 2.0f, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, HeadXf, Width, 4.0f, HeadboardHeight, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	return true;
}

bool FMonolithMeshProceduralActions::BuildDoorFrame(UDynamicMesh* Mesh, float Width, float Height,
	float FrameThickness, float FrameDepth, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;
	float TotalW = Width + FrameThickness * 2;
	float TotalH = Height + FrameThickness;

	// Outer frame block
	FTransform OuterXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, OuterXf, TotalW, FrameDepth, TotalH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Inner cutout (the door opening)
	UDynamicMesh* Cutter = NewObject<UDynamicMesh>(Pool);
	// Oversized depth to ensure clean boolean
	FTransform CutXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Cutter, Opts, CutXf, Width, FrameDepth + 10.0f, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	FGeometryScriptMeshBooleanOptions BoolOpts;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Mesh, FTransform::Identity, Cutter, FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract, BoolOpts);

	return true;
}

bool FMonolithMeshProceduralActions::BuildWindowFrame(UDynamicMesh* Mesh, float Width, float Height,
	float FrameThickness, float FrameDepth, float SillHeight, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;
	float TotalW = Width + FrameThickness * 2;
	float TotalH = Height + FrameThickness * 2 + SillHeight;

	// Outer wall section (full wall)
	FTransform OuterXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, OuterXf, TotalW, FrameDepth, TotalH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Window cutout
	UDynamicMesh* Cutter = NewObject<UDynamicMesh>(Pool);
	FTransform CutXf(FRotator::ZeroRotator, FVector(0, 0, SillHeight), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Cutter, Opts, CutXf, Width, FrameDepth + 10.0f, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	FGeometryScriptMeshBooleanOptions BoolOpts;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Mesh, FTransform::Identity, Cutter, FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract, BoolOpts);

	return true;
}

bool FMonolithMeshProceduralActions::BuildStairs(UDynamicMesh* Mesh, float Width, float StepHeight, float StepDepth,
	int32 StepCount, bool bFloating, FString& OutError)
{
	StepCount = FMath::Clamp(StepCount, 1, 100);

	FGeometryScriptPrimitiveOptions Opts;
	FTransform StairXf = FTransform::Identity;

	// Native staircase primitive — no need to build from boxes
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendLinearStairs(
		Mesh, Opts, StairXf, Width, StepHeight, StepDepth, StepCount, bFloating);

	return true;
}

bool FMonolithMeshProceduralActions::BuildRamp(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;

	// Build a ramp as a box, then shear the top face.
	// Simpler approach: use a wedge via AppendBox + plane cut.
	// Even simpler: AppendBox full size, then cut diagonally.
	// Simplest reliable approach: build as a box and use TransformMeshSelection.
	// Actually, just use a box and scale — or build from triangulated polygon extrude.
	// For blockout quality, a box with one side sliced off works:

	// Create full box
	FTransform BoxXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, BoxXf, Width, Depth, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Plane cut: remove the upper-front triangle.
	// Cut plane normal points into the material we want to keep.
	// We want to cut from the top-front corner (0, Depth/2, Height) to bottom-back corner (0, -Depth/2, 0).
	// Normal = cross product of the ramp surface = pointing upward-forward.
	FVector RampNormal = FVector(0, -Height, Depth).GetSafeNormal();
	FVector RampPoint = FVector(0, Depth * 0.5f, Height);
	FTransform CutFrame(FRotationMatrix::MakeFromZ(RampNormal).Rotator(), RampPoint, FVector::OneVector);

	FGeometryScriptMeshPlaneCutOptions CutOpts;
	CutOpts.bFillHoles = true;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshPlaneCut(Mesh, CutFrame, CutOpts);

	return true;
}

bool FMonolithMeshProceduralActions::BuildPillar(UDynamicMesh* Mesh, float Radius, float Height,
	int32 Sides, bool bRound, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;
	FTransform PillarXf = FTransform::Identity;

	if (bRound)
	{
		Sides = FMath::Clamp(Sides, 6, 64);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(
			Mesh, Opts, PillarXf, Radius, Height, Sides, 0, true, EGeometryScriptPrimitiveOriginMode::Base);
	}
	else
	{
		// Square pillar
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			Mesh, Opts, PillarXf, Radius * 2, Radius * 2, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
	}

	return true;
}

bool FMonolithMeshProceduralActions::BuildCounter(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float TopThickness, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;

	// Counter top
	FTransform TopXf(FRotator::ZeroRotator, FVector(0, 0, Height - TopThickness), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, TopXf, Width, Depth, TopThickness, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Base cabinet body (inset slightly from top)
	float BaseInset = 3.0f;
	float BaseH = Height - TopThickness - 10.0f; // kick space at bottom
	FTransform BaseXf(FRotator::ZeroRotator, FVector(0, BaseInset, 10.0f), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, BaseXf, Width - BaseInset * 2, Depth - BaseInset, BaseH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	return true;
}

bool FMonolithMeshProceduralActions::BuildToilet(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float BowlDepth, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;

	// Base/tank (box)
	FTransform BaseXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, BaseXf, Width, Depth * 0.5f, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Bowl rim (cylinder at front)
	float BowlRadius = Width * 0.45f;
	FTransform BowlXf(FRotator::ZeroRotator, FVector(0, Depth * 0.25f, Height * 0.6f), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(Mesh, Opts, BowlXf, BowlRadius, Height * 0.4f, 12, 0, true, EGeometryScriptPrimitiveOriginMode::Base);

	// Bowl interior subtraction (sphere)
	UDynamicMesh* BowlCut = NewObject<UDynamicMesh>(Pool);
	float CutRadius = BowlRadius * 0.8f;
	FTransform CutXf(FRotator::ZeroRotator, FVector(0, Depth * 0.25f, Height - BowlDepth + CutRadius * 0.3f), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(BowlCut, Opts, CutXf, CutRadius, 8, 12, EGeometryScriptPrimitiveOriginMode::Center);

	FGeometryScriptMeshBooleanOptions BoolOpts;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Mesh, FTransform::Identity, BowlCut, FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract, BoolOpts);

	return true;
}

bool FMonolithMeshProceduralActions::BuildSink(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float BowlRadius, float BowlDepth, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;

	// Pedestal / vanity
	FTransform BaseXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, BaseXf, Width, Depth, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Bowl subtraction (sphere centered at top surface)
	UDynamicMesh* BowlCut = NewObject<UDynamicMesh>(Pool);
	FTransform CutXf(FRotator::ZeroRotator, FVector(0, Depth * 0.1f, Height - BowlDepth + BowlRadius * 0.4f), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(BowlCut, Opts, CutXf, BowlRadius, 8, 12, EGeometryScriptPrimitiveOriginMode::Center);

	FGeometryScriptMeshBooleanOptions BoolOpts;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Mesh, FTransform::Identity, BowlCut, FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract, BoolOpts);

	return true;
}

bool FMonolithMeshProceduralActions::BuildBathtub(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float WallThickness, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;

	// Outer shell
	FTransform OuterXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, OuterXf, Width, Depth, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Inner subtraction (smaller box from top)
	UDynamicMesh* InnerCut = NewObject<UDynamicMesh>(Pool);
	float CutW = Width - WallThickness * 2;
	float CutD = Depth - WallThickness * 2;
	float CutH = Height - WallThickness; // leave bottom thickness
	FTransform CutXf(FRotator::ZeroRotator, FVector(0, 0, WallThickness), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(InnerCut, Opts, CutXf, CutW, CutD, CutH + 10.0f, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	FGeometryScriptMeshBooleanOptions BoolOpts;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Mesh, FTransform::Identity, InnerCut, FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract, BoolOpts);

	return true;
}

// ============================================================================
// Horror Prop Builders
// ============================================================================

bool FMonolithMeshProceduralActions::BuildBarricade(UDynamicMesh* Mesh, float Width, float Height, float Depth,
	int32 BoardCount, float GapRatio, int32 Seed, FString& OutError)
{
	BoardCount = FMath::Clamp(BoardCount, 2, 20);
	GapRatio = FMath::Clamp(GapRatio, 0.05f, 0.8f);

	FRandomStream Rng(Seed);
	FGeometryScriptPrimitiveOptions Opts;

	float TotalSlots = BoardCount + (BoardCount - 1) * GapRatio;
	float BoardH = Height / TotalSlots;
	float GapH = BoardH * GapRatio;

	// Create one plank template
	UDynamicMesh* Plank = NewObject<UDynamicMesh>(Pool);
	FTransform PlankXf = FTransform::Identity;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Plank, Opts, PlankXf, Width, Depth, BoardH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Place planks with random offsets and slight rotations
	TArray<FTransform> PlankTransforms;
	for (int32 i = 0; i < BoardCount; ++i)
	{
		float Z = i * (BoardH + GapH);
		float OffX = Rng.FRandRange(-Width * 0.05f, Width * 0.05f);
		float RotRoll = Rng.FRandRange(-3.0f, 3.0f);

		PlankTransforms.Add(FTransform(FRotator(0, 0, RotRoll), FVector(OffX, 0, Z), FVector::OneVector));
	}

	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshTransformed(
		Mesh, Plank, PlankTransforms, FTransform::Identity, true);

	return true;
}

bool FMonolithMeshProceduralActions::BuildDebrisPile(UDynamicMesh* Mesh, float Radius, float Height,
	int32 PieceCount, int32 Seed, FString& OutError)
{
	PieceCount = FMath::Clamp(PieceCount, 3, 50);
	FRandomStream Rng(Seed);
	FGeometryScriptPrimitiveOptions Opts;

	for (int32 i = 0; i < PieceCount; ++i)
	{
		// Random position within radius, biased toward center and ground
		float Angle = Rng.FRandRange(0.0f, 360.0f);
		float Dist = Rng.FRandRange(0.0f, Radius) * Rng.FRandRange(0.3f, 1.0f); // center bias
		float Z = Rng.FRandRange(0.0f, Height * 0.7f);

		FVector Pos(
			FMath::Cos(FMath::DegreesToRadians(Angle)) * Dist,
			FMath::Sin(FMath::DegreesToRadians(Angle)) * Dist,
			Z);

		FRotator Rot(Rng.FRandRange(-30, 30), Rng.FRandRange(0, 360), Rng.FRandRange(-30, 30));

		// Random piece size
		float SizeX = Rng.FRandRange(5, Radius * 0.3f);
		float SizeY = Rng.FRandRange(5, Radius * 0.3f);
		float SizeZ = Rng.FRandRange(3, Height * 0.3f);

		FTransform PieceXf(Rot, Pos, FVector::OneVector);

		// Alternate between boxes and cylinders for variety
		if (i % 3 == 0)
		{
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(
				Mesh, Opts, PieceXf, SizeX * 0.5f, SizeZ, 6, 0, true, EGeometryScriptPrimitiveOriginMode::Base);
		}
		else
		{
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
				Mesh, Opts, PieceXf, SizeX, SizeY, SizeZ, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
		}
	}

	return true;
}

bool FMonolithMeshProceduralActions::BuildCage(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	int32 BarCount, float BarRadius, FString& OutError)
{
	BarCount = FMath::Clamp(BarCount, 3, 50);
	FGeometryScriptPrimitiveOptions Opts;

	// Create one vertical bar template
	UDynamicMesh* Bar = NewObject<UDynamicMesh>(Pool);
	FTransform BarXf = FTransform::Identity;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(Bar, Opts, BarXf, BarRadius, Height, 6, 0, true, EGeometryScriptPrimitiveOriginMode::Base);

	// Front face bars using AppendMeshRepeated
	float FrontSpacing = Width / FMath::Max(1, BarCount - 1);
	FTransform FrontStart(FRotator::ZeroRotator, FVector(-Width * 0.5f, Depth * 0.5f, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, Bar, FrontStart);
	if (BarCount > 1)
	{
		FTransform FrontStep(FRotator::ZeroRotator, FVector(FrontSpacing, 0, 0), FVector::OneVector);
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshRepeated(Mesh, Bar, FrontStep, BarCount - 1, true);

		// Offset for back face (start from first back bar position)
		// We appended front bars at Y = +Depth/2, now do back at Y = -Depth/2
	}

	// Back face bars
	FTransform BackStart(FRotator::ZeroRotator, FVector(-Width * 0.5f, -Depth * 0.5f, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, Bar, BackStart);
	if (BarCount > 1)
	{
		FTransform BackStep(FRotator::ZeroRotator, FVector(FrontSpacing, 0, 0), FVector::OneVector);
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshRepeated(Mesh, Bar, BackStep, BarCount - 1, true);
	}

	// Side bars (fewer, connecting front/back)
	int32 SideBarCount = FMath::Max(2, BarCount / 2);
	float SideSpacing = Depth / FMath::Max(1, SideBarCount - 1);

	FTransform LeftStart(FRotator::ZeroRotator, FVector(-Width * 0.5f, -Depth * 0.5f, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, Bar, LeftStart);
	if (SideBarCount > 1)
	{
		FTransform LeftStep(FRotator::ZeroRotator, FVector(0, SideSpacing, 0), FVector::OneVector);
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshRepeated(Mesh, Bar, LeftStep, SideBarCount - 1, true);
	}

	FTransform RightStart(FRotator::ZeroRotator, FVector(Width * 0.5f, -Depth * 0.5f, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, Bar, RightStart);
	if (SideBarCount > 1)
	{
		FTransform RightStep(FRotator::ZeroRotator, FVector(0, SideSpacing, 0), FVector::OneVector);
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshRepeated(Mesh, Bar, RightStep, SideBarCount - 1, true);
	}

	// Top frame (4 horizontal bars)
	UDynamicMesh* TopBarX = NewObject<UDynamicMesh>(Pool);
	FTransform TopBarXXf(FRotator(0, 0, 90), FVector(0, 0, 0), FVector::OneVector); // rotated to lie horizontal along X
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(TopBarX, Opts, TopBarXXf, BarRadius * 1.5f, Width, 6, 0, true, EGeometryScriptPrimitiveOriginMode::Base);

	FTransform TopFrontXf(FRotator::ZeroRotator, FVector(-Width * 0.5f, Depth * 0.5f, Height), FVector::OneVector);
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, TopBarX, TopFrontXf);
	FTransform TopBackXf(FRotator::ZeroRotator, FVector(-Width * 0.5f, -Depth * 0.5f, Height), FVector::OneVector);
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, TopBarX, TopBackXf);

	UDynamicMesh* TopBarY = NewObject<UDynamicMesh>(Pool);
	FTransform TopBarYXf(FRotator(90, 0, 0), FVector(0, 0, 0), FVector::OneVector); // rotated along Y
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(TopBarY, Opts, TopBarYXf, BarRadius * 1.5f, Depth, 6, 0, true, EGeometryScriptPrimitiveOriginMode::Base);

	FTransform TopLeftXf(FRotator::ZeroRotator, FVector(-Width * 0.5f, -Depth * 0.5f, Height), FVector::OneVector);
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, TopBarY, TopLeftXf);
	FTransform TopRightXf(FRotator::ZeroRotator, FVector(Width * 0.5f, -Depth * 0.5f, Height), FVector::OneVector);
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, TopBarY, TopRightXf);

	return true;
}

bool FMonolithMeshProceduralActions::BuildCoffin(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float WallThickness, float LidGap, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;

	// Base box (the coffin body)
	float BodyH = Height * 0.7f;
	FTransform BodyXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, BodyXf, Width, Depth, BodyH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Interior subtraction
	UDynamicMesh* InnerCut = NewObject<UDynamicMesh>(Pool);
	float CutW = Width - WallThickness * 2;
	float CutD = Depth - WallThickness * 2;
	float CutH = BodyH - WallThickness;
	FTransform CutXf(FRotator::ZeroRotator, FVector(0, 0, WallThickness), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(InnerCut, Opts, CutXf, CutW, CutD, CutH + 10.0f, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	FGeometryScriptMeshBooleanOptions BoolOpts;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Mesh, FTransform::Identity, InnerCut, FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract, BoolOpts);

	// Lid (separate box, slightly offset/open)
	float LidH = Height - BodyH;
	FTransform LidXf(FRotator(5.0f, 0, 0), FVector(0, LidGap, BodyH + LidGap), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, LidXf, Width, Depth, LidH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	return true;
}

bool FMonolithMeshProceduralActions::BuildGurney(UDynamicMesh* Mesh, float Width, float Depth, float Height,
	float LegRadius, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;

	float TopThickness = 3.0f;
	float LegHeight = Height - TopThickness;

	// Flat surface top
	FTransform TopXf(FRotator::ZeroRotator, FVector(0, 0, LegHeight), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, TopXf, Width, Depth, TopThickness, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// 4 legs (thin cylinders)
	float InX = Width * 0.5f - LegRadius * 2;
	float InY = Depth * 0.5f - LegRadius * 2;
	FVector2D Corners[] = { {-InX,-InY}, {InX,-InY}, {-InX,InY}, {InX,InY} };

	for (const auto& C : Corners)
	{
		FTransform LegXf(FRotator::ZeroRotator, FVector(C.X, C.Y, 0), FVector::OneVector);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(Mesh, Opts, LegXf, LegRadius, LegHeight, 8, 0, true, EGeometryScriptPrimitiveOriginMode::Base);
	}

	// Side rails
	float RailHeight = 15.0f;
	float RailThickness = 1.5f;
	FTransform LeftRailXf(FRotator::ZeroRotator, FVector(-Width * 0.5f + RailThickness * 0.5f, 0, LegHeight + TopThickness), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, LeftRailXf, RailThickness, Depth * 0.6f, RailHeight, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
	FTransform RightRailXf(FRotator::ZeroRotator, FVector(Width * 0.5f - RailThickness * 0.5f, 0, LegHeight + TopThickness), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, RightRailXf, RailThickness, Depth * 0.6f, RailHeight, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	return true;
}

bool FMonolithMeshProceduralActions::BuildBrokenWall(UDynamicMesh* Mesh, float Width, float Height, float Thickness,
	float NoiseScale, float HoleRadius, int32 Seed, FString& OutError)
{
	FGeometryScriptPrimitiveOptions Opts;

	// Wall with subdivisions (needed so noise has vertices to deform)
	int32 SubsX = FMath::Max(1, FMath::RoundToInt32(Width / 15.0f));
	int32 SubsZ = FMath::Max(1, FMath::RoundToInt32(Height / 15.0f));
	FTransform WallXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, WallXf, Width, Thickness, Height, SubsX, 1, SubsZ, EGeometryScriptPrimitiveOriginMode::Base);

	// Create noise-deformed sphere as cutter
	UDynamicMesh* Cutter = NewObject<UDynamicMesh>(Pool);
	FTransform SphereXf(FRotator::ZeroRotator, FVector(0, 0, Height * 0.5f), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
		Cutter, Opts, SphereXf, HoleRadius, 12, 16, EGeometryScriptPrimitiveOriginMode::Center);

	// Apply Perlin noise to the cutter sphere for irregular hole shape
	FGeometryScriptPerlinNoiseOptions NoiseOpts;
	NoiseOpts.BaseLayer.Magnitude = HoleRadius * NoiseScale;
	NoiseOpts.BaseLayer.Frequency = 0.03f;
	NoiseOpts.BaseLayer.RandomSeed = Seed;
	NoiseOpts.bApplyAlongNormal = true;

	FGeometryScriptMeshSelection EmptySelection; // empty = full mesh
	UGeometryScriptLibrary_MeshDeformFunctions::ApplyPerlinNoiseToMesh2(Cutter, EmptySelection, NoiseOpts);

	// Boolean subtract the deformed sphere from the wall
	FGeometryScriptMeshBooleanOptions BoolOpts;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Mesh, FTransform::Identity, Cutter, FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract, BoolOpts);

	return true;
}

bool FMonolithMeshProceduralActions::BuildVentGrate(UDynamicMesh* Mesh, float Width, float Height, float Depth,
	int32 SlotCount, float FrameThickness, FString& OutError)
{
	SlotCount = FMath::Clamp(SlotCount, 1, 30);
	FGeometryScriptPrimitiveOptions Opts;

	// Outer frame
	FTransform FrameXf(FRotator::ZeroRotator, FVector(0, 0, 0), FVector::OneVector);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opts, FrameXf, Width, Depth, Height, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// Horizontal bars using AppendMeshRepeated
	float InnerW = Width - FrameThickness * 2;
	float BarThickness = 1.5f;
	float TotalSlotH = Height - FrameThickness * 2;
	float Spacing = TotalSlotH / (SlotCount + 1); // even spacing including bars

	UDynamicMesh* BarTemplate = NewObject<UDynamicMesh>(Pool);
	FTransform BarXf = FTransform::Identity;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(BarTemplate, Opts, BarXf, InnerW, Depth + 2.0f, BarThickness, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);

	// First bar
	FTransform FirstBarXf(FRotator::ZeroRotator, FVector(0, 0, FrameThickness + Spacing - BarThickness * 0.5f), FVector::OneVector);
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, BarTemplate, FirstBarXf);

	// Remaining bars
	if (SlotCount > 1)
	{
		FTransform StepXf(FRotator::ZeroRotator, FVector(0, 0, Spacing), FVector::OneVector);
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshRepeated(Mesh, BarTemplate, StepXf, SlotCount - 1, true);
	}

	return true;
}

#endif // WITH_GEOMETRYSCRIPT
