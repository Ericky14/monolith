#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

class FProjectFindCallersAction
{
public:
	static FMonolithActionResult Execute(const TSharedPtr<FJsonObject>& Params);
	static FString GetName() { return TEXT("find_callers"); }
	static FString GetDescription()
	{
		return TEXT("Every call site of a Blueprint function, with its address (asset + graph + node_id). ")
			TEXT("Answers \"what runs this?\" - use it before editing a function, and to tell live code from dead code ")
			TEXT("(zero callers = nothing invokes it). Exact name match on the indexed call target, so it does not ")
			TEXT("return the near-miss noise a text search does.");
	}
	static TSharedPtr<FJsonObject> GetSchema();
};
