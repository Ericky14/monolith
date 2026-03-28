#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

#if WITH_GEOMETRYSCRIPT

class UMonolithMeshHandlePool;
class UDynamicMesh;

/**
 * Phase 19A: Procedural Geometry Actions (GeometryScript)
 * Parametric furniture + horror prop generation via boolean ops on primitives.
 * 2 actions: create_parametric_mesh, create_horror_prop
 */
class FMonolithMeshProceduralActions
{
public:
	/** Register all procedural geometry actions with the tool registry */
	static void RegisterActions(FMonolithToolRegistry& Registry);

	/** Set the handle pool instance (called during module startup) */
	static void SetHandlePool(UMonolithMeshHandlePool* InPool);

private:
	static UMonolithMeshHandlePool* Pool;

	// Action handlers
	static FMonolithActionResult CreateParametricMesh(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult CreateHorrorProp(const TSharedPtr<FJsonObject>& Params);

	// ---- Parametric furniture builders ----
	// Each returns triangle count. Mesh is built in-place on TargetMesh.
	static bool BuildChair(UDynamicMesh* Mesh, float Width, float Depth, float Height, float SeatHeight, float BackHeight, float LegThickness, FString& OutError);
	static bool BuildTable(UDynamicMesh* Mesh, float Width, float Depth, float Height, float LegThickness, float TopThickness, FString& OutError);
	static bool BuildDesk(UDynamicMesh* Mesh, float Width, float Depth, float Height, float LegThickness, float TopThickness, bool bHasDrawer, FString& OutError);
	static bool BuildShelf(UDynamicMesh* Mesh, float Width, float Depth, float Height, int32 ShelfCount, float BoardThickness, FString& OutError);
	static bool BuildCabinet(UDynamicMesh* Mesh, float Width, float Depth, float Height, float WallThickness, float RecessDepth, FString& OutError);
	static bool BuildBed(UDynamicMesh* Mesh, float Width, float Depth, float Height, float MattressHeight, float HeadboardHeight, FString& OutError);
	static bool BuildDoorFrame(UDynamicMesh* Mesh, float Width, float Height, float FrameThickness, float FrameDepth, FString& OutError);
	static bool BuildWindowFrame(UDynamicMesh* Mesh, float Width, float Height, float FrameThickness, float FrameDepth, float SillHeight, FString& OutError);
	static bool BuildStairs(UDynamicMesh* Mesh, float Width, float StepHeight, float StepDepth, int32 StepCount, bool bFloating, FString& OutError);
	static bool BuildRamp(UDynamicMesh* Mesh, float Width, float Depth, float Height, FString& OutError);
	static bool BuildPillar(UDynamicMesh* Mesh, float Radius, float Height, int32 Sides, bool bRound, FString& OutError);
	static bool BuildCounter(UDynamicMesh* Mesh, float Width, float Depth, float Height, float TopThickness, FString& OutError);
	static bool BuildToilet(UDynamicMesh* Mesh, float Width, float Depth, float Height, float BowlDepth, FString& OutError);
	static bool BuildSink(UDynamicMesh* Mesh, float Width, float Depth, float Height, float BowlRadius, float BowlDepth, FString& OutError);
	static bool BuildBathtub(UDynamicMesh* Mesh, float Width, float Depth, float Height, float WallThickness, FString& OutError);

	// ---- Horror prop builders ----
	static bool BuildBarricade(UDynamicMesh* Mesh, float Width, float Height, float Depth, int32 BoardCount, float GapRatio, int32 Seed, FString& OutError);
	static bool BuildDebrisPile(UDynamicMesh* Mesh, float Radius, float Height, int32 PieceCount, int32 Seed, FString& OutError);
	static bool BuildCage(UDynamicMesh* Mesh, float Width, float Depth, float Height, int32 BarCount, float BarRadius, FString& OutError);
	static bool BuildCoffin(UDynamicMesh* Mesh, float Width, float Depth, float Height, float WallThickness, float LidGap, FString& OutError);
	static bool BuildGurney(UDynamicMesh* Mesh, float Width, float Depth, float Height, float LegRadius, FString& OutError);
	static bool BuildBrokenWall(UDynamicMesh* Mesh, float Width, float Height, float Thickness, float NoiseScale, float HoleRadius, int32 Seed, FString& OutError);
	static bool BuildVentGrate(UDynamicMesh* Mesh, float Width, float Height, float Depth, int32 SlotCount, float FrameThickness, FString& OutError);

	// ---- Shared helpers ----
	/** Optionally save the built mesh to a UStaticMesh asset. Returns save_path in result. */
	static bool SaveMeshToAsset(UDynamicMesh* Mesh, const FString& SavePath, bool bOverwrite, FString& OutError);

	/** Optionally place a StaticMesh actor in the scene */
	static AActor* PlaceMeshInScene(const FString& AssetPath, const FVector& Location, const FRotator& Rotation, const FString& Label);

	/** Apply final cleanup: SelfUnion (additive-only) or ComputeSplitNormals (post-boolean) */
	static void CleanupMesh(UDynamicMesh* Mesh, bool bHadBooleans);

	/** Parse a "dimensions" sub-object, filling defaults from the provided values */
	static void ParseDimensions(const TSharedPtr<FJsonObject>& Params, float& Width, float& Depth, float& Height,
		float DefaultWidth = 100.0f, float DefaultDepth = 100.0f, float DefaultHeight = 100.0f);

	/** Parse a "params" sub-object, returning it (or empty object if absent) */
	static TSharedPtr<FJsonObject> ParseSubParams(const TSharedPtr<FJsonObject>& Params);
};

#endif // WITH_GEOMETRYSCRIPT
