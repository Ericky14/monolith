#pragma once

#include "MonolithIndexer.h"
#include "Containers/ArrayView.h"

/**
 * Indexes level actors from World/Map assets.
 * Runs after all other indexers (needs all assets in DB).
 * Uses special class name "__Levels__" for post-indexing dispatch.
 * Loads each level's persistent level to extract actor metadata.
 *
 * DRIVEN ONE BATCH PER GAME-THREAD CALL - see IndexAsset() for why this is not optional.
 * The subsystem calls GatherWorldAssets() once, then IndexWorldBatch() per batch (each on its
 * own game-thread dispatch so the editor renders a frame in between), then FinishPass().
 */
class FLevelIndexer : public IMonolithIndexer
{
public:
	virtual TArray<FString> GetSupportedClasses() const override
	{
		return { TEXT("__Levels__") };
	}

	/**
	 * NOT the entry point for this indexer - always returns false.
	 *
	 * Indexing every level inside one IndexAsset() call is what removed the GPU device: the call
	 * blocks the game thread for ~80s, so the editor renders ZERO frames while 161 worlds' worth
	 * of Nanite resources queue up, and the first frame afterwards processes the whole backlog at
	 * once - 258 GB of reserved VA against a 256 GB budget -> DXGI_ERROR_DEVICE_REMOVED. Measured:
	 * VA sat at a healthy 58.5 GB for the entire pass and exploded on the single frame after it.
	 *
	 * The batched API below is the fix, so this override is deliberately inert rather than a
	 * working-but-fatal fallback that a future caller could reach by accident.
	 */
	virtual bool IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId) override;

	virtual FString GetName() const override { return TEXT("LevelIndexer"); }
	virtual bool IsSentinel() const override { return true; }

	/** Set of valid path prefixes for indexing */
	TArray<FName> IndexedPaths;

	/** Set indexed paths. Called before the pass starts. */
	void SetIndexedPaths(const TArray<FName>& InPaths) { IndexedPaths = InPaths; }

	/**
	 * Every World asset under IndexedPaths. No package loading, but MUST run on the game thread:
	 * IAssetRegistry::GetAssets enumerates in-memory assets and asserts IsInGameThread()
	 * (AssetRegistry.cpp: "Enumerating in-memory assets can only be done on the game thread").
	 */
	TArray<FAssetData> GatherWorldAssets() const;

	/** Clears per-pass counters and the peak-VA gauge. Call once before the first batch. */
	void BeginPass();

	/** Loads + indexes one batch of worlds. MUST run on the game thread. */
	void IndexWorldBatch(TArrayView<const FAssetData> Batch, FMonolithIndexDatabase& DB);

	/** Final GC and the peak reserved-VA report. MUST run on the game thread. */
	void FinishPass();

	int32 GetLevelsProcessed() const { return LevelsProcessed; }
	int32 GetActorsInserted() const { return ActorsInserted; }

private:
	int32 IndexActorsInLevel(class ULevel* Level, FMonolithIndexDatabase& DB, int64 AssetId);
	FString SerializeTransform(const FTransform& Transform);
	FString SerializeComponents(const class AActor* Actor);

	int32 ActorsInserted = 0;
	int32 LevelsProcessed = 0;

	/** Makes each instanced load package name unique so loads never collide. */
	int32 InstanceCounter = 0;

	/**
	 * Counts worlds that came back INITIALIZED despite the instanced load. Must stay 0: a non-zero
	 * value means UWorld::IsInstanced() did not hold, the editor built an FScene anyway, and the
	 * reserved-VA blow-up is back. Reported by FinishPass so the failure is a log line, not a crash.
	 */
	int32 InitializedWorldCount = 0;
};
