#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

class FMonolithVoxelActions
{
public:
    static void RegisterActions();

    // Terrain-authoring (mutation) actions, defined in MonolithVoxelTerrainActions.cpp.
    // Called from RegisterActions().
    static void RegisterTerrainActions();

    // Heightmap-data ground queries, defined in MonolithVoxelHeightQueryActions.cpp.
    // Called from RegisterActions().
    static void RegisterHeightQueryActions();

    // --- Graph Queries ---
    static FMonolithActionResult HandleGetGraphInfo(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleListTerminalGraphs(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleGetGraphData(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleGetGraphSummary(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleGetParameters(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleSearchNodes(const TSharedPtr<FJsonObject> &Params);
    static FMonolithActionResult HandleGetNodeDetails(const TSharedPtr<FJsonObject> &Params);
};
