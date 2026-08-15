#pragma once

#include "CoreMinimal.h"

/**
 * C1: PIE-lifetime suppression of the editor's "Use Less CPU when in Background" setting
 * (UEditorPerformanceSettings::bThrottleCPUWhenNotForeground).
 *
 * Why it exists: when the editor is unfocused — the normal state while an MCP client drives
 * it — that setting throttles the editor to a few frames per second. PIE inherits the stall,
 * so melee hit windows open and close between frames and the verification harness reports
 * "the ability did not land" for an ability that is fine. It does not error, it lies; this is
 * the project's #1 documented false-negative source.
 *
 * Why this is NOT an RAII guard: every PIE-starting action here QUEUES the session and returns
 * immediately (the editor's real frame loop advances it). A function-scoped TGuardValue would
 * restore the setting before PIE rendered a single frame, leaving the throttle in force for
 * exactly the window it was meant to protect. So the suppression is process-scoped state that
 * outlives the handler and is undone on PIE end instead.
 *
 * Suppress() records the ORIGINAL value only on the transition into suppression, so repeated /
 * nested starts can never latch the suppressed `false` as the "original". Restore() is
 * idempotent and a no-op when nothing was suppressed, which is what lets every end path call
 * it unconditionally. Game-thread only, like the rest of the PIE machinery.
 */
class FMonolithPieThrottleGuard
{
public:
	/**
	 * Suppress background-CPU throttling. Returns the recorded ORIGINAL setting: true means the
	 * editor really WAS throttling, i.e. this call changed the outcome of the coming session.
	 */
	static bool Suppress();

	/** Restore the recorded original setting. No-op when not currently suppressed. */
	static void Restore();

	static bool IsSuppressed();

	/** The recorded original value. Only meaningful while IsSuppressed(). */
	static bool WasThrottled();

	/**
	 * Bind / unbind FEditorDelegates::PrePIEEnded + EndPIE so a session that ends on its own
	 * (developer pressed Stop, PIE crashed, map changed) still hands the setting back — the
	 * explicit stop paths cannot cover those. Called from the module's Startup/ShutdownModule;
	 * both are idempotent, and Unregister also performs a final Restore().
	 */
	static void RegisterPieEndHook();
	static void UnregisterPieEndHook();
};
