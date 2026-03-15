#pragma once

#include "Commandlets/Commandlet.h"
#include "MonolithQueryCommandlet.generated.h"

/**
 * Offline query commandlet for Monolith databases.
 * Mirrors monolith_offline.py — queries EngineSource.db and ProjectIndex.db
 * without requiring the MCP server or full editor.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe ProjectName -run=MonolithQuery <namespace> <action> [args] [--options]
 *
 * Namespaces:
 *   source  — Engine source queries (search_source, read_source, find_callers, etc.)
 *   project — Project asset queries (search, find_by_type, get_stats, etc.)
 */
UCLASS()
class UMonolithQueryCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMonolithQueryCommandlet();
	virtual int32 Main(const FString& Params) override;

private:
	int32 HandleSource(const TArray<FString>& Args, const TMap<FString, FString>& Options);
	int32 HandleProject(const TArray<FString>& Args, const TMap<FString, FString>& Options);

	void PrintUsage();
	FString GetSourceDbPath() const;
	FString GetProjectDbPath() const;

	/** Parse --key=value options from raw args. Returns positional args only. */
	TArray<FString> ParseOptions(const TArray<FString>& RawArgs, TMap<FString, FString>& OutOptions);
};
