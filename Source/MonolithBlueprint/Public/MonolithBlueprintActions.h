#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

class FMonolithBlueprintActions
{
public:
	static void RegisterActions();

	static FMonolithActionResult HandleListGraphs(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetGraphData(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetGraphSummary(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetVariables(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetExecutionFlow(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSearchNodes(const TSharedPtr<FJsonObject>& Params);

private:
	static UBlueprint* LoadBlueprint(const TSharedPtr<FJsonObject>& Params, FString& OutAssetPath);
};
