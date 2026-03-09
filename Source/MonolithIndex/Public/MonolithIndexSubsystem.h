#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Misc/AsyncTaskNotification.h"
#include "MonolithIndexDatabase.h"
#include "MonolithIndexer.h"
#include "MonolithIndexSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnIndexingProgress, int32 /*Current*/, int32 /*Total*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnIndexingComplete, bool /*bSuccess*/);

/**
 * Editor subsystem that orchestrates the Monolith project index.
 * Owns the SQLite database, manages indexers, runs background indexing.
 */
UCLASS()
class MONOLITHINDEX_API UMonolithIndexSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// --- UEditorSubsystem interface ---
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Trigger a full re-index (wipes DB, re-scans everything) */
	UFUNCTION()
	void StartFullIndex();

	/** Is indexing currently in progress? */
	bool IsIndexing() const { return bIsIndexing; }

	/** Get indexing progress (0.0 - 1.0) */
	float GetProgress() const;

	/** Get current indexing status message */
	FString GetStatusMessage() const { return IndexingStatusMessage; }

	/** Get the database (for queries). May be null if not initialized. */
	FMonolithIndexDatabase* GetDatabase() { return Database.Get(); }

	// --- Query API (called by MCP actions) ---
	TArray<FSearchResult> Search(const FString& Query, int32 Limit = 50);
	TSharedPtr<FJsonObject> FindReferences(const FString& PackagePath);
	TArray<FIndexedAsset> FindByType(const FString& AssetClass, int32 Limit = 100, int32 Offset = 0);
	TSharedPtr<FJsonObject> GetStats();
	TSharedPtr<FJsonObject> GetAssetDetails(const FString& PackagePath);

	/** Register an indexer. Takes ownership. */
	void RegisterIndexer(TSharedPtr<IMonolithIndexer> Indexer);

	// --- Delegates ---
	FOnIndexingProgress OnProgress;
	FOnIndexingComplete OnComplete;

private:
	/** Background indexing task */
	class FIndexingTask : public FRunnable
	{
	public:
		FIndexingTask(UMonolithIndexSubsystem* InOwner);

		virtual bool Init() override { return true; }
		virtual uint32 Run() override;
		virtual void Stop() override { bShouldStop = true; }

		TAtomic<bool> bShouldStop{false};
		TAtomic<int32> CurrentIndex{0};
		TAtomic<int32> TotalAssets{0};

	private:
		UMonolithIndexSubsystem* Owner;
	};

	void OnIndexingFinished(bool bSuccess);
	void OnAssetRegistryFilesLoaded();
	void RegisterDefaultIndexers();
	FString GetDatabasePath() const;
	bool ShouldAutoIndex() const;

	TUniquePtr<FMonolithIndexDatabase> Database;
	TArray<TSharedPtr<IMonolithIndexer>> Indexers;
	TMap<FString, TSharedPtr<IMonolithIndexer>> ClassToIndexer;

	TUniquePtr<FRunnableThread> IndexingThread;
	TUniquePtr<FIndexingTask> IndexingTaskPtr;
	TAtomic<bool> bIsIndexing{false};

	FString IndexingStatusMessage;
	TUniquePtr<FAsyncTaskNotification> TaskNotification;
};
