#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

class FJsonObject;

// Native frame profiling as a structured MCP action (editor namespace).
//
//   profile_frame — read the latest settled stats frame's RAW hierarchical stack (the same
//                   data `stat DumpFrame` prints to the log) and return it as a JSON tree:
//                   per-thread roots, each node {name, ms, calls, children}, culled by a
//                   millisecond threshold. Replaces the fragile capture-the-log workflow.
//
// Same STATS-gate caveats as get_stat_group_values: the stats stream must be producing
// frames (have a `stat` overlay active, e.g. run_console_command('stat unit'), and PIE or a
// ticking editor). Off-gate (Shipping/Test) returns a clean error.
class FMonolithProfileActions
{
public:
	static void RegisterActions(FMonolithToolRegistry& Registry);

	static FMonolithActionResult HandleProfileFrame(const TSharedPtr<FJsonObject>& Params);
};
