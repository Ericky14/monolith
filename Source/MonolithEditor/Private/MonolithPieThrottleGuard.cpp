#include "MonolithPieThrottleGuard.h"

#include "MonolithJsonUtils.h"                // LogMonolith
#include "Editor.h"                           // FEditorDelegates::PrePIEEnded / EndPIE
#include "Editor/EditorPerformanceSettings.h" // UEditorPerformanceSettings (UnrealEd)

namespace
{
	// Process-lifetime suppression state. Game-thread only (every writer is an MCP handler,
	// the PIE-smoke frame observer, or an editor PIE delegate), so no synchronisation.
	bool GThrottleSuppressed = false;
	bool GOriginalThrottle = false;

	// File-unique names: anonymous-namespace statics still collide under UBT's unity builds, and
	// MonolithPieInputActions.cpp already owns a `GPrePieEndedHandle`.
	FDelegateHandle GThrottlePrePieEndedHandle;
	FDelegateHandle GThrottleEndPieHandle;
}

bool FMonolithPieThrottleGuard::Suppress()
{
	UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
	if (!Settings)
	{
		return false;
	}

	// Record the original ONLY on the transition into suppression. Two overlapping sessions
	// (or a restart before EndPIE lands) would otherwise capture the already-suppressed
	// `false` and restore that as the developer's preference.
	if (!GThrottleSuppressed)
	{
		GOriginalThrottle = (Settings->bThrottleCPUWhenNotForeground != 0);
		GThrottleSuppressed = true;

		UE_LOG(LogMonolith, Verbose,
			TEXT("PIE throttle guard: suppressing bThrottleCPUWhenNotForeground (was %s)"),
			GOriginalThrottle ? TEXT("ON") : TEXT("off"));
	}

	// In-memory write only, deliberately. The property is `config` (EditorSettings.ini) and
	// the engine re-reads GetDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground
	// live every frame (UEditorEngine::Tick + ShouldThrottleCPUUsage), so nothing has to reach
	// disk for this to take effect — and writing it would permanently rewrite the developer's
	// editor preference behind their back. PostEditChange() is skipped for the same reason and
	// because it would buy nothing here: the property carries no ConsoleVariable metadata, so
	// UDeveloperSettings would only re-broadcast to the settings UI.
	Settings->bThrottleCPUWhenNotForeground = false;
	return GOriginalThrottle;
}

void FMonolithPieThrottleGuard::Restore()
{
	if (!GThrottleSuppressed)
	{
		return; // never suppressed, or already restored — every end path calls this blind
	}
	GThrottleSuppressed = false;

	if (UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>())
	{
		Settings->bThrottleCPUWhenNotForeground = GOriginalThrottle ? 1 : 0;

		UE_LOG(LogMonolith, Verbose,
			TEXT("PIE throttle guard: restored bThrottleCPUWhenNotForeground to %s"),
			GOriginalThrottle ? TEXT("ON") : TEXT("off"));
	}
}

bool FMonolithPieThrottleGuard::IsSuppressed()
{
	return GThrottleSuppressed;
}

bool FMonolithPieThrottleGuard::WasThrottled()
{
	return GOriginalThrottle;
}

void FMonolithPieThrottleGuard::RegisterPieEndHook()
{
	// Both edges are bound: PrePIEEnded is the earliest orderly-teardown signal, EndPIE also
	// fires on paths that skip it (abort / crash teardown). Restore() is idempotent, so the
	// second one to arrive is free — and binding both means no PIE exit can strand the setting.
	if (!GThrottlePrePieEndedHandle.IsValid())
	{
		GThrottlePrePieEndedHandle = FEditorDelegates::PrePIEEnded.AddLambda(
			[](const bool /*bIsSimulating*/) { FMonolithPieThrottleGuard::Restore(); });
	}
	if (!GThrottleEndPieHandle.IsValid())
	{
		GThrottleEndPieHandle = FEditorDelegates::EndPIE.AddLambda(
			[](const bool /*bIsSimulating*/) { FMonolithPieThrottleGuard::Restore(); });
	}
}

void FMonolithPieThrottleGuard::UnregisterPieEndHook()
{
	if (GThrottlePrePieEndedHandle.IsValid())
	{
		FEditorDelegates::PrePIEEnded.Remove(GThrottlePrePieEndedHandle);
		GThrottlePrePieEndedHandle.Reset();
	}
	if (GThrottleEndPieHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(GThrottleEndPieHandle);
		GThrottleEndPieHandle.Reset();
	}

	// Module shutdown / Live Coding reload must not leave the developer's editor throttled-off
	// with nothing left alive to restore it.
	Restore();
}
