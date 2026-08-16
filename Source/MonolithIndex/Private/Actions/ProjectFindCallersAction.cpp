#include "Actions/ProjectFindCallersAction.h"
#include "MonolithIndexSubsystem.h"
#include "MonolithParamSchema.h"
#include "Editor.h"

FMonolithActionResult FProjectFindCallersAction::Execute(const TSharedPtr<FJsonObject>& Params)
{
	const FString Function = Params->GetStringField(TEXT("function"));
	const int32 Limit = Params->HasField(TEXT("limit")) ? Params->GetIntegerField(TEXT("limit")) : 50;

	if (Function.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("'function' parameter is required"), -32602);
	}

	UMonolithIndexSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UMonolithIndexSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return FMonolithActionResult::Error(TEXT("Index subsystem not available"));
	}
	if (Subsystem->IsIndexing())
	{
		// Reporting "0 callers" mid-reindex would read as "nothing calls this" - the exact wrong
		// conclusion. Say the index is busy instead.
		return FMonolithActionResult::Error(TEXT("Indexing in progress - results would be incomplete; retry when done"));
	}

	const TArray<FCallSite> Callers = Subsystem->FindCallers(Function, Limit);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FCallSite& Site : Callers)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_path"), Site.AssetPath);
		Entry->SetStringField(TEXT("asset_name"), Site.AssetName);
		Entry->SetStringField(TEXT("graph_name"), Site.GraphName);
		Entry->SetStringField(TEXT("node_id"), Site.NodeObjectName);
		Entry->SetStringField(TEXT("node_title"), Site.NodeTitle);
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetStringField(TEXT("function"), Function);
	Result->SetArrayField(TEXT("callers"), Arr);
	Result->SetNumberField(TEXT("count"), Callers.Num());
	if (Callers.Num() == 0)
	{
		// A real answer, but only as good as the index: say so rather than let an empty array be
		// read as proof of dead code.
		Result->SetStringField(TEXT("note"),
			TEXT("No indexed call sites. Blueprint-only result - C++ callers and dynamically-invoked ")
			TEXT("paths (interface messages, delegates, console commands) are not counted; re-run ")
			TEXT("project.reindex if assets changed since the last index."));
	}
	return FMonolithActionResult::Success(Result);
}

TSharedPtr<FJsonObject> FProjectFindCallersAction::GetSchema()
{
	return FParamSchemaBuilder()
		.Required(TEXT("function"), TEXT("string"), TEXT("Function name as called, e.g. EquipHotbarSelection"))
		.Optional(TEXT("limit"), TEXT("integer"), TEXT("Maximum call sites to return"), TEXT("50"))
		.Build();
}
