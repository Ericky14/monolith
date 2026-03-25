#include "MonolithBlueprintNodeActions.h"
#include "MonolithBlueprintInternal.h"
#include "MonolithBlueprintVariableActions.h"
#include "MonolithBlueprintComponentActions.h"
#include "MonolithBlueprintGraphActions.h"
#include "MonolithBlueprintCompileActions.h"
#include "MonolithJsonUtils.h"
#include "MonolithParamSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Timeline.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "K2Node_FunctionResult.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/TimelineTemplate.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"
#include "UObject/Package.h"

// ============================================================
//  Shared Node Alias Map (1G)
// ============================================================

struct FNodeAlias
{
	FString CanonicalType;
	TMap<FString, FString> DefaultParams; // auto-filled params (e.g., macro_name for ForEachLoop)
};

static const TMap<FString, FNodeAlias>& GetNodeAliases()
{
	static TMap<FString, FNodeAlias> Aliases;
	if (Aliases.Num() == 0)
	{
		// CallFunction aliases
		Aliases.Add(TEXT("function"),       {TEXT("CallFunction"), {}});
		Aliases.Add(TEXT("call_function"),  {TEXT("CallFunction"), {}});
		Aliases.Add(TEXT("call"),           {TEXT("CallFunction"), {}});
		Aliases.Add(TEXT("func"),           {TEXT("CallFunction"), {}});

		// VariableGet aliases
		Aliases.Add(TEXT("get"),            {TEXT("VariableGet"), {}});
		Aliases.Add(TEXT("variable_get"),   {TEXT("VariableGet"), {}});

		// VariableSet aliases
		Aliases.Add(TEXT("set"),            {TEXT("VariableSet"), {}});
		Aliases.Add(TEXT("variable_set"),   {TEXT("VariableSet"), {}});

		// CustomEvent aliases
		Aliases.Add(TEXT("event"),          {TEXT("CustomEvent"), {}});
		Aliases.Add(TEXT("custom_event"),   {TEXT("CustomEvent"), {}});

		// Branch aliases
		Aliases.Add(TEXT("branch"),         {TEXT("Branch"), {}});
		Aliases.Add(TEXT("if"),             {TEXT("Branch"), {}});

		// Sequence aliases
		Aliases.Add(TEXT("sequence"),       {TEXT("Sequence"), {}});

		// MacroInstance aliases
		Aliases.Add(TEXT("macro"),          {TEXT("MacroInstance"), {}});
		Aliases.Add(TEXT("macro_instance"), {TEXT("MacroInstance"), {}});

		// SpawnActorFromClass aliases
		Aliases.Add(TEXT("spawn_actor"),    {TEXT("SpawnActorFromClass"), {}});
		Aliases.Add(TEXT("spawn"),          {TEXT("SpawnActorFromClass"), {}});

		// DynamicCast aliases
		Aliases.Add(TEXT("cast"),           {TEXT("DynamicCast"), {}});
		Aliases.Add(TEXT("dynamic_cast"),   {TEXT("DynamicCast"), {}});

		// Self aliases (1B)
		Aliases.Add(TEXT("self"),           {TEXT("Self"), {}});

		// Return aliases (1B)
		Aliases.Add(TEXT("return"),         {TEXT("Return"), {}});
		Aliases.Add(TEXT("function_result"), {TEXT("Return"), {}});

		// --- Phase 1A: Macro shorthand aliases ---
		const FString StandardMacros = TEXT("/Engine/EditorBlueprintResources/StandardMacros");

		Aliases.Add(TEXT("foreachloop"),    {TEXT("MacroInstance"), {{TEXT("macro_name"), TEXT("ForEachLoop")}, {TEXT("macro_blueprint"), StandardMacros}}});
		Aliases.Add(TEXT("for_each"),       {TEXT("MacroInstance"), {{TEXT("macro_name"), TEXT("ForEachLoop")}, {TEXT("macro_blueprint"), StandardMacros}}});
		Aliases.Add(TEXT("forloop"),        {TEXT("MacroInstance"), {{TEXT("macro_name"), TEXT("ForLoop")}, {TEXT("macro_blueprint"), StandardMacros}}});
		Aliases.Add(TEXT("forloopwithbreak"), {TEXT("MacroInstance"), {{TEXT("macro_name"), TEXT("ForLoopWithBreak")}, {TEXT("macro_blueprint"), StandardMacros}}});
		Aliases.Add(TEXT("doonce"),         {TEXT("MacroInstance"), {{TEXT("macro_name"), TEXT("DoOnce")}, {TEXT("macro_blueprint"), StandardMacros}}});
		Aliases.Add(TEXT("do_once"),        {TEXT("MacroInstance"), {{TEXT("macro_name"), TEXT("DoOnce")}, {TEXT("macro_blueprint"), StandardMacros}}});
		Aliases.Add(TEXT("flipflop"),       {TEXT("MacroInstance"), {{TEXT("macro_name"), TEXT("FlipFlop")}, {TEXT("macro_blueprint"), StandardMacros}}});
		Aliases.Add(TEXT("flip_flop"),      {TEXT("MacroInstance"), {{TEXT("macro_name"), TEXT("FlipFlop")}, {TEXT("macro_blueprint"), StandardMacros}}});
		Aliases.Add(TEXT("gate"),           {TEXT("MacroInstance"), {{TEXT("macro_name"), TEXT("Gate")}, {TEXT("macro_blueprint"), StandardMacros}}});

		// --- Phase 1A: CallFunction shorthand aliases ---
		Aliases.Add(TEXT("isvalid"),        {TEXT("CallFunction"), {{TEXT("function_name"), TEXT("IsValid")}, {TEXT("target_class"), TEXT("KismetSystemLibrary")}}});
		Aliases.Add(TEXT("is_valid"),       {TEXT("CallFunction"), {{TEXT("function_name"), TEXT("IsValid")}, {TEXT("target_class"), TEXT("KismetSystemLibrary")}}});
		Aliases.Add(TEXT("delay"),          {TEXT("CallFunction"), {{TEXT("function_name"), TEXT("Delay")}, {TEXT("target_class"), TEXT("KismetSystemLibrary")}}});
		Aliases.Add(TEXT("retriggerabledelay"), {TEXT("CallFunction"), {{TEXT("function_name"), TEXT("RetriggerableDelay")}, {TEXT("target_class"), TEXT("KismetSystemLibrary")}}});
	}
	return Aliases;
}

// ============================================================
//  MonolithBlueprintInternal helpers
// ============================================================

bool MonolithBlueprintInternal::HasCustomEventNamed(UBlueprint* BP, FName EventName)
{
	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	for (UEdGraph* G : AllGraphs)
	{
		if (!G) continue;
		for (UEdGraphNode* N : G->Nodes)
		{
			UK2Node_CustomEvent* Existing = Cast<UK2Node_CustomEvent>(N);
			if (Existing && Existing->CustomFunctionName == EventName)
			{
				return true;
			}
		}
	}
	return false;
}

// ============================================================
//  Registration
// ============================================================

void FMonolithBlueprintNodeActions::RegisterActions(FMonolithToolRegistry& Registry)
{
	Registry.RegisterAction(TEXT("blueprint"), TEXT("add_node"),
		TEXT("Add a new node to a Blueprint graph. Supports CallFunction, VariableGet, VariableSet, CustomEvent, Branch, Sequence, MacroInstance, SpawnActorFromClass, DynamicCast, Self, Return node types. Also supports shorthand aliases: ForEachLoop, ForLoop, ForLoopWithBreak, DoOnce, FlipFlop, Gate (macro shortcuts), IsValid, Delay, RetriggerableDelay (function shortcuts)."),
		FMonolithActionHandler::CreateStatic(&HandleAddNode),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),       TEXT("string"),  TEXT("Blueprint asset path"))
			.Required(TEXT("node_type"),         TEXT("string"),  TEXT("Node type: CallFunction (or 'function'/'call'), VariableGet (or 'get'), VariableSet (or 'set'), CustomEvent (or 'event'), Branch (or 'if'), Sequence, MacroInstance (or 'macro'), SpawnActorFromClass (or 'spawn'), DynamicCast (or 'cast'), Self, Return. Shortcuts: ForEachLoop, ForLoop, DoOnce, FlipFlop, Gate, IsValid, Delay, RetriggerableDelay"))
			.Optional(TEXT("graph_name"),        TEXT("string"),  TEXT("Graph name (defaults to EventGraph)"))
			.Optional(TEXT("position"),          TEXT("array"),   TEXT("Node position as [x, y] (default: [0, 0])"))
			.Optional(TEXT("function_name"),     TEXT("string"),  TEXT("Function name for CallFunction nodes (e.g. PrintString)"))
			.Optional(TEXT("target_class"),      TEXT("string"),  TEXT("Class to search for the function (e.g. KismetSystemLibrary). If omitted, searches all loaded classes."))
			.Optional(TEXT("variable_name"),     TEXT("string"),  TEXT("Variable name for VariableGet/VariableSet nodes"))
			.Optional(TEXT("event_name"),        TEXT("string"),  TEXT("Custom event name for CustomEvent nodes"))
			.Optional(TEXT("macro_name"),        TEXT("string"),  TEXT("Macro graph name for MacroInstance nodes"))
			.Optional(TEXT("macro_blueprint"),   TEXT("string"),  TEXT("Blueprint asset path containing the macro (optional for MacroInstance)"))
			.Optional(TEXT("cast_class"),        TEXT("string"),  TEXT("Class name for DynamicCast nodes (e.g. 'MyPawn'). Accepts A/U prefix or bare name."))
			.Optional(TEXT("actor_class"),       TEXT("string"),  TEXT("Actor class name for SpawnActorFromClass nodes"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("remove_node"),
		TEXT("Remove a node from a Blueprint graph by node ID."),
		FMonolithActionHandler::CreateStatic(&HandleRemoveNode),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),  TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("node_id"),     TEXT("string"), TEXT("Node ID (from get_nodes or add_node response)"))
			.Optional(TEXT("graph_name"),  TEXT("string"), TEXT("Graph name (searches all graphs if omitted)"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("connect_pins"),
		TEXT("Connect two pins in a Blueprint graph. Source pin must be an output, target pin must be an input (or vice versa — the schema will sort it out)."),
		FMonolithActionHandler::CreateStatic(&HandleConnectPins),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),   TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("source_node"),  TEXT("string"), TEXT("Source node ID"))
			.Required(TEXT("source_pin"),   TEXT("string"), TEXT("Source pin name"))
			.Required(TEXT("target_node"),  TEXT("string"), TEXT("Target node ID"))
			.Required(TEXT("target_pin"),   TEXT("string"), TEXT("Target pin name"))
			.Optional(TEXT("graph_name"),   TEXT("string"), TEXT("Graph name (searches all graphs if omitted)"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("disconnect_pins"),
		TEXT("Disconnect a pin on a Blueprint node. If target_node and target_pin are omitted, all connections on the pin are broken."),
		FMonolithActionHandler::CreateStatic(&HandleDisconnectPins),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),   TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("node_id"),      TEXT("string"), TEXT("Node ID containing the pin to disconnect"))
			.Required(TEXT("pin_name"),     TEXT("string"), TEXT("Pin name to disconnect"))
			.Optional(TEXT("target_node"),  TEXT("string"), TEXT("Target node ID — if provided, only breaks the connection to this specific node"))
			.Optional(TEXT("target_pin"),   TEXT("string"), TEXT("Target pin name — required if target_node is specified"))
			.Optional(TEXT("graph_name"),   TEXT("string"), TEXT("Graph name (searches all graphs if omitted)"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("set_pin_default"),
		TEXT("Set the default value of a pin on a Blueprint node."),
		FMonolithActionHandler::CreateStatic(&HandleSetPinDefault),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),  TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("node_id"),     TEXT("string"), TEXT("Node ID"))
			.Required(TEXT("pin_name"),    TEXT("string"), TEXT("Pin name"))
			.Required(TEXT("value"),       TEXT("string"), TEXT("Default value as string"))
			.Optional(TEXT("graph_name"),  TEXT("string"), TEXT("Graph name (searches all graphs if omitted)"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("set_node_position"),
		TEXT("Move a Blueprint graph node to a new position."),
		FMonolithActionHandler::CreateStatic(&HandleSetNodePosition),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),  TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("node_id"),     TEXT("string"), TEXT("Node ID"))
			.Required(TEXT("position"),    TEXT("array"),  TEXT("New position as [x, y]"))
			.Optional(TEXT("graph_name"),  TEXT("string"), TEXT("Graph name (searches all graphs if omitted)"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("resolve_node"),
		TEXT("Dry-run node creation — returns resolved type, class, function, and all pins with types/defaults/direction without modifying any asset. Useful for discovering what pins a node will have before adding it."),
		FMonolithActionHandler::CreateStatic(&HandleResolveNode),
		FParamSchemaBuilder()
			.Required(TEXT("node_type"),     TEXT("string"), TEXT("Node type: CallFunction, VariableGet, VariableSet, Branch, CustomEvent (same aliases as add_node)"))
			.Optional(TEXT("function_name"), TEXT("string"), TEXT("Function name for CallFunction nodes"))
			.Optional(TEXT("target_class"),  TEXT("string"), TEXT("Class to search for the function (optional for CallFunction)"))
			.Optional(TEXT("variable_name"), TEXT("string"), TEXT("Variable name hint for VariableGet/VariableSet (uses wildcard if omitted)"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("batch_execute"),
		TEXT("Execute multiple Blueprint write operations on a single asset in one transaction. Each operation is { \"op\": \"action_name\", ...action_params_minus_asset_path }. Supported ops: add_node, remove_node, connect_pins, disconnect_pins, set_pin_default, set_node_position, add_variable, remove_variable, rename_variable, set_variable_type, set_variable_defaults, add_local_variable, remove_local_variable, add_component, remove_component, rename_component, reparent_component, set_component_property, duplicate_component, add_function, remove_function, rename_function, add_macro, remove_macro, rename_macro, add_event_dispatcher, set_function_params, implement_interface, remove_interface, scaffold_interface_implementation, add_timeline, add_event_node, add_comment_node, promote_pin_to_variable, add_replicated_variable, save_asset."),
		FMonolithActionHandler::CreateStatic(&HandleBatchExecute),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),         TEXT("string"),  TEXT("Blueprint asset path"))
			.Required(TEXT("operations"),          TEXT("array"),   TEXT("Array of operation objects: { op, ...params }"))
			.Optional(TEXT("compile_on_complete"), TEXT("boolean"), TEXT("Compile the Blueprint after all operations complete (default: false)"))
			.Optional(TEXT("stop_on_error"),       TEXT("boolean"), TEXT("Stop processing on first failed operation (default: false)"))
			.Build());

	// ---- Wave 4 ----

	Registry.RegisterAction(TEXT("blueprint"), TEXT("add_nodes_bulk"),
		TEXT("Place multiple nodes in one transaction. Returns a temp_id -> node_id mapping so callers can immediately reference created nodes in connect_pins_bulk. Each entry: { temp_id, node_type, function_name?, target_class?, variable_name?, position? }."),
		FMonolithActionHandler::CreateStatic(&HandleAddNodesBulk),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),  TEXT("string"),  TEXT("Blueprint asset path"))
			.Required(TEXT("nodes"),       TEXT("array"),   TEXT("Array of node descriptors: { temp_id, node_type, function_name?, target_class?, variable_name?, position? }"))
			.Optional(TEXT("graph_name"),  TEXT("string"),  TEXT("Graph name (defaults to EventGraph)"))
			.Optional(TEXT("auto_layout"), TEXT("boolean"), TEXT("Auto-position nodes in a 5-column grid (200px horizontal, 100px vertical spacing). Ignored if position is set per node. Default: false."))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("connect_pins_bulk"),
		TEXT("Wire multiple pin connections in one transaction. Each entry: { source_node, source_pin, target_node, target_pin }. Returns per-connection success/error."),
		FMonolithActionHandler::CreateStatic(&HandleConnectPinsBulk),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),   TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("connections"),  TEXT("array"),  TEXT("Array of connection descriptors: { source_node, source_pin, target_node, target_pin }"))
			.Optional(TEXT("graph_name"),   TEXT("string"), TEXT("Graph name (searches all graphs if omitted)"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("set_pin_defaults_bulk"),
		TEXT("Set multiple pin default values in one transaction. Each entry: { node_id, pin_name, value }. Returns per-entry success/error."),
		FMonolithActionHandler::CreateStatic(&HandleSetPinDefaultsBulk),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("defaults"),   TEXT("array"),  TEXT("Array of pin default descriptors: { node_id, pin_name, value }"))
			.Optional(TEXT("graph_name"), TEXT("string"), TEXT("Graph name (searches all graphs if omitted)"))
			.Build());

	// ---- Wave 5 ----

	Registry.RegisterAction(TEXT("blueprint"), TEXT("add_timeline"),
		TEXT("Create a Timeline node in a Blueprint event graph. Handles both the UTimelineTemplate (data) and UK2Node_Timeline (graph node) creation with GUID linkage validation. Only works in event graphs (ubergraph pages), not function graphs."),
		FMonolithActionHandler::CreateStatic(&HandleAddTimeline),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),    TEXT("string"),  TEXT("Blueprint asset path"))
			.Optional(TEXT("timeline_name"), TEXT("string"),  TEXT("Timeline variable name (auto-generated if omitted)"))
			.Optional(TEXT("graph_name"),    TEXT("string"),  TEXT("Event graph name (defaults to EventGraph)"))
			.Optional(TEXT("auto_play"),     TEXT("boolean"), TEXT("Start playing automatically (default: false)"))
			.Optional(TEXT("loop"),          TEXT("boolean"), TEXT("Loop the timeline (default: false)"))
			.Optional(TEXT("position"),      TEXT("array"),   TEXT("Node position as [x, y] (default: [0, 0])"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("add_event_node"),
		TEXT("Add a native override event node (BeginPlay, Tick, EndPlay, etc.) or custom event to a Blueprint event graph. Alias table: BeginPlay->ReceiveBeginPlay, Tick->ReceiveTick, EndPlay->ReceiveEndPlay, BeginOverlap->ReceiveActorBeginOverlap, EndOverlap->ReceiveActorEndOverlap, Hit->ReceiveHit, Destroyed->ReceiveDestroyed, AnyDamage->ReceiveAnyDamage, PointDamage->ReceivePointDamage, RadialDamage->ReceiveRadialDamage."),
		FMonolithActionHandler::CreateStatic(&HandleAddEventNode),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),  TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("event_name"),  TEXT("string"), TEXT("Event name: BeginPlay, Tick, EndPlay, BeginOverlap, EndOverlap, Hit, Destroyed, AnyDamage, PointDamage, RadialDamage, or a custom event name"))
			.Optional(TEXT("graph_name"),  TEXT("string"), TEXT("Event graph name (defaults to EventGraph)"))
			.Optional(TEXT("position"),    TEXT("array"),  TEXT("Node position as [x, y] (default: [0, 0])"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("add_comment_node"),
		TEXT("Add a comment box to a Blueprint graph, optionally enclosing a set of nodes. If node_ids is provided, the comment box auto-sizes to contain those nodes with 50px padding."),
		FMonolithActionHandler::CreateStatic(&HandleAddCommentNode),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),  TEXT("string"),  TEXT("Blueprint asset path"))
			.Required(TEXT("text"),        TEXT("string"),  TEXT("Comment box text"))
			.Optional(TEXT("graph_name"),  TEXT("string"),  TEXT("Graph name (defaults to EventGraph)"))
			.Optional(TEXT("node_ids"),    TEXT("array"),   TEXT("Array of node IDs to enclose in the comment box (auto-sizes with 50px padding)"))
			.Optional(TEXT("color"),       TEXT("object"),  TEXT("Comment color as {r, g, b, a} floats 0-1 (default: yellow {r:1, g:1, b:0, a:0.6})"))
			.Optional(TEXT("font_size"),   TEXT("integer"), TEXT("Comment text font size (default: 18)"))
			.Optional(TEXT("position"),    TEXT("array"),   TEXT("Node position as [x, y] — overridden if node_ids provided"))
			.Optional(TEXT("width"),       TEXT("integer"), TEXT("Comment box width — overridden if node_ids provided"))
			.Optional(TEXT("height"),      TEXT("integer"), TEXT("Comment box height — overridden if node_ids provided"))
			.Build());

	// ---- Wave 7 ----

	Registry.RegisterAction(TEXT("blueprint"), TEXT("promote_pin_to_variable"),
		TEXT("Promote a scalar pin on an existing node to a Blueprint member variable, then create and wire a VariableGet (for output pins) or VariableSet (for input pins) node in its place. "
		     "Supports scalar types only (bool, int, float, double, string, name, text, vector, rotator, transform, object refs, soft refs, enums, structs). "
		     "Container types (Array, Map, Set) are not supported in v1 — use add_variable + manual wiring instead."),
		FMonolithActionHandler::CreateStatic(&HandlePromotePinToVariable),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),     TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("node_id"),        TEXT("string"), TEXT("Node ID containing the pin to promote"))
			.Required(TEXT("pin_name"),       TEXT("string"), TEXT("Name of the pin to promote to a variable"))
			.Optional(TEXT("variable_name"),  TEXT("string"), TEXT("Name for the new variable (defaults to pin_name)"))
			.Optional(TEXT("graph_name"),     TEXT("string"), TEXT("Graph name (searches all graphs if omitted)"))
			.Build());
}

// ============================================================
//  add_node
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleAddNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString NodeType = Params->GetStringField(TEXT("node_type"));
	if (NodeType.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: node_type"));
	}

	// Normalize aliases to canonical node type names (shared map from 1G)
	{
		FString Lower = NodeType.ToLower();
		const auto& Aliases = GetNodeAliases();
		if (const FNodeAlias* Alias = Aliases.Find(Lower))
		{
			NodeType = Alias->CanonicalType;
			// Merge default params — only if the caller didn't already set them (1A)
			for (const auto& KV : Alias->DefaultParams)
			{
				if (!Params->HasField(KV.Key))
				{
					Params->SetStringField(KV.Key, KV.Value);
				}
			}
		}
	}

	FString GraphName = Params->GetStringField(TEXT("graph_name"));
	UEdGraph* Graph = MonolithBlueprintInternal::FindGraphByName(BP, GraphName);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Graph not found: %s"), GraphName.IsEmpty() ? TEXT("EventGraph") : *GraphName));
	}

	// Parse position
	int32 PosX = 0;
	int32 PosY = 0;
	const TArray<TSharedPtr<FJsonValue>>* PosArray = nullptr;
	if (Params->TryGetArrayField(TEXT("position"), PosArray) && PosArray && PosArray->Num() >= 2)
	{
		PosX = (int32)(*PosArray)[0]->AsNumber();
		PosY = (int32)(*PosArray)[1]->AsNumber();
	}

	UEdGraphNode* NewNode = nullptr;

	// ---- CallFunction ----
	if (NodeType == TEXT("CallFunction"))
	{
		FString FuncName = Params->GetStringField(TEXT("function_name"));
		if (FuncName.IsEmpty())
		{
			return FMonolithActionResult::Error(TEXT("CallFunction node requires 'function_name'"));
		}

		FString TargetClassName = Params->GetStringField(TEXT("target_class"));

		UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);

		// Build list of function name variants to try:
		// Blueprint-callable wrappers use K2_ prefix (e.g. GetActorLocation → K2_GetActorLocation)
		TArray<FName> FuncNameCandidates;
		FuncNameCandidates.Add(FName(*FuncName));
		if (!FuncName.StartsWith(TEXT("K2_")))
		{
			FuncNameCandidates.Add(FName(*FString::Printf(TEXT("K2_%s"), *FuncName)));
		}

		UFunction* Func = nullptr;
		if (!TargetClassName.IsEmpty())
		{
			// Resolve class name — try as-is, with U prefix, and without U prefix
			UClass* TargetClass = FindFirstObject<UClass>(*TargetClassName, EFindFirstObjectOptions::NativeFirst);
			if (!TargetClass && !TargetClassName.StartsWith(TEXT("U")))
				TargetClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *TargetClassName), EFindFirstObjectOptions::NativeFirst);
			if (!TargetClass && TargetClassName.StartsWith(TEXT("U")))
				TargetClass = FindFirstObject<UClass>(*TargetClassName.Mid(1), EFindFirstObjectOptions::NativeFirst);

			if (TargetClass)
			{
				// FindFunctionByName searches the full inheritance chain by default
				for (const FName& Candidate : FuncNameCandidates)
				{
					Func = TargetClass->FindFunctionByName(Candidate);
					if (Func) break;
				}
			}
			if (!Func)
			{
				return FMonolithActionResult::Error(FString::Printf(
					TEXT("Function '%s' not found on class '%s' (also tried K2_ prefix). Ensure the function is BlueprintCallable."),
					*FuncName, *TargetClassName));
			}
		}
		else
		{
			for (TObjectIterator<UClass> It; It && !Func; ++It)
			{
				for (const FName& Candidate : FuncNameCandidates)
				{
					Func = It->FindFunctionByName(Candidate);
					if (Func) break;
				}
			}
			if (!Func)
			{
				return FMonolithActionResult::Error(FString::Printf(
					TEXT("Function '%s' not found in any loaded class (also tried K2_ prefix)"), *FuncName));
			}
		}

		CallNode->SetFromFunction(Func);
		CallNode->NodePosX = PosX;
		CallNode->NodePosY = PosY;
		Graph->AddNode(CallNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);
		CallNode->AllocateDefaultPins();
		NewNode = CallNode;
	}
	// ---- VariableGet ----
	else if (NodeType == TEXT("VariableGet"))
	{
		FString VarName = Params->GetStringField(TEXT("variable_name"));
		if (VarName.IsEmpty())
		{
			return FMonolithActionResult::Error(TEXT("VariableGet node requires 'variable_name'"));
		}

		UK2Node_VariableGet* VarNode = NewObject<UK2Node_VariableGet>(Graph);
		VarNode->VariableReference.SetSelfMember(FName(*VarName));
		VarNode->NodePosX = PosX;
		VarNode->NodePosY = PosY;
		Graph->AddNode(VarNode, true, false);
		VarNode->AllocateDefaultPins();
		NewNode = VarNode;
	}
	// ---- VariableSet ----
	else if (NodeType == TEXT("VariableSet"))
	{
		FString VarName = Params->GetStringField(TEXT("variable_name"));
		if (VarName.IsEmpty())
		{
			return FMonolithActionResult::Error(TEXT("VariableSet node requires 'variable_name'"));
		}

		UK2Node_VariableSet* VarNode = NewObject<UK2Node_VariableSet>(Graph);
		VarNode->VariableReference.SetSelfMember(FName(*VarName));
		VarNode->NodePosX = PosX;
		VarNode->NodePosY = PosY;
		Graph->AddNode(VarNode, true, false);
		VarNode->AllocateDefaultPins();
		NewNode = VarNode;
	}
	// ---- CustomEvent ----
	else if (NodeType == TEXT("CustomEvent"))
	{
		FString EventName = Params->GetStringField(TEXT("event_name"));
		if (EventName.IsEmpty())
		{
			return FMonolithActionResult::Error(TEXT("CustomEvent node requires 'event_name'"));
		}

		if (MonolithBlueprintInternal::HasCustomEventNamed(BP, FName(*EventName)))
		{
			return FMonolithActionResult::Error(FString::Printf(
				TEXT("A custom event named '%s' already exists in this Blueprint"), *EventName));
		}

		UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(Graph);
		EventNode->CustomFunctionName = FName(*EventName);
		EventNode->NodePosX = PosX;
		EventNode->NodePosY = PosY;
		Graph->AddNode(EventNode, true, false);
		EventNode->AllocateDefaultPins();
		NewNode = EventNode;
	}
	// ---- Branch ----
	else if (NodeType == TEXT("Branch"))
	{
		UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(Graph);
		BranchNode->NodePosX = PosX;
		BranchNode->NodePosY = PosY;
		Graph->AddNode(BranchNode, true, false);
		BranchNode->AllocateDefaultPins();
		NewNode = BranchNode;
	}
	// ---- Sequence ----
	else if (NodeType == TEXT("Sequence"))
	{
		UK2Node_ExecutionSequence* SeqNode = NewObject<UK2Node_ExecutionSequence>(Graph);
		SeqNode->NodePosX = PosX;
		SeqNode->NodePosY = PosY;
		Graph->AddNode(SeqNode, true, false);
		SeqNode->AllocateDefaultPins();
		NewNode = SeqNode;
	}
	// ---- MacroInstance ----
	else if (NodeType == TEXT("MacroInstance"))
	{
		FString MacroName = Params->GetStringField(TEXT("macro_name"));
		if (MacroName.IsEmpty())
		{
			return FMonolithActionResult::Error(TEXT("MacroInstance node requires 'macro_name'"));
		}

		// Resolve the macro graph — search current BP first, then optional macro_blueprint
		UEdGraph* MacroGraph = nullptr;
		FString MacroBPPath = Params->GetStringField(TEXT("macro_blueprint"));
		if (!MacroBPPath.IsEmpty())
		{
			UBlueprint* MacroBP = FMonolithAssetUtils::LoadAssetByPath<UBlueprint>(MacroBPPath);
			if (MacroBP)
			{
				for (const auto& MG : MacroBP->MacroGraphs)
				{
					if (MG && MG->GetName() == MacroName)
					{
						MacroGraph = MG;
						break;
					}
				}
			}
			if (!MacroGraph)
			{
				return FMonolithActionResult::Error(FString::Printf(
					TEXT("Macro '%s' not found in blueprint '%s'"), *MacroName, *MacroBPPath));
			}
		}
		else
		{
			for (const auto& MG : BP->MacroGraphs)
			{
				if (MG && MG->GetName() == MacroName)
				{
					MacroGraph = MG;
					break;
				}
			}
			if (!MacroGraph)
			{
				return FMonolithActionResult::Error(FString::Printf(
					TEXT("Macro '%s' not found in this Blueprint. Provide 'macro_blueprint' if it's in another BP."), *MacroName));
			}
		}

		UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(Graph);
		MacroNode->SetMacroGraph(MacroGraph);
		MacroNode->NodePosX = PosX;
		MacroNode->NodePosY = PosY;
		Graph->AddNode(MacroNode, true, false);
		MacroNode->AllocateDefaultPins();
		NewNode = MacroNode;
	}
	// ---- SpawnActorFromClass ----
	else if (NodeType == TEXT("SpawnActorFromClass"))
	{
		FString ActorClassName = Params->GetStringField(TEXT("actor_class"));
		if (ActorClassName.IsEmpty())
		{
			return FMonolithActionResult::Error(TEXT("SpawnActorFromClass node requires 'actor_class'"));
		}

		UClass* ActorClass = FindFirstObject<UClass>(*ActorClassName, EFindFirstObjectOptions::NativeFirst);
		if (!ActorClass)
		{
			return FMonolithActionResult::Error(FString::Printf(
				TEXT("Actor class not found: %s"), *ActorClassName));
		}

		UK2Node_SpawnActorFromClass* SpawnNode = NewObject<UK2Node_SpawnActorFromClass>(Graph);
		SpawnNode->NodePosX = PosX;
		SpawnNode->NodePosY = PosY;
		Graph->AddNode(SpawnNode, true, false);
		SpawnNode->AllocateDefaultPins();

		// Set the class pin default
		UEdGraphPin* ClassPin = SpawnNode->GetClassPin();
		if (ClassPin)
		{
			ClassPin->DefaultObject = ActorClass;
		}

		NewNode = SpawnNode;
	}
	// ---- DynamicCast ----
	else if (NodeType == TEXT("DynamicCast"))
	{
		// Accept cast_class as the primary param; actor_class is the deprecated fallback
		FString CastClassName = Params->GetStringField(TEXT("cast_class"));
		if (CastClassName.IsEmpty())
		{
			CastClassName = Params->GetStringField(TEXT("actor_class"));
		}
		if (CastClassName.IsEmpty())
		{
			return FMonolithActionResult::Error(TEXT("DynamicCast node requires 'cast_class' (e.g. cast_class=MyPawn)"));
		}

		UClass* CastClass = FindFirstObject<UClass>(*CastClassName, EFindFirstObjectOptions::NativeFirst);
		if (!CastClass && !CastClassName.StartsWith(TEXT("A")))
			CastClass = FindFirstObject<UClass>(*FString::Printf(TEXT("A%s"), *CastClassName), EFindFirstObjectOptions::NativeFirst);
		if (!CastClass && !CastClassName.StartsWith(TEXT("U")))
			CastClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *CastClassName), EFindFirstObjectOptions::NativeFirst);
		if (!CastClass)
		{
			return FMonolithActionResult::Error(FString::Printf(
				TEXT("Class not found for DynamicCast: '%s'"), *CastClassName));
		}

		UK2Node_DynamicCast* CastNode = NewObject<UK2Node_DynamicCast>(Graph);
		CastNode->TargetType = CastClass;
		CastNode->NodePosX = PosX;
		CastNode->NodePosY = PosY;
		Graph->AddNode(CastNode, true, false);
		CastNode->AllocateDefaultPins();
		NewNode = CastNode;
	}
	// ---- Self ----
	else if (NodeType == TEXT("Self"))
	{
		UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
		SelfNode->NodePosX = PosX;
		SelfNode->NodePosY = PosY;
		Graph->AddNode(SelfNode, true, false);
		SelfNode->AllocateDefaultPins();
		NewNode = SelfNode;
	}
	// ---- Return (FunctionResult) ----
	else if (NodeType == TEXT("Return"))
	{
		// Return nodes only make sense inside function graphs
		bool bIsInFunctionGraph = false;
		for (const auto& FG : BP->FunctionGraphs)
		{
			if (FG == Graph)
			{
				bIsInFunctionGraph = true;
				break;
			}
		}
		if (!bIsInFunctionGraph)
		{
			return FMonolithActionResult::Error(TEXT("Return nodes can only be added to function graphs, not event graphs"));
		}
		UK2Node_FunctionResult* ReturnNode = NewObject<UK2Node_FunctionResult>(Graph);
		ReturnNode->NodePosX = PosX;
		ReturnNode->NodePosY = PosY;
		Graph->AddNode(ReturnNode, true, false);
		ReturnNode->AllocateDefaultPins();
		NewNode = ReturnNode;
	}
	else
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Unknown node_type: '%s'. Valid types: CallFunction, VariableGet, VariableSet, CustomEvent, Branch, Sequence, MacroInstance, SpawnActorFromClass, DynamicCast, Self, Return"), *NodeType));
	}

	if (!NewNode)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create node — NewObject returned null"));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Root = MonolithBlueprintInternal::SerializeNode(NewNode);
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("graph"), Graph->GetName());
	return FMonolithActionResult::Success(Root);
}

// ============================================================
//  remove_node
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleRemoveNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString NodeId = Params->GetStringField(TEXT("node_id"));
	if (NodeId.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: node_id"));
	}

	FString GraphName = Params->GetStringField(TEXT("graph_name"));
	UEdGraphNode* Node = MonolithBlueprintInternal::FindNodeById(BP, GraphName, NodeId);
	if (!Node)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	FBlueprintEditorUtils::RemoveNode(BP, Node, /*bDontRecompile=*/false);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("removed_node"), NodeId);
	Root->SetBoolField(TEXT("success"), true);
	return FMonolithActionResult::Success(Root);
}

// ============================================================
//  connect_pins
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleConnectPins(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString SourceNodeId = Params->GetStringField(TEXT("source_node"));
	FString SourcePinName = Params->GetStringField(TEXT("source_pin"));
	FString TargetNodeId = Params->GetStringField(TEXT("target_node"));
	FString TargetPinName = Params->GetStringField(TEXT("target_pin"));

	if (SourceNodeId.IsEmpty()) return FMonolithActionResult::Error(TEXT("Missing required parameter: source_node"));
	if (SourcePinName.IsEmpty()) return FMonolithActionResult::Error(TEXT("Missing required parameter: source_pin"));
	if (TargetNodeId.IsEmpty()) return FMonolithActionResult::Error(TEXT("Missing required parameter: target_node"));
	if (TargetPinName.IsEmpty()) return FMonolithActionResult::Error(TEXT("Missing required parameter: target_pin"));

	FString GraphName = Params->GetStringField(TEXT("graph_name"));

	UEdGraphNode* SrcNode = MonolithBlueprintInternal::FindNodeById(BP, GraphName, SourceNodeId);
	if (!SrcNode)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeId));
	}

	UEdGraphNode* TgtNode = MonolithBlueprintInternal::FindNodeById(BP, GraphName, TargetNodeId);
	if (!TgtNode)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeId));
	}

	FString SrcAvailPins;
	UEdGraphPin* SrcPin = MonolithBlueprintInternal::FindPinOnNode(SrcNode, SourcePinName, EGPD_MAX, &SrcAvailPins);
	if (!SrcPin)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Source pin '%s' not found on node '%s'. Available pins: %s"), *SourcePinName, *SourceNodeId, *SrcAvailPins));
	}

	FString TgtAvailPins;
	UEdGraphPin* TgtPin = MonolithBlueprintInternal::FindPinOnNode(TgtNode, TargetPinName, EGPD_MAX, &TgtAvailPins);
	if (!TgtPin)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Target pin '%s' not found on node '%s'. Available pins: %s"), *TargetPinName, *TargetNodeId, *TgtAvailPins));
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	// Check compatibility before attempting connection
	FPinConnectionResponse Response = Schema->CanCreateConnection(SrcPin, TgtPin);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Cannot connect pins: %s"), *Response.Message.ToString()));
	}

	// Track whether UE will insert an auto-conversion node
	bool bAutoConversion = (Response.Response == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE);

	bool bConnected = Schema->TryCreateConnection(SrcPin, TgtPin);
	if (!bConnected)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("TryCreateConnection failed for '%s.%s' -> '%s.%s'"),
			*SourceNodeId, *SourcePinName, *TargetNodeId, *TargetPinName));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("source_node"), SourceNodeId);
	Root->SetStringField(TEXT("source_pin"), SourcePinName);
	Root->SetStringField(TEXT("target_node"), TargetNodeId);
	Root->SetStringField(TEXT("target_pin"), TargetPinName);
	Root->SetBoolField(TEXT("success"), true);
	if (bAutoConversion)
	{
		Root->SetStringField(TEXT("warning"), TEXT("Connection required an auto-conversion node (types were not directly compatible)"));
	}
	return FMonolithActionResult::Success(Root);
}

// ============================================================
//  disconnect_pins
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleDisconnectPins(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString NodeId = Params->GetStringField(TEXT("node_id"));
	if (NodeId.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: node_id"));
	}

	FString PinName = Params->GetStringField(TEXT("pin_name"));
	if (PinName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: pin_name"));
	}

	FString GraphName = Params->GetStringField(TEXT("graph_name"));

	UEdGraphNode* Node = MonolithBlueprintInternal::FindNodeById(BP, GraphName, NodeId);
	if (!Node)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	FString AvailPins;
	UEdGraphPin* Pin = MonolithBlueprintInternal::FindPinOnNode(Node, PinName, EGPD_MAX, &AvailPins);
	if (!Pin)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Pin '%s' not found on node '%s'. Available pins: %s"), *PinName, *NodeId, *AvailPins));
	}

	FString TargetNodeId = Params->GetStringField(TEXT("target_node"));
	FString TargetPinName = Params->GetStringField(TEXT("target_pin"));

	if (TargetNodeId.IsEmpty())
	{
		// Break all connections on this pin
		Pin->BreakAllPinLinks(true);
	}
	else
	{
		if (TargetPinName.IsEmpty())
		{
			return FMonolithActionResult::Error(TEXT("'target_pin' is required when 'target_node' is specified"));
		}

		UEdGraphNode* TargetNode = MonolithBlueprintInternal::FindNodeById(BP, GraphName, TargetNodeId);
		if (!TargetNode)
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeId));
		}

		FString TgtAvailPins2;
		UEdGraphPin* TargetPin = MonolithBlueprintInternal::FindPinOnNode(TargetNode, TargetPinName, EGPD_MAX, &TgtAvailPins2);
		if (!TargetPin)
		{
			return FMonolithActionResult::Error(FString::Printf(
				TEXT("Target pin '%s' not found on node '%s'. Available pins: %s"), *TargetPinName, *TargetNodeId, *TgtAvailPins2));
		}

		Pin->BreakLinkTo(TargetPin);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("node_id"), NodeId);
	Root->SetStringField(TEXT("pin_name"), PinName);
	if (!TargetNodeId.IsEmpty())
	{
		Root->SetStringField(TEXT("target_node"), TargetNodeId);
		Root->SetStringField(TEXT("target_pin"), TargetPinName);
	}
	Root->SetBoolField(TEXT("success"), true);
	return FMonolithActionResult::Success(Root);
}

// ============================================================
//  set_pin_default
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleSetPinDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString NodeId = Params->GetStringField(TEXT("node_id"));
	if (NodeId.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: node_id"));
	}

	FString PinName = Params->GetStringField(TEXT("pin_name"));
	if (PinName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: pin_name"));
	}

	FString Value = Params->GetStringField(TEXT("value"));
	if (Value.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: value"));
	}

	FString GraphName = Params->GetStringField(TEXT("graph_name"));

	UEdGraphNode* Node = MonolithBlueprintInternal::FindNodeById(BP, GraphName, NodeId);
	if (!Node)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	FString SetPinAvailPins;
	UEdGraphPin* Pin = MonolithBlueprintInternal::FindPinOnNode(Node, PinName, EGPD_MAX, &SetPinAvailPins);
	if (!Pin)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Pin '%s' not found on node '%s'. Available pins: %s"), *PinName, *NodeId, *SetPinAvailPins));
	}

	if (Pin->Direction != EGPD_Input)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Pin '%s' is an output pin — only input pins can have default values"), *PinName));
	}

	if (Pin->LinkedTo.Num() > 0)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Pin '%s' has active connections — disconnect it first before setting a default value"), *PinName));
	}

	Pin->DefaultValue = Value;
	Node->PinDefaultValueChanged(Pin);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("node_id"), NodeId);
	Root->SetStringField(TEXT("pin_name"), PinName);
	Root->SetStringField(TEXT("value"), Value);
	Root->SetBoolField(TEXT("success"), true);
	return FMonolithActionResult::Success(Root);
}

// ============================================================
//  set_node_position
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleSetNodePosition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString NodeId = Params->GetStringField(TEXT("node_id"));
	if (NodeId.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: node_id"));
	}

	const TArray<TSharedPtr<FJsonValue>>* PosArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("position"), PosArray) || !PosArray || PosArray->Num() < 2)
	{
		return FMonolithActionResult::Error(TEXT("'position' must be an array of [x, y]"));
	}

	int32 PosX = (int32)(*PosArray)[0]->AsNumber();
	int32 PosY = (int32)(*PosArray)[1]->AsNumber();

	FString GraphName = Params->GetStringField(TEXT("graph_name"));

	UEdGraphNode* Node = MonolithBlueprintInternal::FindNodeById(BP, GraphName, NodeId);
	if (!Node)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	Node->NodePosX = PosX;
	Node->NodePosY = PosY;
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("node_id"), NodeId);

	TArray<TSharedPtr<FJsonValue>> OutPosArr;
	OutPosArr.Add(MakeShared<FJsonValueNumber>(PosX));
	OutPosArr.Add(MakeShared<FJsonValueNumber>(PosY));
	Root->SetArrayField(TEXT("position"), OutPosArr);

	Root->SetBoolField(TEXT("success"), true);
	return FMonolithActionResult::Success(Root);
}

// ============================================================
//  batch_execute
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleBatchExecute(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Parse operations — handle both EJson::Array (normal) and EJson::String (Claude Code quirk)
	TArray<TSharedPtr<FJsonValue>> Ops;
	TSharedPtr<FJsonValue> OpsField = Params->TryGetField(TEXT("operations"));
	if (!OpsField.IsValid())
	{
		return FMonolithActionResult::Error(TEXT("Missing required field: operations"));
	}
	if (OpsField->Type == EJson::Array)
	{
		Ops = OpsField->AsArray();
	}
	else if (OpsField->Type == EJson::String)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OpsField->AsString());
		if (!FJsonSerializer::Deserialize(Reader, Ops))
		{
			return FMonolithActionResult::Error(TEXT("Failed to parse operations string as JSON array"));
		}
	}
	else
	{
		return FMonolithActionResult::Error(TEXT("'operations' must be an array"));
	}

	bool bStopOnError = false;
	Params->TryGetBoolField(TEXT("stop_on_error"), bStopOnError);

	bool bCompileOnComplete = false;
	Params->TryGetBoolField(TEXT("compile_on_complete"), bCompileOnComplete);

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "BPBatchExec", "BP Batch Execute"));

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Ok = 0, Fail = 0;

	for (int32 i = 0; i < Ops.Num(); ++i)
	{
		TSharedPtr<FJsonObject> Op = Ops[i]->AsObject();
		TSharedRef<FJsonObject> RO = MakeShared<FJsonObject>();
		RO->SetNumberField(TEXT("index"), i);

		if (!Op.IsValid())
		{
			RO->SetStringField(TEXT("op"), TEXT("(invalid)"));
			RO->SetBoolField(TEXT("success"), false);
			RO->SetStringField(TEXT("error"), TEXT("Operation entry is not a valid JSON object"));
			Results.Add(MakeShared<FJsonValueObject>(RO));
			Fail++;
			if (bStopOnError) break;
			continue;
		}

		FString OpName;
		if (!Op->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
		{
			FString HintName;
			Op->TryGetStringField(TEXT("action"), HintName);
			FString Hint = HintName.IsEmpty()
				? TEXT("Each operation must have an \"op\" key with the action name, plus flat inline params (not nested under \"params\").")
				: FString::Printf(TEXT("Use \"op\" key, not \"action\". Found \"action\":\"%s\". Params must be flat inline, not nested."), *HintName);
			RO->SetStringField(TEXT("op"), TEXT("(missing)"));
			RO->SetBoolField(TEXT("success"), false);
			RO->SetStringField(TEXT("error"), Hint);
			Results.Add(MakeShared<FJsonValueObject>(RO));
			Fail++;
			if (bStopOnError) break;
			continue;
		}
		RO->SetStringField(TEXT("op"), OpName);

		// Build sub-params: inject asset_path then copy all op fields
		TSharedRef<FJsonObject> SubParams = MakeShared<FJsonObject>();
		SubParams->SetStringField(TEXT("asset_path"), AssetPath);
		for (auto& Pair : Op->Values)
		{
			SubParams->SetField(Pair.Key, Pair.Value);
		}

		FMonolithActionResult SubResult = FMonolithActionResult::Error(FString::Printf(TEXT("Unknown op: %s"), *OpName));

		// Node ops
		if      (OpName == TEXT("add_node"))               SubResult = HandleAddNode(SubParams);
		else if (OpName == TEXT("remove_node"))            SubResult = HandleRemoveNode(SubParams);
		else if (OpName == TEXT("connect_pins"))           SubResult = HandleConnectPins(SubParams);
		else if (OpName == TEXT("disconnect_pins"))        SubResult = HandleDisconnectPins(SubParams);
		else if (OpName == TEXT("set_pin_default"))        SubResult = HandleSetPinDefault(SubParams);
		else if (OpName == TEXT("set_node_position"))      SubResult = HandleSetNodePosition(SubParams);
		// Variable ops
		else if (OpName == TEXT("add_variable"))           SubResult = FMonolithBlueprintVariableActions::HandleAddVariable(SubParams);
		else if (OpName == TEXT("remove_variable"))        SubResult = FMonolithBlueprintVariableActions::HandleRemoveVariable(SubParams);
		else if (OpName == TEXT("rename_variable"))        SubResult = FMonolithBlueprintVariableActions::HandleRenameVariable(SubParams);
		else if (OpName == TEXT("set_variable_type"))      SubResult = FMonolithBlueprintVariableActions::HandleSetVariableType(SubParams);
		else if (OpName == TEXT("set_variable_defaults"))  SubResult = FMonolithBlueprintVariableActions::HandleSetVariableDefaults(SubParams);
		else if (OpName == TEXT("add_local_variable"))     SubResult = FMonolithBlueprintVariableActions::HandleAddLocalVariable(SubParams);
		else if (OpName == TEXT("remove_local_variable"))  SubResult = FMonolithBlueprintVariableActions::HandleRemoveLocalVariable(SubParams);
		// Component ops
		else if (OpName == TEXT("add_component"))          SubResult = FMonolithBlueprintComponentActions::HandleAddComponent(SubParams);
		else if (OpName == TEXT("remove_component"))       SubResult = FMonolithBlueprintComponentActions::HandleRemoveComponent(SubParams);
		else if (OpName == TEXT("rename_component"))       SubResult = FMonolithBlueprintComponentActions::HandleRenameComponent(SubParams);
		else if (OpName == TEXT("reparent_component"))     SubResult = FMonolithBlueprintComponentActions::HandleReparentComponent(SubParams);
		else if (OpName == TEXT("set_component_property")) SubResult = FMonolithBlueprintComponentActions::HandleSetComponentProperty(SubParams);
		else if (OpName == TEXT("duplicate_component"))    SubResult = FMonolithBlueprintComponentActions::HandleDuplicateComponent(SubParams);
		// Graph/interface ops
		else if (OpName == TEXT("add_function"))           SubResult = FMonolithBlueprintGraphActions::HandleAddFunction(SubParams);
		else if (OpName == TEXT("remove_function"))        SubResult = FMonolithBlueprintGraphActions::HandleRemoveFunction(SubParams);
		else if (OpName == TEXT("rename_function"))        SubResult = FMonolithBlueprintGraphActions::HandleRenameFunction(SubParams);
		else if (OpName == TEXT("add_macro"))              SubResult = FMonolithBlueprintGraphActions::HandleAddMacro(SubParams);
		else if (OpName == TEXT("add_event_dispatcher"))   SubResult = FMonolithBlueprintGraphActions::HandleAddEventDispatcher(SubParams);
		else if (OpName == TEXT("set_function_params"))    SubResult = FMonolithBlueprintGraphActions::HandleSetFunctionParams(SubParams);
		else if (OpName == TEXT("implement_interface"))        SubResult = FMonolithBlueprintGraphActions::HandleImplementInterface(SubParams);
		else if (OpName == TEXT("remove_interface"))           SubResult = FMonolithBlueprintGraphActions::HandleRemoveInterface(SubParams);
		// Wave 5 scaffolding ops
		else if (OpName == TEXT("scaffold_interface_implementation")) SubResult = FMonolithBlueprintGraphActions::HandleScaffoldInterfaceImplementation(SubParams);
		else if (OpName == TEXT("add_timeline"))               SubResult = HandleAddTimeline(SubParams);
		else if (OpName == TEXT("add_event_node"))             SubResult = HandleAddEventNode(SubParams);
		else if (OpName == TEXT("add_comment_node"))           SubResult = HandleAddCommentNode(SubParams);
		// Wave 7 advanced ops
		else if (OpName == TEXT("promote_pin_to_variable"))    SubResult = HandlePromotePinToVariable(SubParams);
		else if (OpName == TEXT("add_replicated_variable"))    SubResult = FMonolithBlueprintVariableActions::HandleAddReplicatedVariable(SubParams);
		// Phase 1 expansion (1E/1F — handlers added by parallel agent)
		else if (OpName == TEXT("save_asset"))                 SubResult = FMonolithBlueprintCompileActions::HandleSaveAsset(SubParams);
		else if (OpName == TEXT("remove_macro"))               SubResult = FMonolithBlueprintGraphActions::HandleRemoveMacro(SubParams);
		else if (OpName == TEXT("rename_macro"))               SubResult = FMonolithBlueprintGraphActions::HandleRenameMacro(SubParams);

		RO->SetBoolField(TEXT("success"), SubResult.bSuccess);
		if (!SubResult.bSuccess)
		{
			RO->SetStringField(TEXT("error"), SubResult.ErrorMessage);
		}
		if (SubResult.bSuccess && SubResult.Result.IsValid())
		{
			RO->SetObjectField(TEXT("data"), SubResult.Result);
		}

		Results.Add(MakeShared<FJsonValueObject>(RO));
		if (SubResult.bSuccess) Ok++; else Fail++;

		if (!SubResult.bSuccess && bStopOnError) break;
	}

	GEditor->EndTransaction();

	TSharedRef<FJsonObject> Final = MakeShared<FJsonObject>();
	Final->SetBoolField(TEXT("success"), Fail == 0);
	Final->SetNumberField(TEXT("total"), Ops.Num());
	Final->SetNumberField(TEXT("succeeded"), Ok);
	Final->SetNumberField(TEXT("failed"), Fail);
	Final->SetArrayField(TEXT("results"), Results);

	if (bCompileOnComplete)
	{
		TSharedRef<FJsonObject> CompileParams = MakeShared<FJsonObject>();
		CompileParams->SetStringField(TEXT("asset_path"), AssetPath);
		FMonolithActionResult CompileResult = FMonolithBlueprintCompileActions::HandleCompileBlueprint(CompileParams);
		Final->SetBoolField(TEXT("compile_success"), CompileResult.bSuccess);
		if (CompileResult.bSuccess && CompileResult.Result.IsValid())
		{
			Final->SetObjectField(TEXT("compile_result"), CompileResult.Result);
		}
		else if (!CompileResult.bSuccess)
		{
			Final->SetStringField(TEXT("compile_error"), CompileResult.ErrorMessage);
		}
	}

	return FMonolithActionResult::Success(Final);
}

// ============================================================
//  resolve_node
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleResolveNode(const TSharedPtr<FJsonObject>& Params)
{
	FString NodeType = Params->GetStringField(TEXT("node_type"));
	if (NodeType.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: node_type"));
	}

	// Apply same alias normalization as add_node (shared map from 1G)
	{
		FString Lower = NodeType.ToLower();
		const auto& Aliases = GetNodeAliases();
		if (const FNodeAlias* Alias = Aliases.Find(Lower))
		{
			NodeType = Alias->CanonicalType;
			// Merge default params for resolve_node too (e.g., function_name for IsValid alias)
			for (const auto& KV : Alias->DefaultParams)
			{
				if (!Params->HasField(KV.Key))
				{
					Params->SetStringField(KV.Key, KV.Value);
				}
			}
		}
	}

	TArray<FString> Warnings;

	// Create a transient Blueprint + graph so AllocateDefaultPins() can find an
	// owning Blueprint via the outer chain.  Without this, nodes like
	// UK2Node_CallFunction assert in FindBlueprintForNodeChecked().
	UBlueprint* TempBP = NewObject<UBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	TempBP->ParentClass = AActor::StaticClass();
	TempBP->GeneratedClass = AActor::StaticClass();
	TempBP->SkeletonGeneratedClass = AActor::StaticClass();
	UEdGraph* TempGraph = NewObject<UEdGraph>(TempBP, NAME_None, RF_Transient);
	TempGraph->Schema = UEdGraphSchema_K2::StaticClass();

	UEdGraphNode* Node = nullptr;

	if (NodeType == TEXT("CallFunction"))
	{
		FString FuncName = Params->GetStringField(TEXT("function_name"));
		if (FuncName.IsEmpty())
		{
			return FMonolithActionResult::Error(TEXT("CallFunction requires 'function_name'"));
		}

		FString TargetClassName = Params->GetStringField(TEXT("target_class"));

		TArray<FName> Candidates;
		Candidates.Add(FName(*FuncName));
		if (!FuncName.StartsWith(TEXT("K2_")))
		{
			Candidates.Add(FName(*FString::Printf(TEXT("K2_%s"), *FuncName)));
		}

		UFunction* Func = nullptr;
		if (!TargetClassName.IsEmpty())
		{
			UClass* TargetClass = FindFirstObject<UClass>(*TargetClassName, EFindFirstObjectOptions::NativeFirst);
			if (!TargetClass && !TargetClassName.StartsWith(TEXT("U")))
				TargetClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *TargetClassName), EFindFirstObjectOptions::NativeFirst);
			if (!TargetClass && TargetClassName.StartsWith(TEXT("U")))
				TargetClass = FindFirstObject<UClass>(*TargetClassName.Mid(1), EFindFirstObjectOptions::NativeFirst);

			if (TargetClass)
			{
				for (const FName& C : Candidates)
				{
					Func = TargetClass->FindFunctionByName(C);
					if (Func) break;
				}
			}
			if (!Func)
			{
				return FMonolithActionResult::Error(FString::Printf(
					TEXT("Function '%s' not found on class '%s'"), *FuncName, *TargetClassName));
			}
		}
		else
		{
			for (TObjectIterator<UClass> It; It && !Func; ++It)
			{
				for (const FName& C : Candidates)
				{
					Func = It->FindFunctionByName(C);
					if (Func) break;
				}
			}
			if (!Func)
			{
				return FMonolithActionResult::Error(FString::Printf(
					TEXT("Function '%s' not found in any loaded class"), *FuncName));
			}
		}

		UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(TempGraph);
		CallNode->SetFromFunction(Func);
		CallNode->AllocateDefaultPins();
		Node = CallNode;
	}
	else if (NodeType == TEXT("VariableGet"))
	{
		// For a dry-run VariableGet, we use a wildcard self-member reference.
		// If variable_name is provided it's recorded in the response but the pin
		// layout is identical regardless — VariableGet always has one output data pin.
		UK2Node_VariableGet* VarNode = NewObject<UK2Node_VariableGet>(TempGraph);
		FString VarName = Params->GetStringField(TEXT("variable_name"));
		if (VarName.IsEmpty()) VarName = TEXT("Variable");
		VarNode->VariableReference.SetSelfMember(FName(*VarName));
		VarNode->AllocateDefaultPins();
		Node = VarNode;
		Warnings.Add(TEXT("VariableGet pin types reflect a wildcard — actual type depends on the specific variable in the target Blueprint"));
	}
	else if (NodeType == TEXT("VariableSet"))
	{
		UK2Node_VariableSet* VarNode = NewObject<UK2Node_VariableSet>(TempGraph);
		FString VarName = Params->GetStringField(TEXT("variable_name"));
		if (VarName.IsEmpty()) VarName = TEXT("Variable");
		VarNode->VariableReference.SetSelfMember(FName(*VarName));
		VarNode->AllocateDefaultPins();
		Node = VarNode;
		Warnings.Add(TEXT("VariableSet pin types reflect a wildcard — actual type depends on the specific variable in the target Blueprint"));
	}
	else if (NodeType == TEXT("Branch"))
	{
		UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(TempGraph);
		BranchNode->AllocateDefaultPins();
		Node = BranchNode;
	}
	else if (NodeType == TEXT("CustomEvent"))
	{
		UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(TempGraph);
		FString EventName = Params->GetStringField(TEXT("event_name"));
		if (EventName.IsEmpty()) EventName = TEXT("MyEvent");
		EventNode->CustomFunctionName = FName(*EventName);
		EventNode->AllocateDefaultPins();
		Node = EventNode;
	}
	else if (NodeType == TEXT("Sequence"))
	{
		UK2Node_ExecutionSequence* SeqNode = NewObject<UK2Node_ExecutionSequence>(TempGraph);
		SeqNode->AllocateDefaultPins();
		Node = SeqNode;
	}
	else
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Unsupported node_type for resolve_node: '%s'. Supported: CallFunction, VariableGet, VariableSet, Branch, CustomEvent, Sequence"), *NodeType));
	}

	if (!Node)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create transient node"));
	}

	// Serialize pins
	TArray<TSharedPtr<FJsonValue>> PinsArr;
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden) continue;

		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		PinObj->SetStringField(TEXT("type"),
			MonolithBlueprintInternal::ContainerPrefix(Pin->PinType) +
			MonolithBlueprintInternal::PinTypeToString(Pin->PinType));
		PinObj->SetBoolField(TEXT("is_exec"), Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);

		if (!Pin->DefaultValue.IsEmpty())
		{
			PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		}
		if (Pin->DefaultObject)
		{
			PinObj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
		}

		PinsArr.Add(MakeShared<FJsonValueObject>(PinObj));
	}

	// Determine resolved_function for CallFunction nodes
	FString ResolvedFunction;
	FString ResolvedClass;
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		ResolvedFunction = CallNode->FunctionReference.GetMemberName().ToString();
		if (UClass* OwnerClass = CallNode->FunctionReference.GetMemberParentClass())
		{
			ResolvedClass = OwnerClass->GetName();
		}
	}

	// Build response
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("resolved_type"), NodeType);
	Root->SetStringField(TEXT("resolved_class"), Node->GetClass()->GetName());
	if (!ResolvedFunction.IsEmpty())
	{
		Root->SetStringField(TEXT("resolved_function"), ResolvedFunction);
	}
	if (!ResolvedClass.IsEmpty())
	{
		Root->SetStringField(TEXT("resolved_function_class"), ResolvedClass);
	}
	Root->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	Root->SetArrayField(TEXT("pins"), PinsArr);
	Root->SetNumberField(TEXT("pin_count"), PinsArr.Num());

	// Warnings
	TArray<TSharedPtr<FJsonValue>> WarnArr;
	for (const FString& W : Warnings)
	{
		WarnArr.Add(MakeShared<FJsonValueString>(W));
	}
	Root->SetArrayField(TEXT("warnings"), WarnArr);

	// Mark transient objects for GC
	TempGraph->MarkAsGarbage();
	TempBP->MarkAsGarbage();

	return FMonolithActionResult::Success(Root);
}

// ============================================================
//  add_nodes_bulk
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleAddNodesBulk(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Parse nodes array — handle both EJson::Array (normal) and EJson::String (Claude Code quirk)
	TArray<TSharedPtr<FJsonValue>> NodesArr;
	TSharedPtr<FJsonValue> NodesField = Params->TryGetField(TEXT("nodes"));
	if (!NodesField.IsValid())
	{
		return FMonolithActionResult::Error(TEXT("Missing required field: nodes"));
	}
	if (NodesField->Type == EJson::Array)
	{
		NodesArr = NodesField->AsArray();
	}
	else if (NodesField->Type == EJson::String)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodesField->AsString());
		if (!FJsonSerializer::Deserialize(Reader, NodesArr))
		{
			return FMonolithActionResult::Error(TEXT("Failed to parse nodes string as JSON array"));
		}
	}
	else
	{
		return FMonolithActionResult::Error(TEXT("'nodes' must be an array"));
	}

	bool bAutoLayout = false;
	Params->TryGetBoolField(TEXT("auto_layout"), bAutoLayout);

	FString SharedGraphName = Params->GetStringField(TEXT("graph_name"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "BPAddNodesBulk", "BP Add Nodes Bulk"));

	TArray<TSharedPtr<FJsonValue>> CreatedArr;
	int32 Count = 0;

	for (int32 i = 0; i < NodesArr.Num(); ++i)
	{
		TSharedPtr<FJsonObject> Entry = NodesArr[i]->AsObject();
		if (!Entry.IsValid())
		{
			// Skip invalid entries silently — can't report without a temp_id
			continue;
		}

		FString TempId = Entry->GetStringField(TEXT("temp_id"));

		// Build sub-params: inject asset_path and graph_name, then copy all entry fields
		TSharedRef<FJsonObject> SubParams = MakeShared<FJsonObject>();
		SubParams->SetStringField(TEXT("asset_path"), AssetPath);
		if (!SharedGraphName.IsEmpty())
		{
			SubParams->SetStringField(TEXT("graph_name"), SharedGraphName);
		}
		for (const auto& Pair : Entry->Values)
		{
			SubParams->SetField(Pair.Key, Pair.Value);
		}

		// Apply auto_layout position if the entry doesn't already specify one and auto_layout is on
		if (bAutoLayout && !Entry->HasField(TEXT("position")))
		{
			int32 Col = i % 5;
			int32 Row = i / 5;
			TArray<TSharedPtr<FJsonValue>> PosArr;
			PosArr.Add(MakeShared<FJsonValueNumber>(Col * 200));
			PosArr.Add(MakeShared<FJsonValueNumber>(Row * 100));
			SubParams->SetArrayField(TEXT("position"), PosArr);
		}

		FMonolithActionResult Result = HandleAddNode(SubParams);

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("temp_id"), TempId);
		Out->SetBoolField(TEXT("success"), Result.bSuccess);

		if (Result.bSuccess && Result.Result.IsValid())
		{
			// SerializeNode uses "id" (not "node_id") — matches get_graph_data format
			FString NodeId = Result.Result->GetStringField(TEXT("id"));
			FString NodeClass = Result.Result->GetStringField(TEXT("class"));
			FString NodeTitle = Result.Result->GetStringField(TEXT("title"));

			Out->SetStringField(TEXT("node_id"), NodeId);
			Out->SetStringField(TEXT("class"),   NodeClass);
			Out->SetStringField(TEXT("title"),   NodeTitle);

			// Include position if available (from auto_layout or user-specified)
			const TArray<TSharedPtr<FJsonValue>>* PosArr = nullptr;
			if (SubParams->TryGetArrayField(TEXT("position"), PosArr) && PosArr && PosArr->Num() >= 2)
			{
				TArray<TSharedPtr<FJsonValue>> PosOut;
				PosOut.Add((*PosArr)[0]);
				PosOut.Add((*PosArr)[1]);
				Out->SetArrayField(TEXT("position"), PosOut);
			}

			Count++;
		}
		else if (!Result.bSuccess)
		{
			Out->SetStringField(TEXT("error"), Result.ErrorMessage);
		}

		CreatedArr.Add(MakeShared<FJsonValueObject>(Out));
	}

	GEditor->EndTransaction();

	TSharedRef<FJsonObject> Final = MakeShared<FJsonObject>();
	Final->SetArrayField(TEXT("nodes_created"), CreatedArr);
	Final->SetNumberField(TEXT("count"), Count);

	return FMonolithActionResult::Success(Final);
}

// ============================================================
//  connect_pins_bulk
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleConnectPinsBulk(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Parse connections array — handle both EJson::Array and EJson::String (Claude Code quirk)
	TArray<TSharedPtr<FJsonValue>> ConnArr;
	TSharedPtr<FJsonValue> ConnField = Params->TryGetField(TEXT("connections"));
	if (!ConnField.IsValid())
	{
		return FMonolithActionResult::Error(TEXT("Missing required field: connections"));
	}
	if (ConnField->Type == EJson::Array)
	{
		ConnArr = ConnField->AsArray();
	}
	else if (ConnField->Type == EJson::String)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConnField->AsString());
		if (!FJsonSerializer::Deserialize(Reader, ConnArr))
		{
			return FMonolithActionResult::Error(TEXT("Failed to parse connections string as JSON array"));
		}
	}
	else
	{
		return FMonolithActionResult::Error(TEXT("'connections' must be an array"));
	}

	FString SharedGraphName = Params->GetStringField(TEXT("graph_name"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "BPConnectPinsBulk", "BP Connect Pins Bulk"));

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Connected = 0, Failed = 0;

	for (int32 i = 0; i < ConnArr.Num(); ++i)
	{
		TSharedPtr<FJsonObject> Entry = ConnArr[i]->AsObject();

		TSharedRef<FJsonObject> RO = MakeShared<FJsonObject>();
		RO->SetNumberField(TEXT("index"), i);

		if (!Entry.IsValid())
		{
			RO->SetBoolField(TEXT("success"), false);
			RO->SetStringField(TEXT("error"), TEXT("Connection entry is not a valid JSON object"));
			Results.Add(MakeShared<FJsonValueObject>(RO));
			Failed++;
			continue;
		}

		// Build sub-params
		TSharedRef<FJsonObject> SubParams = MakeShared<FJsonObject>();
		SubParams->SetStringField(TEXT("asset_path"), AssetPath);
		if (!SharedGraphName.IsEmpty())
		{
			SubParams->SetStringField(TEXT("graph_name"), SharedGraphName);
		}
		for (const auto& Pair : Entry->Values)
		{
			SubParams->SetField(Pair.Key, Pair.Value);
		}

		FMonolithActionResult Result = HandleConnectPins(SubParams);

		RO->SetBoolField(TEXT("success"), Result.bSuccess);
		if (!Result.bSuccess)
		{
			RO->SetStringField(TEXT("error"), Result.ErrorMessage);
			Failed++;
		}
		else
		{
			Connected++;
		}

		Results.Add(MakeShared<FJsonValueObject>(RO));
	}

	GEditor->EndTransaction();

	TSharedRef<FJsonObject> Final = MakeShared<FJsonObject>();
	Final->SetNumberField(TEXT("connected"), Connected);
	Final->SetNumberField(TEXT("failed"),    Failed);
	Final->SetArrayField(TEXT("results"),    Results);

	return FMonolithActionResult::Success(Final);
}

// ============================================================
//  set_pin_defaults_bulk
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleSetPinDefaultsBulk(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Parse defaults array — handle both EJson::Array and EJson::String (Claude Code quirk)
	TArray<TSharedPtr<FJsonValue>> DefaultsArr;
	TSharedPtr<FJsonValue> DefaultsField = Params->TryGetField(TEXT("defaults"));
	if (!DefaultsField.IsValid())
	{
		return FMonolithActionResult::Error(TEXT("Missing required field: defaults"));
	}
	if (DefaultsField->Type == EJson::Array)
	{
		DefaultsArr = DefaultsField->AsArray();
	}
	else if (DefaultsField->Type == EJson::String)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DefaultsField->AsString());
		if (!FJsonSerializer::Deserialize(Reader, DefaultsArr))
		{
			return FMonolithActionResult::Error(TEXT("Failed to parse defaults string as JSON array"));
		}
	}
	else
	{
		return FMonolithActionResult::Error(TEXT("'defaults' must be an array"));
	}

	FString SharedGraphName = Params->GetStringField(TEXT("graph_name"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "BPSetPinDefaultsBulk", "BP Set Pin Defaults Bulk"));

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Set = 0, Failed = 0;

	for (int32 i = 0; i < DefaultsArr.Num(); ++i)
	{
		TSharedPtr<FJsonObject> Entry = DefaultsArr[i]->AsObject();

		TSharedRef<FJsonObject> RO = MakeShared<FJsonObject>();
		RO->SetNumberField(TEXT("index"), i);

		if (!Entry.IsValid())
		{
			RO->SetBoolField(TEXT("success"), false);
			RO->SetStringField(TEXT("error"), TEXT("Default entry is not a valid JSON object"));
			Results.Add(MakeShared<FJsonValueObject>(RO));
			Failed++;
			continue;
		}

		// Build sub-params
		TSharedRef<FJsonObject> SubParams = MakeShared<FJsonObject>();
		SubParams->SetStringField(TEXT("asset_path"), AssetPath);
		if (!SharedGraphName.IsEmpty())
		{
			SubParams->SetStringField(TEXT("graph_name"), SharedGraphName);
		}
		for (const auto& Pair : Entry->Values)
		{
			SubParams->SetField(Pair.Key, Pair.Value);
		}

		FMonolithActionResult Result = HandleSetPinDefault(SubParams);

		RO->SetBoolField(TEXT("success"), Result.bSuccess);
		if (!Result.bSuccess)
		{
			RO->SetStringField(TEXT("error"), Result.ErrorMessage);
			Failed++;
		}
		else
		{
			Set++;
		}

		Results.Add(MakeShared<FJsonValueObject>(RO));
	}

	GEditor->EndTransaction();

	TSharedRef<FJsonObject> Final = MakeShared<FJsonObject>();
	Final->SetNumberField(TEXT("set"),     Set);
	Final->SetNumberField(TEXT("failed"),  Failed);
	Final->SetArrayField(TEXT("results"),  Results);

	return FMonolithActionResult::Success(Final);
}

// ============================================================
//  add_timeline
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleAddTimeline(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Timelines are only supported in actor-based blueprints
	if (!FBlueprintEditorUtils::DoesSupportTimelines(BP))
	{
		return FMonolithActionResult::Error(TEXT("This Blueprint type does not support timelines (must be Actor-based)"));
	}

	// Resolve or find the target graph — must be an event graph (ubergraph page)
	FString GraphName = Params->GetStringField(TEXT("graph_name"));
	UEdGraph* Graph = nullptr;

	if (GraphName.IsEmpty())
	{
		// Default to first ubergraph page
		if (BP->UbergraphPages.Num() > 0)
		{
			Graph = BP->UbergraphPages[0];
		}
	}
	else
	{
		// Find by name in ubergraph pages only
		for (UEdGraph* G : BP->UbergraphPages)
		{
			if (G && G->GetName() == GraphName)
			{
				Graph = G;
				break;
			}
		}
	}

	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Event graph not found: '%s'. Timeline nodes can only be placed in event graphs (ubergraph pages), not function graphs."),
			GraphName.IsEmpty() ? TEXT("EventGraph") : *GraphName));
	}

	// Resolve timeline variable name — generate unique if not provided
	FString TimelineVarName = Params->GetStringField(TEXT("timeline_name"));
	if (TimelineVarName.IsEmpty())
	{
		TimelineVarName = FBlueprintEditorUtils::FindUniqueTimelineName(BP).ToString();
	}
	else
	{
		// Validate the provided name is unique
		FName DesiredName(*TimelineVarName);
		for (const UTimelineTemplate* Existing : BP->Timelines)
		{
			if (Existing && Existing->GetVariableName() == DesiredName)
			{
				return FMonolithActionResult::Error(FString::Printf(
					TEXT("A timeline named '%s' already exists in this Blueprint"), *TimelineVarName));
			}
		}
	}

	// Parse options
	bool bAutoPlay = false;
	bool bLoop = false;
	Params->TryGetBoolField(TEXT("auto_play"), bAutoPlay);
	Params->TryGetBoolField(TEXT("loop"), bLoop);

	int32 PosX = 0;
	int32 PosY = 0;
	const TArray<TSharedPtr<FJsonValue>>* PosArray = nullptr;
	if (Params->TryGetArrayField(TEXT("position"), PosArray) && PosArray && PosArray->Num() >= 2)
	{
		PosX = (int32)(*PosArray)[0]->AsNumber();
		PosY = (int32)(*PosArray)[1]->AsNumber();
	}

	// Step 1: Create the UTimelineTemplate (the data container)
	UTimelineTemplate* Template = FBlueprintEditorUtils::AddNewTimeline(BP, FName(*TimelineVarName));
	if (!Template)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("AddNewTimeline failed for name '%s'"), *TimelineVarName));
	}

	// Verify template is in BP->Timelines
	if (!BP->Timelines.Contains(Template))
	{
		return FMonolithActionResult::Error(TEXT("Timeline template was created but not found in BP->Timelines — aborting"));
	}

	// Step 2: Create the UK2Node_Timeline graph node
	UK2Node_Timeline* TimelineNode = NewObject<UK2Node_Timeline>(Graph);
	if (!TimelineNode)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create UK2Node_Timeline node object"));
	}

	// Step 3: Wire the node to the template via name and GUID
	TimelineNode->TimelineName = Template->GetVariableName();
	TimelineNode->TimelineGuid = Template->TimelineGuid;
	TimelineNode->bAutoPlay = bAutoPlay;
	TimelineNode->bLoop = bLoop;

	// Set flags on the template too — runtime reads from template, not node
	Template->Modify();
	Template->bAutoPlay = bAutoPlay;
	Template->bLoop = bLoop;
	TimelineNode->NodePosX = PosX;
	TimelineNode->NodePosY = PosY;

	// Step 4: GUID validation — critical. Silent failure if wrong.
	if (TimelineNode->TimelineGuid != Template->TimelineGuid)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("GUID linkage mismatch: node GUID '%s' != template GUID '%s'. This would cause silent compile errors."),
			*TimelineNode->TimelineGuid.ToString(),
			*Template->TimelineGuid.ToString()));
	}
	if (TimelineNode->TimelineName != Template->GetVariableName())
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Name linkage mismatch: node name '%s' != template name '%s'."),
			*TimelineNode->TimelineName.ToString(),
			*Template->GetVariableName().ToString()));
	}

	// Step 5: Add to graph and allocate pins
	Graph->AddNode(TimelineNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);
	TimelineNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	// Serialize pins for the response
	TArray<TSharedPtr<FJsonValue>> PinsArr;
	for (const UEdGraphPin* Pin : TimelineNode->Pins)
	{
		if (!Pin || Pin->bHidden) continue;
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		PinObj->SetBoolField(TEXT("is_exec"), Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
		PinsArr.Add(MakeShared<FJsonValueObject>(PinObj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("node_id"), TimelineNode->GetName());
	Root->SetStringField(TEXT("timeline_name"), TimelineNode->TimelineName.ToString());
	Root->SetStringField(TEXT("timeline_guid"), TimelineNode->TimelineGuid.ToString());
	Root->SetBoolField(TEXT("auto_play"), bAutoPlay);
	Root->SetBoolField(TEXT("loop"), bLoop);
	Root->SetArrayField(TEXT("pins"), PinsArr);
	Root->SetStringField(TEXT("graph"), Graph->GetName());
	return FMonolithActionResult::Success(Root);
}

// ============================================================
//  add_event_node
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleAddEventNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString EventName = Params->GetStringField(TEXT("event_name"));
	if (EventName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: event_name"));
	}

	// Resolve graph — must be an event graph
	FString GraphName = Params->GetStringField(TEXT("graph_name"));
	UEdGraph* Graph = nullptr;

	if (GraphName.IsEmpty())
	{
		if (BP->UbergraphPages.Num() > 0)
		{
			Graph = BP->UbergraphPages[0];
		}
	}
	else
	{
		for (UEdGraph* G : BP->UbergraphPages)
		{
			if (G && G->GetName() == GraphName)
			{
				Graph = G;
				break;
			}
		}
	}

	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Event graph not found: '%s'. Event nodes can only be placed in event graphs (ubergraph pages)."),
			GraphName.IsEmpty() ? TEXT("EventGraph") : *GraphName));
	}

	// Parse position
	int32 PosX = 0;
	int32 PosY = 0;
	const TArray<TSharedPtr<FJsonValue>>* PosArray = nullptr;
	if (Params->TryGetArrayField(TEXT("position"), PosArray) && PosArray && PosArray->Num() >= 2)
	{
		PosX = (int32)(*PosArray)[0]->AsNumber();
		PosY = (int32)(*PosArray)[1]->AsNumber();
	}

	// Alias table: friendly names -> actual UE function names on AActor (or other classes)
	static const TMap<FString, FString> EventAliases = {
		{TEXT("beginplay"),         TEXT("ReceiveBeginPlay")},
		{TEXT("begin_play"),        TEXT("ReceiveBeginPlay")},
		{TEXT("tick"),              TEXT("ReceiveTick")},
		{TEXT("receive_tick"),      TEXT("ReceiveTick")},
		{TEXT("endplay"),           TEXT("ReceiveEndPlay")},
		{TEXT("end_play"),          TEXT("ReceiveEndPlay")},
		{TEXT("beginoverlap"),      TEXT("ReceiveActorBeginOverlap")},
		{TEXT("begin_overlap"),     TEXT("ReceiveActorBeginOverlap")},
		{TEXT("endoverlap"),        TEXT("ReceiveActorEndOverlap")},
		{TEXT("end_overlap"),       TEXT("ReceiveActorEndOverlap")},
		{TEXT("hit"),               TEXT("ReceiveHit")},
		{TEXT("destroyed"),         TEXT("ReceiveDestroyed")},
		{TEXT("anydamage"),         TEXT("ReceiveAnyDamage")},
		{TEXT("any_damage"),        TEXT("ReceiveAnyDamage")},
		{TEXT("pointdamage"),       TEXT("ReceivePointDamage")},
		{TEXT("point_damage"),      TEXT("ReceivePointDamage")},
		{TEXT("radialdamage"),      TEXT("ReceiveRadialDamage")},
		{TEXT("radial_damage"),     TEXT("ReceiveRadialDamage")},
	};

	FString Lower = EventName.ToLower();
	FString ResolvedEventName = EventName; // Use as-is by default
	if (const FString* Canonical = EventAliases.Find(Lower))
	{
		ResolvedEventName = *Canonical;
	}

	// Try to find the declaring class by walking the inheritance chain
	// This is needed for SetExternalMember — it must be the class that DECLARES the function
	UClass* DeclaringClass = nullptr;
	UFunction* EventFunc = nullptr;

	FName EventFName(*ResolvedEventName);
	UClass* ParentClass = BP->ParentClass;
	if (ParentClass)
	{
		// Walk up the chain to find the class that first declares this function
		for (UClass* TestClass = ParentClass; TestClass; TestClass = TestClass->GetSuperClass())
		{
			UFunction* TestFunc = TestClass->FindFunctionByName(EventFName, EIncludeSuperFlag::ExcludeSuper);
			if (TestFunc)
			{
				DeclaringClass = TestClass;
				EventFunc = TestFunc;
				// Keep walking up — we want the topmost class that declares it
			}
		}
	}

	// If we found a native event in the inheritance chain, create an override event node
	if (DeclaringClass && EventFunc)
	{
		// Check if an override already exists for this function
		UK2Node_Event* ExistingOverride = FBlueprintEditorUtils::FindOverrideForFunction(BP, DeclaringClass, EventFName);
		if (ExistingOverride)
		{
			return FMonolithActionResult::Error(FString::Printf(
				TEXT("Override event '%s' already exists in this Blueprint (node: %s)"),
				*ResolvedEventName, *ExistingOverride->GetName()));
		}

		UK2Node_Event* EventNode = NewObject<UK2Node_Event>(Graph);
		EventNode->EventReference.SetExternalMember(EventFName, DeclaringClass);
		EventNode->bOverrideFunction = true;
		EventNode->NodePosX = PosX;
		EventNode->NodePosY = PosY;
		Graph->AddNode(EventNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);
		EventNode->AllocateDefaultPins();

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

		TSharedPtr<FJsonObject> Root = MonolithBlueprintInternal::SerializeNode(EventNode);
		Root->SetStringField(TEXT("event_name"), ResolvedEventName);
		Root->SetBoolField(TEXT("is_override"), true);
		Root->SetStringField(TEXT("class"), DeclaringClass->GetName());
		Root->SetStringField(TEXT("graph"), Graph->GetName());
		return FMonolithActionResult::Success(Root);
	}
	else
	{
		// No native function found — treat as a custom event
		// Check for duplicate custom event names first
		if (MonolithBlueprintInternal::HasCustomEventNamed(BP, FName(*EventName)))
		{
			return FMonolithActionResult::Error(FString::Printf(
				TEXT("A custom event named '%s' already exists in this Blueprint"), *EventName));
		}

		UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(Graph);
		EventNode->CustomFunctionName = FName(*EventName);
		EventNode->NodePosX = PosX;
		EventNode->NodePosY = PosY;
		Graph->AddNode(EventNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);
		EventNode->AllocateDefaultPins();

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

		TSharedPtr<FJsonObject> Root = MonolithBlueprintInternal::SerializeNode(EventNode);
		Root->SetStringField(TEXT("event_name"), EventName);
		Root->SetBoolField(TEXT("is_override"), false);
		Root->SetStringField(TEXT("class"), TEXT("CustomEvent"));
		Root->SetStringField(TEXT("graph"), Graph->GetName());
		return FMonolithActionResult::Success(Root);
	}
}

// ============================================================
//  add_comment_node
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandleAddCommentNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString CommentText = Params->GetStringField(TEXT("text"));
	if (CommentText.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: text"));
	}

	FString GraphName = Params->GetStringField(TEXT("graph_name"));
	UEdGraph* Graph = MonolithBlueprintInternal::FindGraphByName(BP, GraphName);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Graph not found: %s"), GraphName.IsEmpty() ? TEXT("EventGraph") : *GraphName));
	}

	// Parse color — default yellow (semi-transparent)
	double R = 1.0, G = 1.0, B = 0.0, A = 0.6;
	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (Params->TryGetObjectField(TEXT("color"), ColorObj) && ColorObj)
	{
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		(*ColorObj)->TryGetNumberField(TEXT("a"), A);
	}

	double FontSizeD = 18.0;
	Params->TryGetNumberField(TEXT("font_size"), FontSizeD);
	int32 FontSize = (int32)FontSizeD;

	// Parse position and dimensions defaults
	int32 PosX = 0;
	int32 PosY = 0;
	double WidthD = 400.0, HeightD = 200.0;
	int32 Width = 400;
	int32 Height = 200;

	const TArray<TSharedPtr<FJsonValue>>* PosArray = nullptr;
	if (Params->TryGetArrayField(TEXT("position"), PosArray) && PosArray && PosArray->Num() >= 2)
	{
		PosX = (int32)(*PosArray)[0]->AsNumber();
		PosY = (int32)(*PosArray)[1]->AsNumber();
	}

	if (Params->TryGetNumberField(TEXT("width"), WidthD))  Width  = (int32)WidthD;
	if (Params->TryGetNumberField(TEXT("height"), HeightD)) Height = (int32)HeightD;

	// If node_ids provided, compute bounding rect from those nodes
	const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
	bool bAutoSized = false;
	if (Params->TryGetArrayField(TEXT("node_ids"), NodeIdsArray) && NodeIdsArray && NodeIdsArray->Num() > 0)
	{
		// Estimated node dimensions (no runtime widget dimensions available in editor backend)
		constexpr int32 EstNodeW = 200;
		constexpr int32 EstNodeH = 100;
		constexpr int32 Padding = 50;

		int32 MinX = INT_MAX, MinY = INT_MAX, MaxX = INT_MIN, MaxY = INT_MIN;

		for (const TSharedPtr<FJsonValue>& IdVal : *NodeIdsArray)
		{
			FString NodeId = IdVal->AsString();
			if (NodeId.IsEmpty()) continue;

			UEdGraphNode* Node = MonolithBlueprintInternal::FindNodeById(BP, GraphName, NodeId);
			if (!Node) continue;

			MinX = FMath::Min(MinX, Node->NodePosX);
			MinY = FMath::Min(MinY, Node->NodePosY);
			MaxX = FMath::Max(MaxX, Node->NodePosX + EstNodeW);
			MaxY = FMath::Max(MaxY, Node->NodePosY + EstNodeH);
		}

		if (MinX != INT_MAX)
		{
			PosX   = MinX - Padding;
			PosY   = MinY - Padding - 30; // extra space for comment header
			Width  = (MaxX - MinX) + Padding * 2;
			Height = (MaxY - MinY) + Padding * 2 + 30;
			bAutoSized = true;
		}
	}

	// Create the comment node
	UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
	CommentNode->NodeComment = CommentText;
	CommentNode->CommentColor = FLinearColor(R, G, B, A);
	CommentNode->FontSize = FontSize;
	CommentNode->NodePosX = PosX;
	CommentNode->NodePosY = PosY;
	CommentNode->NodeWidth = Width;
	CommentNode->NodeHeight = Height;

	if (bAutoSized)
	{
		CommentNode->MoveMode = ECommentBoxMode::GroupMovement;
	}

	Graph->AddNode(CommentNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("node_id"), CommentNode->GetName());
	Root->SetStringField(TEXT("text"), CommentText);

	TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
	Bounds->SetNumberField(TEXT("x"), PosX);
	Bounds->SetNumberField(TEXT("y"), PosY);
	Bounds->SetNumberField(TEXT("w"), Width);
	Bounds->SetNumberField(TEXT("h"), Height);
	Root->SetObjectField(TEXT("bounds"), Bounds);
	Root->SetStringField(TEXT("graph"), Graph->GetName());

	return FMonolithActionResult::Success(Root);
}

// ============================================================
//  promote_pin_to_variable  (Wave 7)
// ============================================================

FMonolithActionResult FMonolithBlueprintNodeActions::HandlePromotePinToVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString NodeId = Params->GetStringField(TEXT("node_id"));
	if (NodeId.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: node_id"));
	}

	FString PinName = Params->GetStringField(TEXT("pin_name"));
	if (PinName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: pin_name"));
	}

	FString GraphName = Params->GetStringField(TEXT("graph_name"));

	// Find the node
	UEdGraphNode* SourceNode = MonolithBlueprintInternal::FindNodeById(BP, GraphName, NodeId);
	if (!SourceNode)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	// Find the pin
	FString PromoteAvailPins;
	UEdGraphPin* Pin = MonolithBlueprintInternal::FindPinOnNode(SourceNode, PinName, EGPD_MAX, &PromoteAvailPins);
	if (!Pin)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Pin '%s' not found on node '%s'. Available pins: %s"), *PinName, *NodeId, *PromoteAvailPins));
	}

	// Validate: not exec
	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return FMonolithActionResult::Error(TEXT("Cannot promote execution (exec) pins to variables"));
	}

	// Validate: not wildcard
	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
	{
		return FMonolithActionResult::Error(TEXT("Cannot promote wildcard pins to variables — resolve the type first"));
	}

	// Validate: scalar types only (no containers)
	if (Pin->PinType.ContainerType != EPinContainerType::None)
	{
		return FMonolithActionResult::Error(
			TEXT("Container types (Array, Map, Set) are not yet supported by promote_pin_to_variable. "
			     "Use add_variable + manual wiring instead."));
	}

	// Determine variable name: use provided or default to pin name
	FString VarNameStr = Params->GetStringField(TEXT("variable_name"));
	if (VarNameStr.IsEmpty())
	{
		VarNameStr = PinName;
	}
	FName VarName(*VarNameStr);

	// Check for name collision
	for (const FBPVariableDescription& Existing : BP->NewVariables)
	{
		if (Existing.VarName == VarName)
		{
			return FMonolithActionResult::Error(FString::Printf(
				TEXT("A variable named '%s' already exists in this Blueprint. Provide a unique 'variable_name'."), *VarNameStr));
		}
	}

	// Find the hosting graph (needed for placing the new variable node)
	UEdGraph* Graph = nullptr;
	if (!GraphName.IsEmpty())
	{
		Graph = MonolithBlueprintInternal::FindGraphByName(BP, GraphName);
	}
	else
	{
		// Find graph by searching for the node
		auto SearchInGraphs = [&](const TArray<TObjectPtr<UEdGraph>>& Graphs) -> UEdGraph*
		{
			for (const auto& G : Graphs)
			{
				if (!G) continue;
				for (UEdGraphNode* N : G->Nodes)
				{
					if (N && N->GetName() == NodeId) return G;
				}
			}
			return nullptr;
		};
		Graph = SearchInGraphs(BP->UbergraphPages);
		if (!Graph) Graph = SearchInGraphs(BP->FunctionGraphs);
		if (!Graph) Graph = SearchInGraphs(BP->MacroGraphs);
	}
	if (!Graph)
	{
		return FMonolithActionResult::Error(TEXT("Could not locate graph containing the node"));
	}

	// Build pin type string for the response before modifying anything
	FString TypeStr = MonolithBlueprintInternal::ContainerPrefix(Pin->PinType) +
	                  MonolithBlueprintInternal::PinTypeToString(Pin->PinType);

	// Step 1: Add the member variable.
	if (!FBlueprintEditorUtils::AddMemberVariable(BP, VarName, Pin->PinType))
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Failed to add variable '%s' — a variable with that name may already exist"), *VarNameStr));
	}

	// Step 2: Position the new variable node relative to the source node
	// NOTE: MarkBlueprintAsStructurallyModified is deferred to AFTER pin
	// rewiring to avoid invalidating the Pin pointer during skeleton regen.
	const EEdGraphPinDirection PinDir = Pin->Direction;
	int32 VarNodePosX = SourceNode->NodePosX + (PinDir == EGPD_Output ? 200 : -200);
	int32 VarNodePosY = SourceNode->NodePosY;

	// Step 4: Create VariableGet (for output pins — feeds data to consumers)
	//         or VariableSet (for input pins — receives data from producers)
	UEdGraphNode* VarNode = nullptr;
	int32 ConnectionsMade = 0;

	if (PinDir == EGPD_Output)
	{
		// Output pin → promote to VariableGet
		UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(Graph);
		GetNode->VariableReference.SetSelfMember(VarName);
		GetNode->NodePosX = VarNodePosX;
		GetNode->NodePosY = VarNodePosY;
		Graph->AddNode(GetNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);
		GetNode->AllocateDefaultPins();
		VarNode = GetNode;

		// Rewire: connect the VariableGet's output to each of the original pin's consumers
		// Find the output data pin on the new VariableGet node
		UEdGraphPin* GetOutputPin = nullptr;
		for (UEdGraphPin* P : GetNode->Pins)
		{
			if (P && P->Direction == EGPD_Output && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				GetOutputPin = P;
				break;
			}
		}

		if (GetOutputPin)
		{
			// Collect existing connections before breaking
			TArray<UEdGraphPin*> Consumers;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked) Consumers.Add(Linked);
			}

			// Break the original pin's connections
			Pin->BreakAllPinLinks(true);

			// Wire the VariableGet output to each former consumer
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			for (UEdGraphPin* Consumer : Consumers)
			{
				if (Schema->TryCreateConnection(GetOutputPin, Consumer))
				{
					ConnectionsMade++;
				}
			}
		}
	}
	else
	{
		// Input pin → promote to VariableSet
		UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(Graph);
		SetNode->VariableReference.SetSelfMember(VarName);
		SetNode->NodePosX = VarNodePosX;
		SetNode->NodePosY = VarNodePosY;
		Graph->AddNode(SetNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);
		SetNode->AllocateDefaultPins();
		VarNode = SetNode;

		// Find the input data pin on the VariableSet node (the value pin, not exec)
		UEdGraphPin* SetInputPin = nullptr;
		for (UEdGraphPin* P : SetNode->Pins)
		{
			if (P && P->Direction == EGPD_Input && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				SetInputPin = P;
				break;
			}
		}

		if (SetInputPin)
		{
			// Collect existing producers before breaking
			TArray<UEdGraphPin*> Producers;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked) Producers.Add(Linked);
			}

			// Break the original pin's connections
			Pin->BreakAllPinLinks(true);

			// Wire each former producer to the VariableSet input
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			for (UEdGraphPin* Producer : Producers)
			{
				if (Schema->TryCreateConnection(Producer, SetInputPin))
				{
					ConnectionsMade++;
				}
			}
		}
	}

	// Now safe to do structural modification — all pin rewiring is complete
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("variable_name"), VarNameStr);
	Root->SetStringField(TEXT("variable_type"), TypeStr);
	if (PinDir == EGPD_Output)
	{
		Root->SetStringField(TEXT("getter_node_id"), VarNode ? VarNode->GetName() : TEXT(""));
	}
	else
	{
		Root->SetStringField(TEXT("setter_node_id"), VarNode ? VarNode->GetName() : TEXT(""));
	}
	Root->SetNumberField(TEXT("connections_made"), ConnectionsMade);
	Root->SetStringField(TEXT("graph"), Graph->GetName());
	return FMonolithActionResult::Success(Root);
}
