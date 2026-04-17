#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

class FMonolithVoxelActions
{
public:
    static void RegisterActions();

    // --- Graph Queries ---
    static FMonolithActionResult HandleGetGraphInfo(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleListTerminalGraphs(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleGetGraphData(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleGetGraphSummary(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleGetParameters(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleSearchNodes(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleGetNodeDetails(const TSharedPtr<FJsonObject> &Params);
};
