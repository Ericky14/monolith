#include "Actions/ProjectFindInputHandlerAction.h"
#include "MonolithIndexSubsystem.h"
#include "MonolithParamSchema.h"
#include "Editor.h"

FMonolithActionResult FProjectFindInputHandlerAction::Execute(const TSharedPtr<FJsonObject>& Params)
{
	const FString Key = Params->GetStringField(TEXT("key"));
	const int32 Limit = Params->HasField(TEXT("limit")) ? Params->GetIntegerField(TEXT("limit")) : 50;

	if (Key.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("'key' parameter is required (e.g. E, SpaceBar, Gamepad_FaceButton_Bottom)"), -32602);
	}

	UMonolithIndexSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UMonolithIndexSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return FMonolithActionResult::Error(TEXT("Index subsystem not available"));
	}
	if (Subsystem->IsIndexing())
	{
		return FMonolithActionResult::Error(TEXT("Indexing in progress - results would be incomplete; retry when done"));
	}

	const TArray<FInputHandlerResult> Handlers = Subsystem->FindInputHandlers(Key, Limit);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Arr;
	int32 UnhandledCount = 0;
	for (const FInputHandlerResult& H : Handlers)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("key"), H.KeyName);
		Entry->SetStringField(TEXT("input_action"), H.ActionName);
		Entry->SetStringField(TEXT("input_action_path"), H.ActionPath);
		Entry->SetStringField(TEXT("mapping_context"), H.ContextAssetPath);
		if (H.HandlerAssetPath.IsEmpty())
		{
			// Bound but unimplemented is a distinct, useful answer - flag it rather than omit it.
			++UnhandledCount;
			Entry->SetBoolField(TEXT("handled"), false);
		}
		else
		{
			Entry->SetBoolField(TEXT("handled"), true);
			Entry->SetStringField(TEXT("handler_asset"), H.HandlerAssetPath);
			Entry->SetStringField(TEXT("handler_graph"), H.HandlerGraphName);
			Entry->SetStringField(TEXT("handler_node_id"), H.HandlerNodeObjectName);
			Entry->SetStringField(TEXT("handler_node_title"), H.HandlerNodeTitle);
		}
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Result->SetStringField(TEXT("key"), Key);
	Result->SetArrayField(TEXT("bindings"), Arr);
	Result->SetNumberField(TEXT("count"), Handlers.Num());
	Result->SetNumberField(TEXT("unhandled_count"), UnhandledCount);
	if (Handlers.Num() == 0)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("No indexed binding for this key. Check the key's FName spelling (E, SpaceBar, ")
			TEXT("LeftMouseButton, Gamepad_FaceButton_Bottom), and note that keys bound only in C++ ")
			TEXT("or via runtime-added mapping contexts are not indexed."));
	}
	return FMonolithActionResult::Success(Result);
}

TSharedPtr<FJsonObject> FProjectFindInputHandlerAction::GetSchema()
{
	return FParamSchemaBuilder()
		.Required(TEXT("key"), TEXT("string"), TEXT("Key FName, e.g. E, SpaceBar, Gamepad_FaceButton_Bottom"))
		.Optional(TEXT("limit"), TEXT("integer"), TEXT("Maximum bindings to return"), TEXT("50"))
		.Build();
}
