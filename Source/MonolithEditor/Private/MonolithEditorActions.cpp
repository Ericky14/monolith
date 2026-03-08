#include "MonolithEditorActions.h"
#include "MonolithJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"

#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

// --- Compile state ---

FMonolithLogCapture* FMonolithEditorActions::CachedLogCapture = nullptr;
double FMonolithEditorActions::LastCompileTimestamp = 0.0;
FString FMonolithEditorActions::LastCompileResult = TEXT("none");
bool FMonolithEditorActions::bIsCompiling = false;
bool FMonolithEditorActions::bPatchApplied = false;
double FMonolithEditorActions::LastCompileEndTimestamp = 0.0;

// --- Log capture ---

void FMonolithLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	FScopeLock ScopeLock(&Lock);

	FMonolithLogEntry Entry;
	Entry.Timestamp = FPlatformTime::Seconds();
	Entry.Category = Category;
	Entry.Verbosity = Verbosity;
	Entry.Message = V;

	if (RingBuffer.Num() < MaxEntries)
	{
		RingBuffer.Add(MoveTemp(Entry));
	}
	else
	{
		RingBuffer[WriteIndex] = MoveTemp(Entry);
		bWrapped = true;
	}
	WriteIndex = (WriteIndex + 1) % MaxEntries;

	switch (Verbosity)
	{
	case ELogVerbosity::Fatal: ++TotalFatal; break;
	case ELogVerbosity::Error: ++TotalError; break;
	case ELogVerbosity::Warning: ++TotalWarning; break;
	case ELogVerbosity::Display:
	case ELogVerbosity::Log: ++TotalLog; break;
	case ELogVerbosity::Verbose:
	case ELogVerbosity::VeryVerbose: ++TotalVerbose; break;
	default: break;
	}
}

TArray<FMonolithLogEntry> FMonolithLogCapture::GetRecentEntries(int32 Count) const
{
	FScopeLock ScopeLock(&Lock);
	TArray<FMonolithLogEntry> Result;

	int32 Total = RingBuffer.Num();
	int32 Start = bWrapped ? WriteIndex : 0;
	int32 Num = FMath::Min(Count, Total);
	int32 Begin = bWrapped ? (WriteIndex - Num + Total) % Total : FMath::Max(0, Total - Num);

	for (int32 i = 0; i < Num; ++i)
	{
		int32 Idx = (Begin + i) % Total;
		Result.Add(RingBuffer[Idx]);
	}
	return Result;
}

TArray<FMonolithLogEntry> FMonolithLogCapture::SearchEntries(const FString& Pattern, const FString& CategoryFilter, ELogVerbosity::Type MaxVerbosity, int32 Limit) const
{
	FScopeLock ScopeLock(&Lock);
	TArray<FMonolithLogEntry> Result;

	FString PatternLower = Pattern.ToLower();
	int32 Total = RingBuffer.Num();
	int32 Start = bWrapped ? WriteIndex : 0;

	for (int32 i = 0; i < Total && Result.Num() < Limit; ++i)
	{
		int32 Idx = (Start + i) % Total;
		const FMonolithLogEntry& Entry = RingBuffer[Idx];

		if (Entry.Verbosity > MaxVerbosity) continue;
		if (!CategoryFilter.IsEmpty() && Entry.Category != FName(*CategoryFilter)) continue;
		if (!PatternLower.IsEmpty() && !Entry.Message.ToLower().Contains(PatternLower)) continue;

		Result.Add(Entry);
	}
	return Result;
}

TArray<FString> FMonolithLogCapture::GetActiveCategories() const
{
	FScopeLock ScopeLock(&Lock);
	TSet<FString> Categories;
	for (const FMonolithLogEntry& Entry : RingBuffer)
	{
		Categories.Add(Entry.Category.ToString());
	}
	return Categories.Array();
}

int32 FMonolithLogCapture::GetCountByVerbosity(ELogVerbosity::Type Verbosity) const
{
	FScopeLock ScopeLock(&Lock);
	switch (Verbosity)
	{
	case ELogVerbosity::Fatal: return TotalFatal;
	case ELogVerbosity::Error: return TotalError;
	case ELogVerbosity::Warning: return TotalWarning;
	case ELogVerbosity::Log: return TotalLog;
	case ELogVerbosity::Verbose: return TotalVerbose;
	default: return 0;
	}
}

int32 FMonolithLogCapture::GetTotalCount() const
{
	FScopeLock ScopeLock(&Lock);
	return TotalFatal + TotalError + TotalWarning + TotalLog + TotalVerbose;
}

TArray<FMonolithLogEntry> FMonolithLogCapture::GetEntriesSince(double SinceTimestamp, const TArray<FName>& CategoryFilter, ELogVerbosity::Type MaxVerbosity, int32 Limit) const
{
	FScopeLock ScopeLock(&Lock);
	TArray<FMonolithLogEntry> Result;

	int32 Total = RingBuffer.Num();
	int32 Start = bWrapped ? WriteIndex : 0;

	for (int32 i = 0; i < Total && Result.Num() < Limit; ++i)
	{
		int32 Idx = (Start + i) % Total;
		const FMonolithLogEntry& Entry = RingBuffer[Idx];

		if (Entry.Timestamp < SinceTimestamp) continue;
		if (Entry.Verbosity > MaxVerbosity) continue;
		if (CategoryFilter.Num() > 0 && !CategoryFilter.Contains(Entry.Category)) continue;

		Result.Add(Entry);
	}
	return Result;
}

int32 FMonolithLogCapture::CountErrorsSince(double SinceTimestamp) const
{
	FScopeLock ScopeLock(&Lock);
	int32 Count = 0;
	int32 Total = RingBuffer.Num();
	int32 Start = bWrapped ? WriteIndex : 0;

	for (int32 i = 0; i < Total; ++i)
	{
		int32 Idx = (Start + i) % Total;
		const FMonolithLogEntry& Entry = RingBuffer[Idx];
		if (Entry.Timestamp >= SinceTimestamp && Entry.Verbosity <= ELogVerbosity::Error)
		{
			++Count;
		}
	}
	return Count;
}

// --- Helpers ---

static FString VerbosityToString(ELogVerbosity::Type V)
{
	switch (V)
	{
	case ELogVerbosity::Fatal: return TEXT("fatal");
	case ELogVerbosity::Error: return TEXT("error");
	case ELogVerbosity::Warning: return TEXT("warning");
	case ELogVerbosity::Display: return TEXT("display");
	case ELogVerbosity::Log: return TEXT("log");
	case ELogVerbosity::Verbose: return TEXT("verbose");
	case ELogVerbosity::VeryVerbose: return TEXT("very_verbose");
	default: return TEXT("unknown");
	}
}

static ELogVerbosity::Type StringToVerbosity(const FString& S)
{
	if (S == TEXT("fatal")) return ELogVerbosity::Fatal;
	if (S == TEXT("error")) return ELogVerbosity::Error;
	if (S == TEXT("warning")) return ELogVerbosity::Warning;
	if (S == TEXT("display")) return ELogVerbosity::Display;
	if (S == TEXT("verbose")) return ELogVerbosity::Verbose;
	if (S == TEXT("very_verbose")) return ELogVerbosity::VeryVerbose;
	return ELogVerbosity::Log;
}

static TSharedPtr<FJsonObject> LogEntryToJson(const FMonolithLogEntry& Entry)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("timestamp"), Entry.Timestamp);
	Obj->SetStringField(TEXT("category"), Entry.Category.ToString());
	Obj->SetStringField(TEXT("verbosity"), VerbosityToString(Entry.Verbosity));
	Obj->SetStringField(TEXT("message"), Entry.Message);
	return Obj;
}

// --- Live Coding delegate ---

void FMonolithEditorActions::InitLiveCodingDelegate()
{
#if PLATFORM_WINDOWS
	ILiveCodingModule* LC = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (LC)
	{
		LC->GetOnPatchCompleteDelegate().AddStatic(&FMonolithEditorActions::OnLiveCodingPatchComplete);
	}
#endif
}

void FMonolithEditorActions::OnLiveCodingPatchComplete()
{
	bIsCompiling = false;
	bPatchApplied = true;
	LastCompileResult = TEXT("success");
	LastCompileEndTimestamp = FPlatformTime::Seconds();
}

static FString TimestampToIso(double PlatformSeconds)
{
	if (PlatformSeconds <= 0.0) return TEXT("never");
	FDateTime Now = FDateTime::UtcNow();
	double CurrentSeconds = FPlatformTime::Seconds();
	double Delta = CurrentSeconds - PlatformSeconds;
	FDateTime EventTime = Now - FTimespan::FromSeconds(Delta);
	return EventTime.ToIso8601();
}

// --- Registration ---

void FMonolithEditorActions::RegisterActions(FMonolithLogCapture* LogCapture)
{
	CachedLogCapture = LogCapture;
	FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

	Registry.RegisterAction(TEXT("editor"), TEXT("trigger_build"),
		TEXT("Trigger a Live Coding compile"),
		FMonolithActionHandler::CreateStatic(&HandleTriggerBuild));

	Registry.RegisterAction(TEXT("editor"), TEXT("live_compile"),
		TEXT("Trigger a Live Coding compile (alias for trigger_build). Params: wait (bool, optional) - block until compile finishes"),
		FMonolithActionHandler::CreateStatic(&HandleTriggerBuild));

	Registry.RegisterAction(TEXT("editor"), TEXT("get_build_errors"),
		TEXT("Get build errors and warnings. Params: since (float, seconds ago), category (string), compile_only (bool, filter to LogLiveCoding/LogCompile/LogLinker)"),
		FMonolithActionHandler::CreateStatic(&HandleGetBuildErrors));

	Registry.RegisterAction(TEXT("editor"), TEXT("get_build_status"),
		TEXT("Check compile status: compiling, last_result, last_compile_time, errors_since_compile, patch_applied"),
		FMonolithActionHandler::CreateStatic(&HandleGetBuildStatus));

	Registry.RegisterAction(TEXT("editor"), TEXT("get_build_summary"),
		TEXT("Get summary of last build (errors, warnings, time)"),
		FMonolithActionHandler::CreateStatic(&HandleGetBuildSummary));

	Registry.RegisterAction(TEXT("editor"), TEXT("search_build_output"),
		TEXT("Search build log output by pattern"),
		FMonolithActionHandler::CreateStatic(&HandleSearchBuildOutput));

	Registry.RegisterAction(TEXT("editor"), TEXT("get_recent_logs"),
		TEXT("Get recent editor log entries"),
		FMonolithActionHandler::CreateStatic(&HandleGetRecentLogs));

	Registry.RegisterAction(TEXT("editor"), TEXT("search_logs"),
		TEXT("Search log entries by category, verbosity, and text pattern"),
		FMonolithActionHandler::CreateStatic(&HandleSearchLogs));

	Registry.RegisterAction(TEXT("editor"), TEXT("tail_log"),
		TEXT("Get last N log lines"),
		FMonolithActionHandler::CreateStatic(&HandleTailLog));

	Registry.RegisterAction(TEXT("editor"), TEXT("get_log_categories"),
		TEXT("List active log categories"),
		FMonolithActionHandler::CreateStatic(&HandleGetLogCategories));

	Registry.RegisterAction(TEXT("editor"), TEXT("get_log_stats"),
		TEXT("Get log statistics by verbosity level"),
		FMonolithActionHandler::CreateStatic(&HandleGetLogStats));

	Registry.RegisterAction(TEXT("editor"), TEXT("get_compile_output"),
		TEXT("Get structured compile report: result, time, log lines from compile categories, error/warning counts, patch status"),
		FMonolithActionHandler::CreateStatic(&HandleGetCompileOutput));

	Registry.RegisterAction(TEXT("editor"), TEXT("get_crash_context"),
		TEXT("Get last crash/ensure context information"),
		FMonolithActionHandler::CreateStatic(&HandleGetCrashContext));

	InitLiveCodingDelegate();
}

// --- Build actions ---

FMonolithActionResult FMonolithEditorActions::HandleTriggerBuild(const TSharedPtr<FJsonObject>& Params)
{
#if PLATFORM_WINDOWS
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (!LiveCoding)
	{
		return FMonolithActionResult::Error(TEXT("Live Coding module not available"));
	}

	if (!LiveCoding->IsEnabledForSession() && !LiveCoding->IsEnabledByDefault())
	{
		LiveCoding->EnableByDefault(true);
		LiveCoding->EnableForSession(true);
	}

	if (LiveCoding->IsCompiling())
	{
		return FMonolithActionResult::Error(TEXT("A compile is already in progress"));
	}

	bool bWait = false;
	if (Params->HasField(TEXT("wait")))
	{
		bWait = Params->GetBoolField(TEXT("wait"));
	}

	LastCompileTimestamp = FPlatformTime::Seconds();
	bIsCompiling = true;
	bPatchApplied = false;

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	if (bWait)
	{
		ELiveCodingCompileResult CompileResult;
		bool bStarted = LiveCoding->Compile(ELiveCodingCompileFlags::WaitForCompletion, &CompileResult);

		bIsCompiling = false;
		LastCompileEndTimestamp = FPlatformTime::Seconds();
		Root->SetBoolField(TEXT("started"), bStarted);

		FString ResultStr;
		switch (CompileResult)
		{
		case ELiveCodingCompileResult::Success: ResultStr = TEXT("success"); bPatchApplied = true; break;
		case ELiveCodingCompileResult::NoChanges: ResultStr = TEXT("no_changes"); break;
		case ELiveCodingCompileResult::Failure: ResultStr = TEXT("failure"); break;
		case ELiveCodingCompileResult::Cancelled: ResultStr = TEXT("cancelled"); break;
		case ELiveCodingCompileResult::CompileStillActive: ResultStr = TEXT("compile_still_active"); break;
		case ELiveCodingCompileResult::NotStarted: ResultStr = TEXT("not_started"); break;
		default: ResultStr = TEXT("unknown"); break;
		}
		LastCompileResult = ResultStr;
		Root->SetStringField(TEXT("result"), ResultStr);
	}
	else
	{
		LiveCoding->Compile();
		LastCompileResult = TEXT("in_progress");
		Root->SetBoolField(TEXT("started"), true);
		Root->SetStringField(TEXT("result"), TEXT("in_progress"));
	}

	return FMonolithActionResult::Success(Root);
#else
	return FMonolithActionResult::Error(TEXT("Live Coding is only available on Windows"));
#endif
}

FMonolithActionResult FMonolithEditorActions::HandleGetBuildErrors(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ErrorsArr;
	TArray<TSharedPtr<FJsonValue>> WarningsArr;

	// Determine time window
	double SinceTimestamp = LastCompileTimestamp; // Default: since last compile
	if (Params->HasField(TEXT("since")))
	{
		double SecondsAgo = Params->GetNumberField(TEXT("since"));
		SinceTimestamp = FPlatformTime::Seconds() - SecondsAgo;
	}

	// Build category filter
	TArray<FName> CategoryFilter;
	bool bCompileOnly = false;
	if (Params->HasField(TEXT("compile_only")))
	{
		bCompileOnly = Params->GetBoolField(TEXT("compile_only"));
	}
	if (bCompileOnly)
	{
		CategoryFilter.Add(FName(TEXT("LogLiveCoding")));
		CategoryFilter.Add(FName(TEXT("LogCompile")));
		CategoryFilter.Add(FName(TEXT("LogLinker")));
	}
	else if (Params->HasField(TEXT("category")))
	{
		CategoryFilter.Add(FName(*Params->GetStringField(TEXT("category"))));
	}

	if (CachedLogCapture)
	{
		TArray<FMonolithLogEntry> Entries = CachedLogCapture->GetEntriesSince(
			SinceTimestamp, CategoryFilter, ELogVerbosity::Warning, 500);

		for (const FMonolithLogEntry& Entry : Entries)
		{
			if (Entry.Verbosity <= ELogVerbosity::Error)
			{
				TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
				ErrObj->SetStringField(TEXT("message"), Entry.Message);
				ErrObj->SetStringField(TEXT("category"), Entry.Category.ToString());
				ErrObj->SetStringField(TEXT("verbosity"), VerbosityToString(Entry.Verbosity));
				ErrorsArr.Add(MakeShared<FJsonValueObject>(ErrObj));
			}
			else if (Entry.Verbosity == ELogVerbosity::Warning)
			{
				TSharedPtr<FJsonObject> WarnObj = MakeShared<FJsonObject>();
				WarnObj->SetStringField(TEXT("message"), Entry.Message);
				WarnObj->SetStringField(TEXT("category"), Entry.Category.ToString());
				WarningsArr.Add(MakeShared<FJsonValueObject>(WarnObj));
			}
		}
	}

	Root->SetNumberField(TEXT("error_count"), ErrorsArr.Num());
	Root->SetArrayField(TEXT("errors"), ErrorsArr);
	Root->SetNumberField(TEXT("warning_count"), WarningsArr.Num());
	Root->SetArrayField(TEXT("warnings"), WarningsArr);
	Root->SetStringField(TEXT("since"), TimestampToIso(SinceTimestamp));

	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithEditorActions::HandleGetBuildStatus(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

#if PLATFORM_WINDOWS
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (LiveCoding)
	{
		bool bCurrentlyCompiling = LiveCoding->IsCompiling();

		// Update tracked state if LC reports done but we haven't caught it yet
		if (bIsCompiling && !bCurrentlyCompiling)
		{
			bIsCompiling = false;
			if (LastCompileEndTimestamp < LastCompileTimestamp)
			{
				LastCompileEndTimestamp = FPlatformTime::Seconds();
			}
		}

		Root->SetBoolField(TEXT("live_coding_available"), true);
		Root->SetBoolField(TEXT("live_coding_started"), LiveCoding->HasStarted());
		Root->SetBoolField(TEXT("live_coding_enabled"), LiveCoding->IsEnabledForSession());
		Root->SetBoolField(TEXT("compiling"), bCurrentlyCompiling);
	}
	else
	{
		Root->SetBoolField(TEXT("live_coding_available"), false);
		Root->SetBoolField(TEXT("compiling"), false);
	}
#else
	Root->SetBoolField(TEXT("live_coding_available"), false);
	Root->SetBoolField(TEXT("compiling"), false);
#endif

	Root->SetStringField(TEXT("last_result"), LastCompileResult);
	Root->SetStringField(TEXT("last_compile_time"), TimestampToIso(LastCompileTimestamp));
	Root->SetBoolField(TEXT("patch_applied"), bPatchApplied);

	if (CachedLogCapture && LastCompileTimestamp > 0.0)
	{
		Root->SetNumberField(TEXT("errors_since_compile"), CachedLogCapture->CountErrorsSince(LastCompileTimestamp));
	}
	else
	{
		Root->SetNumberField(TEXT("errors_since_compile"), 0);
	}

	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithEditorActions::HandleGetBuildSummary(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	// Get error/warning counts from log capture
	int32 ErrorCount = 0;
	int32 WarningCount = 0;

	if (CachedLogCapture)
	{
		ErrorCount = CachedLogCapture->GetCountByVerbosity(ELogVerbosity::Error);
		WarningCount = CachedLogCapture->GetCountByVerbosity(ELogVerbosity::Warning);
	}

	Root->SetNumberField(TEXT("total_errors"), ErrorCount);
	Root->SetNumberField(TEXT("total_warnings"), WarningCount);

#if PLATFORM_WINDOWS
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (LiveCoding)
	{
		Root->SetBoolField(TEXT("compiling"), LiveCoding->IsCompiling());
		Root->SetBoolField(TEXT("live_coding_started"), LiveCoding->HasStarted());
	}
#endif

	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithEditorActions::HandleSearchBuildOutput(const TSharedPtr<FJsonObject>& Params)
{
	FString Pattern = Params->GetStringField(TEXT("pattern"));
	if (Pattern.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: pattern"));
	}

	int32 Limit = 100;
	if (Params->HasField(TEXT("limit")))
	{
		Limit = static_cast<int32>(Params->GetNumberField(TEXT("limit")));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Matches;

	if (CachedLogCapture)
	{
		// Search for compile-related messages matching the pattern
		TArray<FMonolithLogEntry> Entries = CachedLogCapture->SearchEntries(
			Pattern, TEXT(""), ELogVerbosity::VeryVerbose, Limit);

		for (const FMonolithLogEntry& Entry : Entries)
		{
			Matches.Add(MakeShared<FJsonValueObject>(LogEntryToJson(Entry)));
		}
	}

	Root->SetStringField(TEXT("pattern"), Pattern);
	Root->SetNumberField(TEXT("match_count"), Matches.Num());
	Root->SetArrayField(TEXT("matches"), Matches);

	return FMonolithActionResult::Success(Root);
}

// --- Compile output ---

FMonolithActionResult FMonolithEditorActions::HandleGetCompileOutput(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	Root->SetStringField(TEXT("last_result"), LastCompileResult);
	Root->SetStringField(TEXT("last_compile_time"), TimestampToIso(LastCompileTimestamp));
	Root->SetStringField(TEXT("last_compile_end_time"), TimestampToIso(LastCompileEndTimestamp));
	Root->SetBoolField(TEXT("patch_applied"), bPatchApplied);
	Root->SetBoolField(TEXT("compiling"), bIsCompiling);

	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	TArray<TSharedPtr<FJsonValue>> LogLines;

	if (CachedLogCapture && LastCompileTimestamp > 0.0)
	{
		// Get all log lines from compile-related categories since last compile
		TArray<FName> CompileCategories;
		CompileCategories.Add(FName(TEXT("LogLiveCoding")));
		CompileCategories.Add(FName(TEXT("LogCompile")));
		CompileCategories.Add(FName(TEXT("LogLinker")));

		TArray<FMonolithLogEntry> Entries = CachedLogCapture->GetEntriesSince(
			LastCompileTimestamp, CompileCategories, ELogVerbosity::VeryVerbose, 500);

		for (const FMonolithLogEntry& Entry : Entries)
		{
			LogLines.Add(MakeShared<FJsonValueObject>(LogEntryToJson(Entry)));
			if (Entry.Verbosity <= ELogVerbosity::Error) ++ErrorCount;
			else if (Entry.Verbosity == ELogVerbosity::Warning) ++WarningCount;
		}
	}

	Root->SetNumberField(TEXT("error_count"), ErrorCount);
	Root->SetNumberField(TEXT("warning_count"), WarningCount);
	Root->SetNumberField(TEXT("log_line_count"), LogLines.Num());
	Root->SetArrayField(TEXT("compile_log"), LogLines);

	return FMonolithActionResult::Success(Root);
}

// --- Log actions ---

FMonolithActionResult FMonolithEditorActions::HandleGetRecentLogs(const TSharedPtr<FJsonObject>& Params)
{
	int32 Count = 100;
	if (Params->HasField(TEXT("count")))
	{
		Count = static_cast<int32>(Params->GetNumberField(TEXT("count")));
	}
	else if (Params->HasField(TEXT("max")))
	{
		Count = static_cast<int32>(Params->GetNumberField(TEXT("max")));
	}
	Count = FMath::Clamp(Count, 1, 1000);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> LogArr;

	if (CachedLogCapture)
	{
		TArray<FMonolithLogEntry> Entries = CachedLogCapture->GetRecentEntries(Count);
		for (const FMonolithLogEntry& Entry : Entries)
		{
			LogArr.Add(MakeShared<FJsonValueObject>(LogEntryToJson(Entry)));
		}
	}

	Root->SetNumberField(TEXT("count"), LogArr.Num());
	Root->SetArrayField(TEXT("entries"), LogArr);

	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithEditorActions::HandleSearchLogs(const TSharedPtr<FJsonObject>& Params)
{
	FString Pattern = Params->GetStringField(TEXT("pattern"));
	FString Category = Params->GetStringField(TEXT("category"));
	FString VerbosityStr = Params->GetStringField(TEXT("verbosity"));
	ELogVerbosity::Type MaxVerbosity = VerbosityStr.IsEmpty() ? ELogVerbosity::VeryVerbose : StringToVerbosity(VerbosityStr);

	int32 Limit = 200;
	if (Params->HasField(TEXT("limit")))
	{
		Limit = static_cast<int32>(Params->GetNumberField(TEXT("limit")));
	}
	Limit = FMath::Clamp(Limit, 1, 2000);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> LogArr;

	if (CachedLogCapture)
	{
		TArray<FMonolithLogEntry> Entries = CachedLogCapture->SearchEntries(Pattern, Category, MaxVerbosity, Limit);
		for (const FMonolithLogEntry& Entry : Entries)
		{
			LogArr.Add(MakeShared<FJsonValueObject>(LogEntryToJson(Entry)));
		}
	}

	Root->SetNumberField(TEXT("match_count"), LogArr.Num());
	Root->SetArrayField(TEXT("entries"), LogArr);

	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithEditorActions::HandleTailLog(const TSharedPtr<FJsonObject>& Params)
{
	int32 Count = 50;
	if (Params->HasField(TEXT("count")))
	{
		Count = static_cast<int32>(Params->GetNumberField(TEXT("count")));
	}
	Count = FMath::Clamp(Count, 1, 500);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Lines;

	if (CachedLogCapture)
	{
		TArray<FMonolithLogEntry> Entries = CachedLogCapture->GetRecentEntries(Count);
		for (const FMonolithLogEntry& Entry : Entries)
		{
			FString Line = FString::Printf(TEXT("[%s][%s] %s"),
				*Entry.Category.ToString(),
				*VerbosityToString(Entry.Verbosity),
				*Entry.Message);
			Lines.Add(MakeShared<FJsonValueString>(Line));
		}
	}

	Root->SetNumberField(TEXT("count"), Lines.Num());
	Root->SetArrayField(TEXT("lines"), Lines);

	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithEditorActions::HandleGetLogCategories(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> CatArr;

	if (CachedLogCapture)
	{
		TArray<FString> Categories = CachedLogCapture->GetActiveCategories();
		Categories.Sort();
		for (const FString& Cat : Categories)
		{
			CatArr.Add(MakeShared<FJsonValueString>(Cat));
		}
	}

	Root->SetNumberField(TEXT("count"), CatArr.Num());
	Root->SetArrayField(TEXT("categories"), CatArr);

	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithEditorActions::HandleGetLogStats(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	if (CachedLogCapture)
	{
		Root->SetNumberField(TEXT("total"), CachedLogCapture->GetTotalCount());
		Root->SetNumberField(TEXT("fatal"), CachedLogCapture->GetCountByVerbosity(ELogVerbosity::Fatal));
		Root->SetNumberField(TEXT("error"), CachedLogCapture->GetCountByVerbosity(ELogVerbosity::Error));
		Root->SetNumberField(TEXT("warning"), CachedLogCapture->GetCountByVerbosity(ELogVerbosity::Warning));
		Root->SetNumberField(TEXT("log"), CachedLogCapture->GetCountByVerbosity(ELogVerbosity::Log));
		Root->SetNumberField(TEXT("verbose"), CachedLogCapture->GetCountByVerbosity(ELogVerbosity::Verbose));
	}
	else
	{
		Root->SetNumberField(TEXT("total"), 0);
		Root->SetStringField(TEXT("status"), TEXT("log_capture_not_initialized"));
	}

	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithEditorActions::HandleGetCrashContext(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	// Check for crash log file on disk
	FString CrashLogPath = FPaths::ProjectLogDir() / TEXT("CrashContext.runtime-xml");
	bool bHasCrashLog = FPaths::FileExists(CrashLogPath);
	Root->SetBoolField(TEXT("has_crash_context"), bHasCrashLog);

	if (bHasCrashLog)
	{
		FString CrashXml;
		if (FFileHelper::LoadFileToString(CrashXml, *CrashLogPath))
		{
			// Truncate if very large
			if (CrashXml.Len() > 4096)
			{
				CrashXml = CrashXml.Left(4096) + TEXT("...(truncated)");
			}
			Root->SetStringField(TEXT("crash_xml"), CrashXml);
		}
	}

	// Also check ensure log
	FString EnsureLogPath = FPaths::ProjectLogDir() / TEXT("Ensures.log");
	if (FPaths::FileExists(EnsureLogPath))
	{
		FString EnsureLog;
		if (FFileHelper::LoadFileToString(EnsureLog, *EnsureLogPath))
		{
			if (EnsureLog.Len() > 4096)
			{
				EnsureLog = EnsureLog.Right(4096);
			}
			Root->SetStringField(TEXT("ensure_log"), EnsureLog);
		}
	}

	// Provide recent errors/fatals from log capture
	if (CachedLogCapture)
	{
		TArray<FMonolithLogEntry> ErrorEntries = CachedLogCapture->SearchEntries(
			TEXT(""), TEXT(""), ELogVerbosity::Error, 20);
		TArray<TSharedPtr<FJsonValue>> RecentErrors;
		for (const FMonolithLogEntry& Entry : ErrorEntries)
		{
			RecentErrors.Add(MakeShared<FJsonValueObject>(LogEntryToJson(Entry)));
		}
		Root->SetArrayField(TEXT("recent_errors"), RecentErrors);
	}

	return FMonolithActionResult::Success(Root);
}
