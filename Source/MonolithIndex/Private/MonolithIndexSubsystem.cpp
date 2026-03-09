#include "MonolithIndexSubsystem.h"
#include "MonolithIndexDatabase.h"
#include "MonolithSettings.h"
#include "Misc/AsyncTaskNotification.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/Paths.h"
#include "HAL/RunnableThread.h"
#include "Async/Async.h"

// Indexers
#include "Indexers/BlueprintIndexer.h"
#include "Indexers/MaterialIndexer.h"
#include "Indexers/GenericAssetIndexer.h"
#include "Indexers/DependencyIndexer.h"
#include "Indexers/LevelIndexer.h"
#include "Indexers/ConfigIndexer.h"
#include "Indexers/DataTableIndexer.h"
#include "Indexers/GameplayTagIndexer.h"
#include "Indexers/CppIndexer.h"
#include "Indexers/AnimationIndexer.h"
#include "Indexers/NiagaraIndexer.h"
#include "Indexers/UserDefinedEnumIndexer.h"
#include "Indexers/UserDefinedStructIndexer.h"
#include "Indexers/InputActionIndexer.h"

void UMonolithIndexSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Database = MakeUnique<FMonolithIndexDatabase>();
	FString DbPath = GetDatabasePath();

	if (!Database->Open(DbPath))
	{
		UE_LOG(LogMonolithIndex, Error, TEXT("Failed to open index database at %s"), *DbPath);
		return;
	}

	RegisterDefaultIndexers();

	if (ShouldAutoIndex())
	{
		UE_LOG(LogMonolithIndex, Log, TEXT("First launch detected -- deferring full index until Asset Registry is ready"));
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.OnFilesLoaded().AddUObject(this, &UMonolithIndexSubsystem::OnAssetRegistryFilesLoaded);
		}
		else
		{
			// Asset Registry already done (unlikely at Initialize time, but handle it)
			UE_LOG(LogMonolithIndex, Log, TEXT("Asset Registry already loaded -- starting full project index"));
			StartFullIndex();
		}
	}
}

void UMonolithIndexSubsystem::OnAssetRegistryFilesLoaded()
{
	// Unbind ourselves — this is a one-shot callback
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetRegistry.OnFilesLoaded().RemoveAll(this);

	if (ShouldAutoIndex())
	{
		UE_LOG(LogMonolithIndex, Log, TEXT("Asset Registry fully loaded -- starting full project index"));
		StartFullIndex();
	}
	else
	{
		UE_LOG(LogMonolithIndex, Log, TEXT("Asset Registry loaded but auto-index no longer needed (already indexed)"));
	}
}

void UMonolithIndexSubsystem::Deinitialize()
{
	// Unbind from Asset Registry delegate if still bound
	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		AssetRegistry.OnFilesLoaded().RemoveAll(this);
	}

	// Stop any running indexing
	if (IndexingTaskPtr.IsValid())
	{
		if (bIsIndexing)
		{
			UE_LOG(LogMonolithIndex, Warning, TEXT("Indexing was still in progress during shutdown — force-stopped"));
		}
		IndexingTaskPtr->Stop();
		if (IndexingThread)
		{
			IndexingThread->WaitForCompletion();
			IndexingThread.Reset();
		}
		IndexingTaskPtr.Reset();
	}

	bIsIndexing = false;

	TaskNotification.Reset();

	if (Database.IsValid())
	{
		Database->Close();
	}

	Super::Deinitialize();
}

void UMonolithIndexSubsystem::RegisterIndexer(TSharedPtr<IMonolithIndexer> Indexer)
{
	if (!Indexer.IsValid()) return;

	Indexers.Add(Indexer);
	for (const FString& ClassName : Indexer->GetSupportedClasses())
	{
		ClassToIndexer.Add(ClassName, Indexer);
	}

	UE_LOG(LogMonolithIndex, Verbose, TEXT("Registered indexer: %s (%d classes)"),
		*Indexer->GetName(), Indexer->GetSupportedClasses().Num());
}

void UMonolithIndexSubsystem::RegisterDefaultIndexers()
{
	const UMonolithSettings* Settings = GetDefault<UMonolithSettings>();

	if (Settings->bIndexBlueprints)
		RegisterIndexer(MakeShared<FBlueprintIndexer>());
	if (Settings->bIndexMaterials)
		RegisterIndexer(MakeShared<FMaterialIndexer>());
	if (Settings->bIndexGenericAssets)
		RegisterIndexer(MakeShared<FGenericAssetIndexer>());
	if (Settings->bIndexDependencies)
		RegisterIndexer(MakeShared<FDependencyIndexer>());
	if (Settings->bIndexLevels)
		RegisterIndexer(MakeShared<FLevelIndexer>());
	if (Settings->bIndexDataTables)
		RegisterIndexer(MakeShared<FDataTableIndexer>());
	if (Settings->bIndexGameplayTags)
		RegisterIndexer(MakeShared<FGameplayTagIndexer>());
	if (Settings->bIndexConfigs)
		RegisterIndexer(MakeShared<FConfigIndexer>());
	if (Settings->bIndexCppSymbols)
		RegisterIndexer(MakeShared<FCppIndexer>());
	if (Settings->bIndexAnimations)
		RegisterIndexer(MakeShared<FAnimationIndexer>());
	if (Settings->bIndexNiagara)
		RegisterIndexer(MakeShared<FNiagaraIndexer>());
	if (Settings->bIndexUserDefinedEnums)
		RegisterIndexer(MakeShared<FUserDefinedEnumIndexer>());
	if (Settings->bIndexUserDefinedStructs)
		RegisterIndexer(MakeShared<FUserDefinedStructIndexer>());
	if (Settings->bIndexInputActions)
		RegisterIndexer(MakeShared<FInputActionIndexer>());

	UE_LOG(LogMonolithIndex, Log, TEXT("Registered %d indexers"), Indexers.Num());
}

void UMonolithIndexSubsystem::StartFullIndex()
{
	if (bIsIndexing)
	{
		UE_LOG(LogMonolithIndex, Warning, TEXT("Indexing already in progress"));
		return;
	}

	bIsIndexing = true;

	// Reset the database for a full re-index
	Database->ResetDatabase();

	// Show notification
	FAsyncTaskNotificationConfig NotifConfig;
	NotifConfig.TitleText = FText::FromString(TEXT("Monolith"));
	NotifConfig.ProgressText = FText::FromString(TEXT("Indexing project..."));
	NotifConfig.bCanCancel = true;
	NotifConfig.LogCategory = &LogMonolithIndex;
	TaskNotification = MakeUnique<FAsyncTaskNotification>(NotifConfig);

	// Launch background thread
	IndexingTaskPtr = MakeUnique<FIndexingTask>(this);
	IndexingThread.Reset(FRunnableThread::Create(
		IndexingTaskPtr.Get(),
		TEXT("MonolithIndexing"),
		0,
		TPri_BelowNormal
	));

	UE_LOG(LogMonolithIndex, Log, TEXT("Background indexing started"));
}

float UMonolithIndexSubsystem::GetProgress() const
{
	if (!IndexingTaskPtr.IsValid() || IndexingTaskPtr->TotalAssets == 0) return 0.0f;
	return static_cast<float>(IndexingTaskPtr->CurrentIndex) / static_cast<float>(IndexingTaskPtr->TotalAssets);
}

// ============================================================
// Query API wrappers
// ============================================================

TArray<FSearchResult> UMonolithIndexSubsystem::Search(const FString& Query, int32 Limit)
{
	if (!Database.IsValid() || !Database->IsOpen()) return {};
	return Database->FullTextSearch(Query, Limit);
}

TSharedPtr<FJsonObject> UMonolithIndexSubsystem::FindReferences(const FString& PackagePath)
{
	if (!Database.IsValid() || !Database->IsOpen()) return nullptr;
	return Database->FindReferences(PackagePath);
}

TArray<FIndexedAsset> UMonolithIndexSubsystem::FindByType(const FString& AssetClass, int32 Limit, int32 Offset)
{
	if (!Database.IsValid() || !Database->IsOpen()) return {};
	return Database->FindByType(AssetClass, Limit, Offset);
}

TSharedPtr<FJsonObject> UMonolithIndexSubsystem::GetStats()
{
	if (!Database.IsValid() || !Database->IsOpen()) return nullptr;
	return Database->GetStats();
}

TSharedPtr<FJsonObject> UMonolithIndexSubsystem::GetAssetDetails(const FString& PackagePath)
{
	if (!Database.IsValid() || !Database->IsOpen()) return nullptr;
	return Database->GetAssetDetails(PackagePath);
}

// ============================================================
// Background indexing task
// ============================================================

UMonolithIndexSubsystem::FIndexingTask::FIndexingTask(UMonolithIndexSubsystem* InOwner)
	: Owner(InOwner)
{
}

uint32 UMonolithIndexSubsystem::FIndexingTask::Run()
{
	// Asset Registry enumeration MUST happen on the game thread
	TArray<FAssetData> AllAssets;
	FEvent* RegistryEvent = FPlatformProcess::GetSynchEventFromPool(true);
	AsyncTask(ENamedThreads::GameThread, [&AllAssets, RegistryEvent]()
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

		if (!AssetRegistry.IsSearchAllAssets())
		{
			AssetRegistry.SearchAllAssets(true);
		}
		AssetRegistry.WaitForCompletion();

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
		Filter.bRecursivePaths = true;
		AssetRegistry.GetAssets(Filter, AllAssets);

		RegistryEvent->Trigger();
	});
	RegistryEvent->Wait();
	FPlatformProcess::ReturnSynchEventToPool(RegistryEvent);

	TotalAssets = AllAssets.Num();
	Owner->IndexingStatusMessage = FString::Printf(TEXT("Scanning %d assets..."), TotalAssets.Load());
	UE_LOG(LogMonolithIndex, Log, TEXT("Indexing %d assets..."), TotalAssets.Load());

	FMonolithIndexDatabase* DB = Owner->Database.Get();
	if (!DB || !DB->IsOpen())
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			Owner->OnIndexingFinished(false);
		});
		return 1;
	}

	DB->BeginTransaction();

	int32 BatchSize = 100;
	int32 Indexed = 0;
	int32 Errors = 0;

	// Collect assets that have deep indexers for a second pass
	struct FDeepIndexEntry
	{
		FAssetData AssetData;
		int64 AssetId;
		TSharedPtr<IMonolithIndexer> Indexer;
	};
	TArray<FDeepIndexEntry> DeepIndexQueue;

	TMap<FString, int32> ClassDistribution;
	TMap<FString, int32> QueuedClassDistribution;

	for (int32 i = 0; i < AllAssets.Num(); ++i)
	{
		if (bShouldStop) break;

		if (Owner->TaskNotification && Owner->TaskNotification->GetPromptAction() == EAsyncTaskNotificationPromptAction::Cancel)
		{
			bShouldStop = true;
			break;
		}

		const FAssetData& AssetData = AllAssets[i];
		CurrentIndex = i + 1;

		// Insert the base asset record
		FIndexedAsset IndexedAsset;
		IndexedAsset.PackagePath = AssetData.PackageName.ToString();
		IndexedAsset.AssetName = AssetData.AssetName.ToString();
		IndexedAsset.AssetClass = AssetData.AssetClassPath.GetAssetName().ToString();
		ClassDistribution.FindOrAdd(IndexedAsset.AssetClass)++;

		int64 AssetId = DB->InsertAsset(IndexedAsset);
		if (AssetId < 0)
		{
			Errors++;
			continue;
		}

		// Queue assets that have deep indexers (Blueprint, Material, etc.)
		TSharedPtr<IMonolithIndexer>* FoundIndexer = Owner->ClassToIndexer.Find(IndexedAsset.AssetClass);
		if (FoundIndexer && FoundIndexer->IsValid())
		{
			DeepIndexQueue.Add({ AssetData, AssetId, *FoundIndexer });
			QueuedClassDistribution.FindOrAdd(IndexedAsset.AssetClass)++;
		}

		Indexed++;

		// Commit in batches
		if (Indexed % BatchSize == 0)
		{
			DB->CommitTransaction();
			DB->BeginTransaction();

			UE_LOG(LogMonolithIndex, Log, TEXT("Indexed %d / %d assets (%d errors)"),
				Indexed, TotalAssets.Load(), Errors);

			if (Owner->TaskNotification)
			{
				Owner->TaskNotification->SetProgressText(FText::FromString(
					FString::Printf(TEXT("Indexing %d / %d assets..."), CurrentIndex.Load(), TotalAssets.Load())));
			}

			AsyncTask(ENamedThreads::GameThread, [this]()
			{
				Owner->OnProgress.Broadcast(CurrentIndex.Load(), TotalAssets.Load());
			});
		}
	}

	// Log class distribution summary
	UE_LOG(LogMonolithIndex, Log, TEXT("Asset class distribution (top 20):"));
	ClassDistribution.ValueSort([](int32 A, int32 B) { return A > B; });
	int32 Shown = 0;
	for (const auto& Pair : ClassDistribution)
	{
		if (Shown++ >= 20) break;
		UE_LOG(LogMonolithIndex, Log, TEXT("  %s: %d"), *Pair.Key, Pair.Value);
	}

	UE_LOG(LogMonolithIndex, Log, TEXT("Deep index queue: %d assets across %d classes"),
		DeepIndexQueue.Num(), QueuedClassDistribution.Num());
	for (const auto& Pair : QueuedClassDistribution)
	{
		UE_LOG(LogMonolithIndex, Log, TEXT("  Queued %s: %d"), *Pair.Key, Pair.Value);
	}

	DB->CommitTransaction();

	UE_LOG(LogMonolithIndex, Log, TEXT("Metadata pass complete: %d assets indexed, %d errors"), Indexed, Errors);

	// ============================================================
	// Deep indexing pass — load assets on game thread in time-budgeted batches
	// Assets must be loaded on the game thread to avoid texture compiler crashes.
	// We process in small batches, yielding when the frame budget is exceeded.
	// ============================================================
	Owner->IndexingStatusMessage = FString::Printf(TEXT("Deep indexing %d assets..."), DeepIndexQueue.Num());

	if (!bShouldStop && DeepIndexQueue.Num() > 0)
	{
		UE_LOG(LogMonolithIndex, Log, TEXT("Starting deep indexing pass for %d assets..."), DeepIndexQueue.Num());

		constexpr int32 DeepBatchSize = 16;
		constexpr double FrameBudgetSeconds = 0.016; // ~16ms per batch to stay interactive
		TAtomic<int32> DeepIndexed{0};
		TAtomic<int32> DeepErrors{0};
		int32 TotalDeep = DeepIndexQueue.Num();

		for (int32 BatchStart = 0; BatchStart < TotalDeep && !bShouldStop; BatchStart += DeepBatchSize)
		{
			int32 BatchEnd = FMath::Min(BatchStart + DeepBatchSize, TotalDeep);

			// Capture the slice for this batch
			TArray<FDeepIndexEntry> BatchSlice;
			BatchSlice.Reserve(BatchEnd - BatchStart);
			for (int32 j = BatchStart; j < BatchEnd; ++j)
			{
				BatchSlice.Add(DeepIndexQueue[j]);
			}

			FEvent* BatchEvent = FPlatformProcess::GetSynchEventFromPool(true);

			AsyncTask(ENamedThreads::GameThread, [DB, BatchSlice = MoveTemp(BatchSlice), &DeepIndexed, &DeepErrors, FrameBudgetSeconds, BatchEvent]()
			{
				DB->BeginTransaction();
				double BatchStartTime = FPlatformTime::Seconds();

				for (const FDeepIndexEntry& Entry : BatchSlice)
				{
					// Load asset on game thread (safe for texture compiler)
					UObject* LoadedAsset = Entry.AssetData.GetAsset();
					if (LoadedAsset)
					{
						if (Entry.Indexer->IndexAsset(Entry.AssetData, LoadedAsset, *DB, Entry.AssetId))
						{
							DeepIndexed++;
						}
						else
						{
							DeepErrors++;
							UE_LOG(LogMonolithIndex, Warning, TEXT("Deep indexer '%s' failed for: %s"),
								*Entry.Indexer->GetName(),
								*Entry.AssetData.PackageName.ToString());
						}
					}
					else
					{
						DeepErrors++;
						UE_LOG(LogMonolithIndex, Warning, TEXT("Failed to load asset for deep indexing: %s (class: %s)"),
							*Entry.AssetData.PackageName.ToString(),
							*Entry.AssetData.AssetClassPath.GetAssetName().ToString());
					}

					// If we've exceeded our frame budget, commit what we have and yield
					double Elapsed = FPlatformTime::Seconds() - BatchStartTime;
					if (Elapsed > FrameBudgetSeconds)
					{
						DB->CommitTransaction();
						DB->BeginTransaction();
						BatchStartTime = FPlatformTime::Seconds();
					}
				}

				DB->CommitTransaction();
				BatchEvent->Trigger();
			});

			BatchEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(BatchEvent);

			// Update progress — report deep pass as second half of overall progress
			CurrentIndex = Indexed + BatchEnd;
			TotalAssets = Indexed + TotalDeep;

			if (Owner->TaskNotification)
			{
				Owner->TaskNotification->SetProgressText(FText::FromString(
					FString::Printf(TEXT("Deep indexing %d / %d assets..."), BatchEnd, TotalDeep)));
			}

			AsyncTask(ENamedThreads::GameThread, [this]()
			{
				Owner->OnProgress.Broadcast(CurrentIndex.Load(), TotalAssets.Load());
			});

			UE_LOG(LogMonolithIndex, Log, TEXT("Deep indexed %d / %d assets (%d ok, %d errors)"),
				BatchEnd, TotalDeep, DeepIndexed.Load(), DeepErrors.Load());
		}

		UE_LOG(LogMonolithIndex, Log, TEXT("Deep indexing complete: %d indexed, %d errors"),
			DeepIndexed.Load(), DeepErrors.Load());
	}

	// Run dependency indexer on game thread (Asset Registry requires it)
	Owner->IndexingStatusMessage = TEXT("Analyzing dependencies...");
	TSharedPtr<IMonolithIndexer>* DepIndexer = Owner->ClassToIndexer.Find(TEXT("__Dependencies__"));
	if (DepIndexer && DepIndexer->IsValid())
	{
		double SentinelStart = FPlatformTime::Seconds();
		UE_LOG(LogMonolithIndex, Log, TEXT("Running dependency indexer..."));
		TSharedPtr<IMonolithIndexer> DepIndexerCopy = *DepIndexer;
		FEvent* DepEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [DB, DepIndexerCopy, DepEvent]()
		{
			DB->BeginTransaction();
			FAssetData DummyData;
			DepIndexerCopy->IndexAsset(DummyData, nullptr, *DB, 0);
			DB->CommitTransaction();
			DepEvent->Trigger();
		});
		DepEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(DepEvent);
		UE_LOG(LogMonolithIndex, Log, TEXT("Dependency indexer completed in %.2fs"), FPlatformTime::Seconds() - SentinelStart);
	}

	// Run level indexer on game thread (asset loading requires it)
	Owner->IndexingStatusMessage = TEXT("Indexing level actors...");
	TSharedPtr<IMonolithIndexer>* LevelIndexer = Owner->ClassToIndexer.Find(TEXT("__Levels__"));
	if (LevelIndexer && LevelIndexer->IsValid())
	{
		double SentinelStart = FPlatformTime::Seconds();
		UE_LOG(LogMonolithIndex, Log, TEXT("Running level indexer..."));
		TSharedPtr<IMonolithIndexer> LevelIndexerCopy = *LevelIndexer;
		FEvent* LevelEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [DB, LevelIndexerCopy, LevelEvent]()
		{
			DB->BeginTransaction();
			FAssetData DummyData;
			LevelIndexerCopy->IndexAsset(DummyData, nullptr, *DB, 0);
			DB->CommitTransaction();
			LevelEvent->Trigger();
		});
		LevelEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(LevelEvent);
		UE_LOG(LogMonolithIndex, Log, TEXT("Level indexer completed in %.2fs"), FPlatformTime::Seconds() - SentinelStart);
	}

	// Run DataTable indexer on game thread (requires asset loading)
	Owner->IndexingStatusMessage = TEXT("Indexing DataTable rows...");
	TSharedPtr<IMonolithIndexer>* DTIndexer = Owner->ClassToIndexer.Find(TEXT("__DataTables__"));
	if (DTIndexer && DTIndexer->IsValid())
	{
		double SentinelStart = FPlatformTime::Seconds();
		UE_LOG(LogMonolithIndex, Log, TEXT("Running DataTable indexer..."));
		TSharedPtr<IMonolithIndexer> DTIndexerCopy = *DTIndexer;
		FEvent* DTEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [DB, DTIndexerCopy, DTEvent]()
		{
			DB->BeginTransaction();
			FAssetData DummyData;
			DTIndexerCopy->IndexAsset(DummyData, nullptr, *DB, 0);
			DB->CommitTransaction();
			DTEvent->Trigger();
		});
		DTEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(DTEvent);
		UE_LOG(LogMonolithIndex, Log, TEXT("DataTable indexer completed in %.2fs"), FPlatformTime::Seconds() - SentinelStart);
	}

	// Run config indexer (file I/O only, no game thread needed)
	Owner->IndexingStatusMessage = TEXT("Indexing config files...");
	TSharedPtr<IMonolithIndexer>* CfgIndexer = Owner->ClassToIndexer.Find(TEXT("__Configs__"));
	if (CfgIndexer && CfgIndexer->IsValid())
	{
		double SentinelStart = FPlatformTime::Seconds();
		UE_LOG(LogMonolithIndex, Log, TEXT("Running config indexer..."));
		DB->BeginTransaction();
		FAssetData DummyCfgData;
		(*CfgIndexer)->IndexAsset(DummyCfgData, nullptr, *DB, 0);
		DB->CommitTransaction();
		UE_LOG(LogMonolithIndex, Log, TEXT("Config indexer completed in %.2fs"), FPlatformTime::Seconds() - SentinelStart);
	}

	// Run C++ symbol indexer (file I/O only, no game thread needed)
	Owner->IndexingStatusMessage = TEXT("Indexing C++ symbols...");
	TSharedPtr<IMonolithIndexer>* CppIndexer = Owner->ClassToIndexer.Find(TEXT("__CppSymbols__"));
	if (CppIndexer && CppIndexer->IsValid())
	{
		double SentinelStart = FPlatformTime::Seconds();
		UE_LOG(LogMonolithIndex, Log, TEXT("Running C++ symbol indexer..."));
		DB->BeginTransaction();
		FAssetData DummyCppData;
		(*CppIndexer)->IndexAsset(DummyCppData, nullptr, *DB, 0);
		DB->CommitTransaction();
		UE_LOG(LogMonolithIndex, Log, TEXT("C++ symbol indexer completed in %.2fs"), FPlatformTime::Seconds() - SentinelStart);
	}

	// Run animation indexer on game thread (asset loading requires it)
	Owner->IndexingStatusMessage = TEXT("Indexing animations...");
	TSharedPtr<IMonolithIndexer>* AnimIndexer = Owner->ClassToIndexer.Find(TEXT("__Animations__"));
	if (AnimIndexer && AnimIndexer->IsValid())
	{
		double SentinelStart = FPlatformTime::Seconds();
		UE_LOG(LogMonolithIndex, Log, TEXT("Running animation indexer..."));
		TSharedPtr<IMonolithIndexer> AnimIndexerCopy = *AnimIndexer;
		FEvent* AnimEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [DB, AnimIndexerCopy, AnimEvent]()
		{
			DB->BeginTransaction();
			FAssetData DummyData;
			AnimIndexerCopy->IndexAsset(DummyData, nullptr, *DB, 0);
			DB->CommitTransaction();
			AnimEvent->Trigger();
		});
		AnimEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(AnimEvent);
		UE_LOG(LogMonolithIndex, Log, TEXT("Animation indexer completed in %.2fs"), FPlatformTime::Seconds() - SentinelStart);
	}

	// Run gameplay tag indexer on game thread (GameplayTagsManager requires it)
	Owner->IndexingStatusMessage = TEXT("Indexing gameplay tags...");
	TSharedPtr<IMonolithIndexer>* TagIndexer = Owner->ClassToIndexer.Find(TEXT("__GameplayTags__"));
	if (TagIndexer && TagIndexer->IsValid())
	{
		double SentinelStart = FPlatformTime::Seconds();
		UE_LOG(LogMonolithIndex, Log, TEXT("Running gameplay tag indexer..."));
		TSharedPtr<IMonolithIndexer> TagIndexerCopy = *TagIndexer;
		FEvent* TagEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [DB, TagIndexerCopy, TagEvent]()
		{
			DB->BeginTransaction();
			FAssetData DummyData;
			TagIndexerCopy->IndexAsset(DummyData, nullptr, *DB, 0);
			DB->CommitTransaction();
			TagEvent->Trigger();
		});
		TagEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(TagEvent);
		UE_LOG(LogMonolithIndex, Log, TEXT("Gameplay tag indexer completed in %.2fs"), FPlatformTime::Seconds() - SentinelStart);
	}

	// Run Niagara indexer on game thread (requires asset loading)
	Owner->IndexingStatusMessage = TEXT("Indexing Niagara systems...");
	TSharedPtr<IMonolithIndexer>* NiagaraIndexerPtr = Owner->ClassToIndexer.Find(TEXT("__Niagara__"));
	if (NiagaraIndexerPtr && NiagaraIndexerPtr->IsValid())
	{
		double SentinelStart = FPlatformTime::Seconds();
		UE_LOG(LogMonolithIndex, Log, TEXT("Running Niagara indexer..."));
		TSharedPtr<IMonolithIndexer> NiagaraIndexerCopy = *NiagaraIndexerPtr;
		FEvent* NiagaraEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [DB, NiagaraIndexerCopy, NiagaraEvent]()
		{
			DB->BeginTransaction();
			FAssetData DummyData;
			NiagaraIndexerCopy->IndexAsset(DummyData, nullptr, *DB, 0);
			DB->CommitTransaction();
			NiagaraEvent->Trigger();
		});
		NiagaraEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(NiagaraEvent);
		UE_LOG(LogMonolithIndex, Log, TEXT("Niagara indexer completed in %.2fs"), FPlatformTime::Seconds() - SentinelStart);
	}

	// Write index timestamp to meta (only if not cancelled and asset count looks valid)
	if (!bShouldStop)
	{
		constexpr int32 MinAssetCountThreshold = 500;
		if (Indexed < MinAssetCountThreshold)
		{
			UE_LOG(LogMonolithIndex, Warning, TEXT("Index only found %d assets — Asset Registry may not have been fully loaded. Skipping last_full_index write so next launch will re-index."), Indexed);
		}
		else
		{
			DB->WriteMeta(TEXT("last_full_index"), FDateTime::UtcNow().ToString());
			UE_LOG(LogMonolithIndex, Log, TEXT("Wrote last_full_index timestamp (%d assets indexed)"), Indexed);
		}
	}

	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		Owner->OnIndexingFinished(!bShouldStop);
	});

	return 0;
}

void UMonolithIndexSubsystem::OnIndexingFinished(bool bSuccess)
{
	bIsIndexing = false;
	IndexingStatusMessage.Empty();

	if (IndexingThread)
	{
		IndexingThread->WaitForCompletion();
		IndexingThread.Reset();
	}

	IndexingTaskPtr.Reset();

	if (TaskNotification)
	{
		TaskNotification->SetComplete(
			FText::FromString(TEXT("Monolith")),
			FText::FromString(bSuccess ? TEXT("Project indexing complete") : TEXT("Project indexing failed")),
			bSuccess);
		TaskNotification.Reset();
	}

	OnComplete.Broadcast(bSuccess);
	OnProgress.Clear();

	UE_LOG(LogMonolithIndex, Log, TEXT("Indexing %s"),
		bSuccess ? TEXT("completed successfully") : TEXT("failed or was cancelled"));
}

FString UMonolithIndexSubsystem::GetDatabasePath() const
{
	FString PluginDir = FPaths::ProjectPluginsDir() / TEXT("Monolith") / TEXT("Saved");
	return PluginDir / TEXT("ProjectIndex.db");
}

bool UMonolithIndexSubsystem::ShouldAutoIndex() const
{
	if (!Database.IsValid() || !Database->IsOpen()) return false;

	FSQLiteDatabase* RawDB = Database->GetRawDatabase();
	if (!RawDB) return false;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*RawDB, TEXT("SELECT value FROM meta WHERE key = 'last_full_index';"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		return false; // Already indexed before
	}
	return true;
}
