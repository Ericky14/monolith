#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "MonolithSourceDatabase.h"
#include "MonolithSourceSubsystem.generated.h"

class FMonolithSourceIndexer;

/**
 * Editor subsystem that owns the engine source DB and triggers C++ source indexing.
 */
UCLASS()
class MONOLITHSOURCE_API UMonolithSourceSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual ~UMonolithSourceSubsystem();
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Get the source database (read-only). May be null if DB doesn't exist. */
	FMonolithSourceDatabase* GetDatabase() { return Database.IsValid() ? Database.Get() : nullptr; }

	/** Full reindex: engine + shaders + project source (clean build). */
	void TriggerReindex();

	/** Incremental project-only reindex: loads existing engine symbols, indexes only project C++ source. */
	void TriggerProjectReindex();

	/** Is indexing currently running? */
	bool IsIndexing() const { return bIsIndexing; }

private:
	FString GetDatabasePath() const;
	FString GetEngineSourcePath() const;
	FString GetEngineShaderPath() const;
	FString GetProjectPath() const;
	void ReopenDatabase(const FString& DbPath);

	TUniquePtr<FMonolithSourceDatabase> Database;
	FMonolithSourceIndexer* Indexer = nullptr;
	TAtomic<bool> bIsIndexing{false};
};
