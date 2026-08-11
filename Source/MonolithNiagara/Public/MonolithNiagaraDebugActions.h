#pragma once
#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

/**
 * Live-simulation debug/readback actions for the niagara namespace.
 * Operates on LIVE UNiagaraComponents in the PIE or editor world (not on assets),
 * unlike the authoring actions in FMonolithNiagaraActions.
 */
class FMonolithNiagaraDebugActions
{
public:
	static void RegisterActions(FMonolithToolRegistry& Registry);

	// dump_particles — per-particle numeric readback (+ optional world-space debug draw)
	// for live NiagaraComponents. CPU-sim emitters only; GPU-sim emitters are reported
	// but skipped (their buffers live on the GPU).
	static FMonolithActionResult HandleDumpParticles(const TSharedPtr<FJsonObject>& Params);
};
