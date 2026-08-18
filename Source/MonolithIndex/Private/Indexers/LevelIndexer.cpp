#include "Indexers/LevelIndexer.h"
#include "MonolithMemoryHelper.h"
#include "MonolithSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "WorldPartition/WorldPartition.h"
#include "Subsystems/WorldSubsystem.h"
#include "RenderingThread.h"
#include "RHIGlobals.h"
#include "UObject/LinkerInstancingContext.h"
#include "Misc/PackagePath.h"

// WHY THIS PASS IS DRIVEN ONE BATCH PER GAME-THREAD CALL.
//
// Indexing a level means LoadPackage'ing a world, which pulls in its meshes and registers every
// Nanite resource with the global streaming manager. That registration is queued work: the
// renderer only settles it when a FRAME runs.
//
// The original pass looped all 161 levels inside a SINGLE game-thread call, so the editor
// rendered ZERO frames for its entire ~80s (measured: the frame counter sat on 229 start to
// finish, while the deep-index pass - which dispatches one game-thread call PER BATCH - advanced
// 15 -> 228 over the same kind of work and never had a problem). Nothing was leaking during the
// pass: reserved GPU VA held a healthy 58.5 GB throughout. The damage landed on the FIRST FRAME
// AFTERWARDS, which processed 161 levels of backlog at once, reallocating pooled buffers in a
// storm until reserved VA hit 258 GB against a 256 GB budget and CreateReservedResource returned
// DXGI_ERROR_DEVICE_REMOVED. That is why the crash always struck ~5-7s AFTER this pass logged
// success, with UpdateAllPrimitiveSceneInfos / SubmitBufferUploads on the GPU breadcrumb.
// Reproduced 4/4 with the pass enabled, 0/10 without.
//
// So the fix is not to free more - it is to never let the backlog build: return to the main loop
// between batches so the renderer digests a few levels at a time, exactly like the deep pass.
// FinishPass() logs the measured peak so a regression shows up as a number, not as a dead GPU.
namespace
{
	/** Peak live reserved VA seen during one level pass. Reset by BeginPass(). */
	int64 GPeakReservedVirtualBytes = 0;

	/**
	 * GRHIGlobals.ReservedResources.VirtualSize is a LIVE GAUGE, not a running total: RHICoreStats
	 * adds a signed delta and the decrement runs in ~FD3D12Buffer. A high reading therefore means
	 * that much reserved VA is genuinely alive right now - the condition that removes the device.
	 */
	int64 ReadReservedVirtualBytes()
	{
		return FPlatformAtomics::AtomicRead(&GRHIGlobals.ReservedResources.VirtualSize);
	}

	void SampleReservedVirtualBytes()
	{
		GPeakReservedVirtualBytes = FMath::Max(GPeakReservedVirtualBytes, ReadReservedVirtualBytes());
	}

	/**
	 * Canonical GC partner in a bulk-load loop: FlushRenderingCommands enqueues
	 * FlushPendingDeleteRHIResourcesCmd, which double-flushes the deferred RHI deletion queue
	 * (RenderingThread.cpp). Honest scope note: this was NOT what fixed the device removal - it
	 * measured 0.00 GB reclaimed on every call, because the failure was queued ALLOCATIONS, not
	 * stranded deletions. It is kept because releases genuinely can be pending after a GC and
	 * draining them here is nearly free.
	 */
	void ReleasePendingRenderResources()
	{
		const int64 Before = ReadReservedVirtualBytes();
		GPeakReservedVirtualBytes = FMath::Max(GPeakReservedVirtualBytes, Before);

		FlushRenderingCommands();

		const int64 After = ReadReservedVirtualBytes();
		if (Before != After)
		{
			UE_LOG(LogMonolithIndex, Verbose,
				TEXT("LevelIndexer: reserved GPU VA %.2f GB -> %.2f GB (reclaimed %.2f GB)"),
				Before / double(1LL << 30), After / double(1LL << 30),
				(Before - After) / double(1LL << 30));
		}
	}
}

bool FLevelIndexer::IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId)
{
	// Deliberately inert - see the header. Running the whole pass in one call is the exact
	// shape that removed the GPU device, so this path stays closed rather than merely discouraged.
	UE_LOG(LogMonolithIndex, Warning,
		TEXT("LevelIndexer::IndexAsset called directly and ignored. Drive the pass with GatherWorldAssets() + IndexWorldBatch() per game-thread dispatch + FinishPass()."));
	return false;
}

TArray<FAssetData> FLevelIndexer::GatherWorldAssets() const
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FAssetData> WorldAssets;
	FARFilter Filter;
	if (IndexedPaths.Num() > 0)
	{
		for (const FName& Path : IndexedPaths)
		{
			Filter.PackagePaths.Add(Path);
		}
	}
	else
	{
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
	}
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
	Registry.GetAssets(Filter, WorldAssets);

	return WorldAssets;
}

void FLevelIndexer::BeginPass()
{
	ActorsInserted = 0;
	LevelsProcessed = 0;
	InstanceCounter = 0;
	InitializedWorldCount = 0;
	OriginTransformCount = 0;
	TransformSampleCount = 0;
	GPeakReservedVirtualBytes = 0;

	if (GetDefault<UMonolithSettings>()->bLogMemoryStats)
	{
		FMonolithMemoryHelper::LogMemoryStats(TEXT("LevelIndexer start"));
	}
}

void FLevelIndexer::IndexWorldBatch(TArrayView<const FAssetData> Batch, FMonolithIndexDatabase& DB)
{
	const UMonolithSettings* Settings = GetDefault<UMonolithSettings>();
	const SIZE_T MemoryBudgetMB = static_cast<SIZE_T>(FMonolithMemoryHelper::GetResolvedMemoryBudgetMB());
	const bool bLogMemory = Settings->bLogMemoryStats;

	// Memory budget check before the batch
	if (FMonolithMemoryHelper::ShouldThrottle(MemoryBudgetMB))
	{
		UE_LOG(LogMonolithIndex, Log, TEXT("LevelIndexer: Memory budget exceeded, forcing GC..."));
		FMonolithMemoryHelper::ForceGarbageCollection(true);
		ReleasePendingRenderResources();

		if (bLogMemory)
		{
			FMonolithMemoryHelper::LogMemoryStats(TEXT("LevelIndexer after throttle GC"));
		}
	}

	for (const FAssetData& WorldData : Batch)
	{
		int64 LevelAssetId = DB.GetAssetId(WorldData.PackageName.ToString());
		if (LevelAssetId < 0) continue;

		// Load the world INSTANCED. This is the fix for the GPU device removal - see below.
		//
		// A plain LoadPackage of a world does NOT stay inert in the editor: UEditorEngine::OnAssetLoaded
		// -> InitializeNewlyCreatedInactiveWorld (EditorEngine.cpp) calls World->InitWorld() +
		// UpdateWorldComponents() on every freshly loaded Inactive world, which allocates a real
		// FScene (World.cpp -> AllocateScene). Those 161 scenes never render a single frame, so their
		// GPUScene buffers are not allocated during life - they are allocated at TEARDOWN, when
		// FScene::Release runs an "UpdateAllPrimitiveSceneInfos" render graph and GPUScene::Update
		// allocates precisely because !Buffer.IsValid(). That is FOUR 2 GB RESERVED-VA buffers per
		// world (GPUScene.cpp: InstanceSceneData + PrimitiveData + InstancePayloadData + LightmapData).
		// Nothing recycles them: FRDGBufferPool only evicts after 30 RENDERED frames, and this pass
		// renders ~1 frame per batch. Measured: reserved VA climbed in exact 8 GB steps per world torn
		// down, hit the 256 GB budget, and CreateReservedResource returned DXGI_ERROR_DEVICE_REMOVED.
		//
		// InitializeNewlyCreatedInactiveWorld explicitly skips instanced worlds (its gate is
		// !World->IsInstanced()), and UWorld::IsInstanced() is true when the package's own name
		// differs from the path it was loaded from. Loading under a unique instance name therefore
		// means no InitWorld, no FScene, no GPUScene, and no reserved allocations at all - and costs
		// us nothing, because this indexer only reads Level->Actors metadata and never needed a
		// render scene. Verify with the "peak reserved GPU VA" line FinishPass() logs.
		const FString SourcePackageName = WorldData.PackageName.ToString();
		const FString InstancedPackageName = FString::Printf(TEXT("%s_MonolithIndex%d"),
			*SourcePackageName, InstanceCounter++);

		FLinkerInstancingContext InstancingContext;
		InstancingContext.AddPackageMapping(FName(*SourcePackageName), FName(*InstancedPackageName));

		UPackage* InstancePackage = CreatePackage(*InstancedPackageName);
		if (!InstancePackage) continue;

		UPackage* Package = LoadPackage(InstancePackage,
			FPackagePath::FromPackageNameChecked(SourcePackageName),
			LOAD_NoWarn | LOAD_Quiet, nullptr, &InstancingContext);
		if (!Package) continue;

		UWorld* World = FindObject<UWorld>(Package, *WorldData.AssetName.ToString());
		if (!World)
		{
			// Try the common naming convention
			World = FindObject<UWorld>(Package, TEXT("World"));
		}
		if (!World || !World->PersistentLevel)
		{
			// Mark package for unload even if we couldn't find the world
			FMonolithMemoryHelper::TryUnloadPackage(Package, /*bWasAlreadyLoaded=*/false);
			continue;
		}

		// Canary for the instanced-load fix above. If this ever trips, the world WAS initialized,
		// meaning an FScene exists and its teardown will allocate 4 x 2 GB of reserved VA - the
		// exact path that removed the GPU device. Reflection-free and cheap, so it stays in.
		if (World->bIsWorldInitialized)
		{
			++InitializedWorldCount;
		}

		// Only index the persistent level - skip streaming sub-levels for performance
		ULevel* Level = World->PersistentLevel;
		ActorsInserted += IndexActorsInLevel(Level, DB, LevelAssetId);

		// Detect whether this world has a landscape subsystem. LoadPackage never calls UWorld::InitWorld, but a
		// landscape actor's PostRegisterAllComponents lazily creates + Initialize()s a ULandscapeSubsystem on
		// this never-InitWorld'd world. If we then tear the world down (TryUnloadPackage + GC), the GC destroys
		// the world while ULandscapeSubsystem is still bInitialized -> handled-but-noisy ensure at
		// WorldSubsystem.cpp:158 (issue #67).
		//
		// The shipped fix kept landscape worlds resident (RF_Standalone intact) to dodge both the ensure and a
		// fatal crash in ULandscapeSubsystem::Deinitialize (its grass-builder destructor dereferences the null
		// World->Scene on a no-render-scene Inactive world). That avoided ~80 resident UWorlds per reindex.
		//
		// Issue #67 optimization (this branch): tear the landscape world down safely instead of leaving it
		// resident, by UNREGISTERING every landscape proxy's components FIRST, then driving CleanupWorld:
		//   1. UnregisterAllComponents() on each ALandscapeProxy cascades to ULandscapeComponent::OnUnregister ->
		//      ULandscapeSubsystem::UnregisterComponent (Landscape.cpp:2360) -> FLandscapeGrassMapsBuilder::
		//      UnregisterComponent (LandscapeGrassMapsBuilder.cpp:806-820), which NULLs each FComponentState
		//      (State->Component = nullptr) and touches NO render scene.
		//   2. World->CleanupWorld() then Deinitialize()s the WHOLE world subsystem collection. ULandscapeSubsystem::
		//      Deinitialize deletes the grass builder; its destructor's evict loop now early-exits on every
		//      Component==nullptr state (the bCancelAndEvictAllImmediately/Component==nullptr branch) and never
		//      reaches CanCurrentlyRender()/World->Scene -> no crash. Super::Deinitialize clears bInitialized ->
		//      no GC-time ensure. CleanupWorld also clears RF_Standalone (a superset of TryUnloadPackage) and
		//      drives WorldPartition uninit, so the landscape branch needs NEITHER the separate WP-uninit NOR
		//      TryUnloadPackage that the non-landscape branch below still uses.
		// This is the key reversal vs. the shipped fix: CleanupWorld was previously FATAL precisely because it ran
		// Deinitialize while grass states still held live components; unregister-first makes it safe.
		//
		// RESIDUAL RISK (accepted, covered by the runtime acceptance gate + rollback, NOT pre-guarded here per
		// issue #67 plan section 6): CleanupWorld drives Deinitialize on EVERY world subsystem, not just landscape.
		// We cannot call ULandscapeSubsystem::Deinitialize directly (it is private), so full CleanupWorld is the
		// only lever. Another auto-initialized world subsystem could, in its own Deinitialize, deref the null
		// World->Scene on this no-render-scene world and crash. If the acceptance gate crashes, roll back to the
		// shipped resident-skip behavior (do not regress the 0-ensure / 0-crash guarantee for the memory win).
		//
		// We detect the ULandscapeSubsystem itself rather than scanning for an ALandscapeProxy actor, because the
		// subsystem is the thing that ensures at GC, and in World Partition / streaming worlds the landscape actor
		// can live in a streaming sublevel or as an external WP actor that is NOT present in PersistentLevel->Actors
		// (issue #67 refinement: LVL_NiagaraDestructionDriver_Demo_Cube still tripped the ensure with a persistent-
		// level-only actor scan because its landscape lives outside the persistent level, but its ULandscapeSubsystem
		// was initialized). For the same reason the unregister pass below uses a world-wide TActorIterator (which
		// covers PersistentLevel + all streaming sublevels) rather than scanning PersistentLevel->Actors only.
		// Resolve both the subsystem class and the proxy class by script path so we avoid a Landscape Build.cs
		// dependency (which would force a full rebuild + hard link); if a class can't be resolved (Landscape module
		// not loaded) there can be no landscape subsystem, so we safely fall back to the original teardown.
		// GetSubsystemBase(TSubclassOf<UWorldSubsystem>) performs the lookup against the world's subsystem
		// collection without a compile-time type dependency and returns non-null iff the subsystem instance exists.
		bool bContainsLandscape = false;
		{
			static UClass* LandscapeSubsystemClass = FindObject<UClass>(nullptr, TEXT("/Script/Landscape.LandscapeSubsystem"));
			if (LandscapeSubsystemClass)
			{
				if (World->GetSubsystemBase(LandscapeSubsystemClass) != nullptr)
				{
					bContainsLandscape = true;
				}
			}
		}

		// Skip teardown if this is the world currently open in the editor - uninit would stop viewport WP cell streaming and unload would close the level
		UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World != EditorWorld)
		{
			if (bContainsLandscape)
			{
				// Unregister every landscape proxy's components BEFORE teardown so the grass-builder destructor
				// (driven by CleanupWorld below) never dereferences the null World->Scene. See the block comment
				// above for the full mechanism. World-wide iteration (persistent level + all streaming sublevels)
				// is required because landscape proxies can live outside PersistentLevel->Actors in WP/streaming
				// worlds. The proxy class is resolved by script path to avoid a Landscape Build.cs dependency.
				int32 UnregisteredProxies = 0;
				static UClass* LandscapeProxyClass = FindObject<UClass>(nullptr, TEXT("/Script/Landscape.LandscapeProxy"));
				if (LandscapeProxyClass)
				{
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						AActor* Actor = *It;
						if (Actor && Actor->IsA(LandscapeProxyClass))
						{
							Actor->UnregisterAllComponents();
							++UnregisteredProxies;
						}
					}
				}

				// Now safe: the grass FComponentStates are all NULLed, so ULandscapeSubsystem::Deinitialize's
				// destructor early-exits without touching the render scene, and Super::Deinitialize clears
				// bInitialized (no GC-time ensure). CleanupWorld also drives WorldPartition uninit and clears
				// RF_Standalone (superset of TryUnloadPackage), so no separate WP-uninit / TryUnloadPackage is
				// needed for landscape worlds.
				World->CleanupWorld();

				UE_LOG(LogMonolithIndex, Verbose,
					TEXT("LevelIndexer: '%s' has a landscape subsystem - unregistered %d landscape proxies and cleaned up the world (issue #67 optimization)."),
					*WorldData.PackageName.ToString(), UnregisteredProxies);
			}
			else
			{
				// Uninitialize WorldPartition before unload - LoadPackage skips the editor teardown path, so GC would otherwise assert in UWorldPartitionSubsystem::Deinitialize
				if (UWorldPartition* WP = World->GetWorldPartition())
				{
					if (WP->IsInitialized())
					{
						WP->Uninitialize();
					}
				}

				// Mark world/package for unloading after indexing
				FMonolithMemoryHelper::TryUnloadPackage(World, /*bWasAlreadyLoaded=*/false);
			}
		}

		LevelsProcessed++;
	}

	// GC at the end of every batch, then sample the gauge. The caller returns to the main loop
	// straight after this, so the renderer gets a frame to digest what this batch queued.
	FMonolithMemoryHelper::ForceGarbageCollection(false);
	ReleasePendingRenderResources();
	SampleReservedVirtualBytes();
}

void FLevelIndexer::FinishPass()
{
	FMonolithMemoryHelper::ForceGarbageCollection(true);
	ReleasePendingRenderResources();
	SampleReservedVirtualBytes();

	UE_LOG(LogMonolithIndex, Log, TEXT("LevelIndexer: indexed %d levels, %d actors total"),
		LevelsProcessed, ActorsInserted);

	// Transform-resolution canary. Actor placement is the whole point of the actor index, and a
	// broken resolution path returns plausible-looking rows rather than an error, so gate on it.
	if (TransformSampleCount > 0)
	{
		const double OriginPct = 100.0 * OriginTransformCount / TransformSampleCount;
		if (OriginPct > 50.0)
		{
			UE_LOG(LogMonolithIndex, Warning,
				TEXT("LevelIndexer: %.1f%% of actors indexed at the ORIGIN (%d/%d). Transforms are almost certainly unresolved - check that IndexActorsInLevel still calls UpdateComponentToWorld, because instanced loading leaves components unregistered and GetActorTransform() then returns identity."),
				OriginPct, OriginTransformCount, TransformSampleCount);
		}
		else
		{
			UE_LOG(LogMonolithIndex, Log,
				TEXT("LevelIndexer: actor transforms resolved (%.1f%% at origin, %d/%d)."),
				OriginPct, OriginTransformCount, TransformSampleCount);
		}
	}

	// Instanced-load canary. 0 is the healthy answer; anything else means render scenes were built.
	if (InitializedWorldCount > 0)
	{
		UE_LOG(LogMonolithIndex, Warning,
			TEXT("LevelIndexer: %d/%d worlds loaded INITIALIZED - the instanced load is not suppressing UEditorEngine::InitializeNewlyCreatedInactiveWorld. Each such world builds an FScene whose teardown reserves 4 x 2 GB of GPU VA; expect the reserved-VA budget to be hit and the device removed."),
			InitializedWorldCount, LevelsProcessed);
	}
	else
	{
		UE_LOG(LogMonolithIndex, Log,
			TEXT("LevelIndexer: 0/%d worlds were initialized (no render scenes created, as intended)."),
			LevelsProcessed);
	}

	// The headroom number that matters. This pass used to spike to 258 GB on the frame after it
	// finished and remove the D3D12 device; the engine's own warning is once-per-process and
	// allocation-only (RHICoreStats.cpp), so its ABSENCE proves nothing. Report the measured peak.
	const double PeakGB = GPeakReservedVirtualBytes / double(1LL << 30);
	constexpr int32 WarnGB = 256;
	UE_LOG(LogMonolithIndex, Log,
		TEXT("LevelIndexer: peak reserved GPU VA %.2f GB (device-removal budget %d GB, cvar rhi.ReservedResources.VirtualSizeWarningGB)"),
		PeakGB, WarnGB);
	if (PeakGB > WarnGB * 0.5)
	{
		UE_LOG(LogMonolithIndex, Warning,
			TEXT("LevelIndexer: reserved GPU VA peaked at %.2f GB, over half the %d GB budget. Batches are queueing too much GPU work between frames - lower PostPassBatchSize before this reaches the budget and removes the GPU device."),
			PeakGB, WarnGB);
	}

	if (GetDefault<UMonolithSettings>()->bLogMemoryStats)
	{
		FMonolithMemoryHelper::LogMemoryStats(TEXT("LevelIndexer complete"));
	}
}

int32 FLevelIndexer::IndexActorsInLevel(ULevel* Level, FMonolithIndexDatabase& DB, int64 AssetId)
{
	if (!Level) return 0;

	int32 Count = 0;
	for (AActor* Actor : Level->Actors)
	{
		if (!Actor) continue;

		// Skip the world settings and default brush - they're internal
		if (Actor->IsA(AWorldSettings::StaticClass())) continue;

		// Resolve the world transform WITHOUT initializing the world.
		//
		// We load worlds instanced on purpose (see IndexWorldBatch) so the editor never
		// InitWorld's them - that is what stopped the GPU device removal. The side effect is
		// that components are never REGISTERED, so ComponentToWorld is never computed and
		// GetActorTransform() returns IDENTITY. Left unhandled that silently indexed every
		// actor in the project at (0,0,0) - measured at 99.7% of rows - which looks like data
		// but is not.
		//
		// USceneComponent::UpdateComponentToWorldWithParent has NO registration check and
		// recursively resolves an unresolved attach parent before itself, so this recomputes
		// the true world transform from the serialized relative transforms alone: no scene,
		// no registration, no InitWorld, and correct for attached actors too.
		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			Root->UpdateComponentToWorld();
		}

		FIndexedActor IndexedActor;
		IndexedActor.AssetId = AssetId;
		IndexedActor.ActorName = Actor->GetName();
		IndexedActor.ActorClass = Actor->GetClass()->GetName();
		IndexedActor.ActorLabel = Actor->GetActorLabel();
		IndexedActor.Transform = SerializeTransform(Actor->GetActorTransform());

		// Canary. Actors legitimately sit at the origin, but a whole project's worth cannot -
		// a high ratio means transform resolution regressed again and the placement data is junk.
		if (Actor->GetActorLocation().IsNearlyZero())
		{
			++OriginTransformCount;
		}
		++TransformSampleCount;
		IndexedActor.Components = SerializeComponents(Actor);

		DB.InsertActor(IndexedActor);
		Count++;
	}
	return Count;
}

FString FLevelIndexer::SerializeTransform(const FTransform& Transform)
{
	auto Obj = MakeShared<FJsonObject>();

	const FVector& Loc = Transform.GetLocation();
	auto LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Loc.X);
	LocObj->SetNumberField(TEXT("y"), Loc.Y);
	LocObj->SetNumberField(TEXT("z"), Loc.Z);
	Obj->SetObjectField(TEXT("location"), LocObj);

	const FRotator Rot = Transform.GetRotation().Rotator();
	auto RotObj = MakeShared<FJsonObject>();
	RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
	RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
	Obj->SetObjectField(TEXT("rotation"), RotObj);

	const FVector& Scale = Transform.GetScale3D();
	auto ScaleObj = MakeShared<FJsonObject>();
	ScaleObj->SetNumberField(TEXT("x"), Scale.X);
	ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
	ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
	Obj->SetObjectField(TEXT("scale"), ScaleObj);

	FString Result;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result);
	FJsonSerializer::Serialize(Obj, *Writer, true);
	return Result;
}

FString FLevelIndexer::SerializeComponents(const AActor* Actor)
{
	TArray<TSharedPtr<FJsonValue>> CompArray;

	TInlineComponentArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	for (const UActorComponent* Comp : Components)
	{
		if (!Comp) continue;

		auto CompObj = MakeShared<FJsonObject>();
		CompObj->SetStringField(TEXT("name"), Comp->GetName());
		CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());

		CompArray.Add(MakeShared<FJsonValueObject>(CompObj));
	}

	FString Result;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result);
	FJsonSerializer::Serialize(CompArray, *Writer);
	return Result;
}
