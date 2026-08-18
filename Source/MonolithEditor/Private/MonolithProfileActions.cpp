#include "MonolithProfileActions.h"
#include "MonolithParamSchema.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"

#if STATS
#include "Stats/StatsData.h"
#include "Stats/StatsCommand.h"
#include "Stats/StatsSystemTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMonolithProfile, Log, All);

#if STATS
namespace
{
	// GLog tap that buffers LogStats lines while a dump is in flight. The engine's
	// `stat DumpFrame` self-arms raw stats collection, waits for a complete frame on the
	// stats thread, and then prints the hierarchy via LogStats — inherently async across
	// frames, so the action is two-phase: start (install tap + issue command) → collect
	// (parse what arrived). Serialize() can be called from any thread; guard the buffer.
	class FStatsLogTap final : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == TEXT("LogStats"))
			{
				FScopeLock Lock(&Mutex);
				Lines.Add(V);
			}
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

		TArray<FString> Snapshot()
		{
			FScopeLock Lock(&Mutex);
			return Lines;
		}

	private:
		FCriticalSection Mutex;
		TArray<FString> Lines;
	};

	TUniquePtr<FStatsLogTap> GActiveTap;
	double GTapStartedAt = 0.0;

	// One dump line looks like:  "    12.345ms (  42)  -  Name - STAT_x - STATGROUP_y - STATCAT_z"
	// Indentation depth encodes the tree. We return a flat array with depth fields — simpler to
	// parse robustly than reconstructing a tree from whitespace, and just as readable for a model.
	bool ParseDumpLine(const FString& Line, int32& OutDepth, double& OutMs, int32& OutCalls, FString& OutName)
	{
		int32 Idx = 0;
		while (Idx < Line.Len() && Line[Idx] == TEXT(' ')) { ++Idx; }
		const int32 Indent = Idx;

		FString Rest = Line.Mid(Idx);
		int32 MsIdx;
		if (!Rest.FindChar(TEXT('m'), MsIdx) || MsIdx + 1 >= Rest.Len() || Rest[MsIdx + 1] != TEXT('s'))
		{
			return false;
		}
		const FString MsStr = Rest.Left(MsIdx);
		if (MsStr.IsEmpty() || !MsStr.IsNumeric() && !MsStr.Contains(TEXT(".")))
		{
			return false;
		}
		OutMs = FCString::Atod(*MsStr);

		// "( 42)" call count
		OutCalls = 0;
		int32 OpenIdx;
		if (Rest.FindChar(TEXT('('), OpenIdx))
		{
			int32 CloseIdx;
			if (Rest.FindChar(TEXT(')'), CloseIdx) && CloseIdx > OpenIdx)
			{
				OutCalls = FCString::Atoi(*Rest.Mid(OpenIdx + 1, CloseIdx - OpenIdx - 1).TrimStartAndEnd());
			}
		}

		// name = text after the first "-  ", trimmed of stat-group suffixes
		int32 DashIdx = Rest.Find(TEXT("-  "));
		if (DashIdx == INDEX_NONE) { return false; }
		FString Name = Rest.Mid(DashIdx + 3).TrimStartAndEnd();
		int32 SuffixIdx = Name.Find(TEXT(" - STAT"));
		if (SuffixIdx != INDEX_NONE) { Name = Name.Left(SuffixIdx); }
		OutName = Name;

		OutDepth = Indent / 2; // the dump indents two spaces per level
		return true;
	}
}
#endif // STATS

void FMonolithProfileActions::RegisterActions(FMonolithToolRegistry& Registry)
{
	Registry.RegisterAction(TEXT("editor"), TEXT("profile_frame"),
		TEXT("Native CPU frame profile via the engine's own `stat DumpFrame`, returned as structured data. TWO-PHASE because the dump is async across "
		     "frames: call once with no args (or collect=false) to START — this installs a log tap and issues the dump (which self-arms stats collection); "
		     "the editor/PIE must then advance a few frames. Call again with collect=true (~1s later) to HARVEST: returns entries[] of "
		     "{depth, ms (inclusive), calls, name} in dump order (depth encodes the tree; depth 0 = thread roots like GameThread/RenderThread). "
		     "threshold_ms maps to the dump's -ms= cull. If collect returns pending=true, frames have not advanced enough — ensure PIE or a focused "
		     "editor is ticking and retry. Typical use: start; wait ~1s while frames tick; collect; look at GameThread children (world tick vs Slate UI "
		     "vs streaming) and RenderThread children. STATS builds only (Development editor)."),
		FMonolithActionHandler::CreateStatic(&HandleProfileFrame),
		FParamSchemaBuilder()
			.Optional(TEXT("collect"), TEXT("bool"), TEXT("false/omitted = start a new dump; true = harvest the previously started dump."), TEXT("false"))
			.Optional(TEXT("threshold_ms"), TEXT("number"), TEXT("Cull entries below this inclusive milliseconds (default 0.2)."), TEXT("0.2"))
			.Build());

	UE_LOG(LogMonolithProfile, Log, TEXT("MonolithEditor: registered profile_frame action"));
}

#if STATS

FMonolithActionResult FMonolithProfileActions::HandleProfileFrame(const TSharedPtr<FJsonObject>& Params)
{
	bool bCollect = false;
	Params->TryGetBoolField(TEXT("collect"), bCollect);

	double ThresholdMs = 0.2;
	Params->TryGetNumberField(TEXT("threshold_ms"), ThresholdMs);
	ThresholdMs = FMath::Clamp(ThresholdMs, 0.0, 1000.0);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	if (!bCollect)
	{
		// START phase: (re)install the tap and issue the dump command.
		if (GActiveTap)
		{
			GLog->RemoveOutputDevice(GActiveTap.Get());
			GActiveTap.Reset();
		}
		GActiveTap = MakeUnique<FStatsLogTap>();
		GLog->AddOutputDevice(GActiveTap.Get());
		GTapStartedAt = FPlatformTime::Seconds();

		const FString Cmd = FString::Printf(TEXT("stat dumpframe -ms=%.3f"), ThresholdMs);
		UE::Stats::DirectStatsCommand(*Cmd, /*bBlockForCompletion=*/false);

		Root->SetBoolField(TEXT("started"), true);
		Root->SetStringField(TEXT("note"),
			TEXT("Dump issued. Let the editor/PIE tick ~1s (frames must advance for the stats thread to deliver a complete frame), then call profile_frame with collect=true."));
		return FMonolithActionResult::Success(Root);
	}

	// COLLECT phase.
	if (!GActiveTap)
	{
		return FMonolithActionResult::Error(TEXT("no dump in flight — call profile_frame without collect first"));
	}

	const TArray<FString> Lines = GActiveTap->Snapshot();

	TArray<TSharedPtr<FJsonValue>> Entries;
	bool bSawStack = false;
	for (const FString& Line : Lines)
	{
		if (Line.Contains(TEXT("Stack ---"))) { bSawStack = true; continue; }
		if (!bSawStack) { continue; }

		int32 Depth = 0, Calls = 0;
		double Ms = 0.0;
		FString Name;
		if (ParseDumpLine(Line, Depth, Ms, Calls, Name))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("depth"), Depth);
			Entry->SetNumberField(TEXT("ms"), FMath::RoundToDouble(Ms * 1000.0) / 1000.0);
			Entry->SetNumberField(TEXT("calls"), Calls);
			Entry->SetStringField(TEXT("name"), Name);
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	if (!bSawStack || Entries.Num() == 0)
	{
		Root->SetBoolField(TEXT("pending"), true);
		Root->SetNumberField(TEXT("seconds_since_start"), FPlatformTime::Seconds() - GTapStartedAt);
		Root->SetStringField(TEXT("note"),
			TEXT("Dump has not arrived yet — frames must advance (focused editor or PIE). Retry collect; if it never arrives, restart with a fresh start call."));
		return FMonolithActionResult::Success(Root);
	}

	GLog->RemoveOutputDevice(GActiveTap.Get());
	GActiveTap.Reset();

	Root->SetBoolField(TEXT("pending"), false);
	Root->SetNumberField(TEXT("entry_count"), Entries.Num());
	Root->SetArrayField(TEXT("entries"), Entries);
	return FMonolithActionResult::Success(Root);
}

#else // !STATS

FMonolithActionResult FMonolithProfileActions::HandleProfileFrame(const TSharedPtr<FJsonObject>&)
{
	return FMonolithActionResult::Error(TEXT("profile_frame requires the engine STATS system, which is not compiled into this build configuration"));
}

#endif // STATS
