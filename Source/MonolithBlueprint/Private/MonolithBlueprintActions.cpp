#include "MonolithBlueprintActions.h"
#include "MonolithJsonUtils.h"
#include "MonolithAssetUtils.h"
#include "MonolithParamSchema.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Variable.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "EdGraphNode_Comment.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// --- Internal helpers ---

namespace MonolithBlueprintInternal
{
	void AddGraphArray(
		TArray<TSharedPtr<FJsonValue>>& OutArr,
		const TArray<TObjectPtr<UEdGraph>>& Graphs,
		const FString& Type)
	{
		for (const auto& Graph : Graphs)
		{
			if (!Graph) continue;
			TSharedPtr<FJsonObject> GObj = MakeShared<FJsonObject>();
			GObj->SetStringField(TEXT("name"), Graph->GetName());
			GObj->SetStringField(TEXT("type"), Type);
			GObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
			OutArr.Add(MakeShared<FJsonValueObject>(GObj));
		}
	}

	UEdGraph* FindGraphByName(UBlueprint* BP, const FString& GraphName)
	{
		if (GraphName.IsEmpty() && BP->UbergraphPages.Num() > 0)
		{
			return BP->UbergraphPages[0];
		}

		auto SearchArray = [&](const TArray<TObjectPtr<UEdGraph>>& Arr) -> UEdGraph*
		{
			for (const auto& G : Arr)
			{
				if (G && G->GetName() == GraphName) return G;
			}
			return nullptr;
		};

		if (UEdGraph* G = SearchArray(BP->UbergraphPages)) return G;
		if (UEdGraph* G = SearchArray(BP->FunctionGraphs)) return G;
		if (UEdGraph* G = SearchArray(BP->MacroGraphs)) return G;
		if (UEdGraph* G = SearchArray(BP->DelegateSignatureGraphs)) return G;
		return nullptr;
	}

	FString PinTypeToString(const FEdGraphPinType& PinType)
	{
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			return TEXT("exec");
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
			return TEXT("bool");
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
			return TEXT("int");
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
			return TEXT("int64");
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
			return PinType.PinSubCategory == TEXT("double") ? TEXT("double") : TEXT("float");
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_String)
			return TEXT("string");
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
			return TEXT("name");
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
			return TEXT("text");
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
			return TEXT("byte");

		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
			PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
			PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
			PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass ||
			PinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
		{
			FString TypeName = PinType.PinCategory.ToString();
			if (PinType.PinSubCategoryObject.IsValid())
			{
				TypeName += TEXT(":") + PinType.PinSubCategoryObject->GetName();
			}
			return TypeName;
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			if (PinType.PinSubCategoryObject.IsValid())
			{
				return TEXT("struct:") + PinType.PinSubCategoryObject->GetName();
			}
			return TEXT("struct");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
		{
			if (PinType.PinSubCategoryObject.IsValid())
			{
				return TEXT("enum:") + PinType.PinSubCategoryObject->GetName();
			}
			return TEXT("enum");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
			return TEXT("wildcard");
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
			return TEXT("delegate");
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
			return TEXT("multicast_delegate");

		return PinType.PinCategory.ToString();
	}

	FString ContainerPrefix(const FEdGraphPinType& PinType)
	{
		switch (PinType.ContainerType)
		{
		case EPinContainerType::Array: return TEXT("array:");
		case EPinContainerType::Set: return TEXT("set:");
		case EPinContainerType::Map: return TEXT("map:");
		default: return TEXT("");
		}
	}

	TSharedPtr<FJsonObject> SerializePin(const UEdGraphPin* Pin)
	{
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("id"), Pin->PinId.ToString());
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("direction"),
			Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		PinObj->SetStringField(TEXT("type"),
			ContainerPrefix(Pin->PinType) + PinTypeToString(Pin->PinType));

		if (!Pin->DefaultValue.IsEmpty())
		{
			PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		}
		if (Pin->DefaultObject)
		{
			PinObj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
		}

		TArray<TSharedPtr<FJsonValue>> ConnArr;
		for (const UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (!Linked || !Linked->GetOwningNode()) continue;
			FString ConnId = FString::Printf(TEXT("%s.%s"),
				*Linked->GetOwningNode()->GetName(),
				*Linked->PinName.ToString());
			ConnArr.Add(MakeShared<FJsonValueString>(ConnId));
		}
		PinObj->SetArrayField(TEXT("connected_to"), ConnArr);
		return PinObj;
	}

	TSharedPtr<FJsonObject> SerializeNode(UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
		NObj->SetStringField(TEXT("id"), Node->GetName());
		NObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NObj->SetStringField(TEXT("title"),
			Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

		TArray<TSharedPtr<FJsonValue>> PosArr;
		PosArr.Add(MakeShared<FJsonValueNumber>(Node->NodePosX));
		PosArr.Add(MakeShared<FJsonValueNumber>(Node->NodePosY));
		NObj->SetArrayField(TEXT("pos"), PosArr);

		if (!Node->NodeComment.IsEmpty())
		{
			NObj->SetStringField(TEXT("comment"), Node->NodeComment);
		}

		if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			NObj->SetStringField(TEXT("function"),
				CallNode->FunctionReference.GetMemberName().ToString());
			if (UClass* OwnerClass = CallNode->FunctionReference.GetMemberParentClass())
			{
				NObj->SetStringField(TEXT("function_class"), OwnerClass->GetName());
			}
		}
		else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
		{
			NObj->SetStringField(TEXT("event_name"),
				EventNode->EventReference.GetMemberName().ToString());
			if (EventNode->CustomFunctionName != NAME_None)
			{
				NObj->SetStringField(TEXT("custom_name"),
					EventNode->CustomFunctionName.ToString());
			}
		}
		else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
		{
			if (MacroNode->GetMacroGraph())
			{
				NObj->SetStringField(TEXT("macro_name"),
					MacroNode->GetMacroGraph()->GetName());
			}
		}

		TArray<TSharedPtr<FJsonValue>> PinsArr;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden) continue;
			PinsArr.Add(MakeShared<FJsonValueObject>(SerializePin(Pin)));
		}
		NObj->SetArrayField(TEXT("pins"), PinsArr);

		return NObj;
	}

	TSharedPtr<FJsonObject> TraceExecFlow(
		UEdGraphNode* Node,
		TSet<UEdGraphNode*>& Visited,
		int32 MaxDepth = 100)
	{
		if (!Node || Visited.Contains(Node) || MaxDepth <= 0)
		{
			return nullptr;
		}
		Visited.Add(Node);

		TSharedPtr<FJsonObject> FlowObj = MakeShared<FJsonObject>();
		FlowObj->SetStringField(TEXT("node"),
			Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		FlowObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());

		TArray<UEdGraphPin*> ExecOutputs;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output &&
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
				!Pin->bHidden)
			{
				ExecOutputs.Add(Pin);
			}
		}

		if (ExecOutputs.Num() == 1 && ExecOutputs[0]->LinkedTo.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ThenArr;
			for (UEdGraphPin* Linked : ExecOutputs[0]->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode()) continue;
				TSharedPtr<FJsonObject> Next = TraceExecFlow(
					Linked->GetOwningNode(), Visited, MaxDepth - 1);
				if (Next)
				{
					ThenArr.Add(MakeShared<FJsonValueObject>(Next));
				}
			}
			if (ThenArr.Num() > 0)
			{
				FlowObj->SetArrayField(TEXT("then"), ThenArr);
			}
		}
		else if (ExecOutputs.Num() > 1)
		{
			TSharedPtr<FJsonObject> BranchesObj = MakeShared<FJsonObject>();
			for (UEdGraphPin* ExecPin : ExecOutputs)
			{
				TArray<TSharedPtr<FJsonValue>> BranchArr;
				for (UEdGraphPin* Linked : ExecPin->LinkedTo)
				{
					if (!Linked || !Linked->GetOwningNode()) continue;
					TSet<UEdGraphNode*> BranchVisited = Visited;
					TSharedPtr<FJsonObject> Next = TraceExecFlow(
						Linked->GetOwningNode(), BranchVisited, MaxDepth - 1);
					if (Next)
					{
						BranchArr.Add(MakeShared<FJsonValueObject>(Next));
					}
				}
				BranchesObj->SetArrayField(ExecPin->PinName.ToString(), BranchArr);
			}
			FlowObj->SetObjectField(TEXT("branches"), BranchesObj);
		}

		return FlowObj;
	}

	UEdGraphNode* FindEntryNode(UEdGraph* Graph, const FString& EntryPoint)
	{
		// Pass 1: Check event and function entry nodes specifically
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				FString EventName = EventNode->EventReference.GetMemberName().ToString();
				if (EventName == EntryPoint || EventNode->GetName() == EntryPoint)
					return Node;
				if (EventNode->CustomFunctionName != NAME_None &&
					EventNode->CustomFunctionName.ToString() == EntryPoint)
					return Node;
				FString DisplayTitle = EventNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				if (DisplayTitle.Contains(EntryPoint))
					return Node;
			}
			if (UK2Node_FunctionEntry* FuncEntry = Cast<UK2Node_FunctionEntry>(Node))
			{
				if (Graph->GetName() == EntryPoint)
					return Node;
			}
		}

		// Pass 2: Fuzzy fallback — skip comments and already-checked types
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (Cast<UK2Node_Event>(Node) || Cast<UK2Node_FunctionEntry>(Node))
				continue;
			if (Cast<UEdGraphNode_Comment>(Node))
				continue;
			FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (Title.Contains(EntryPoint))
				return Node;
		}
		return nullptr;
	}
}

// --- Registration ---

void FMonolithBlueprintActions::RegisterActions()
{
	FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

	Registry.RegisterAction(TEXT("blueprint"), TEXT("list_graphs"),
		TEXT("List all graphs in a Blueprint asset"),
		FMonolithActionHandler::CreateStatic(&HandleListGraphs),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("get_graph_data"),
		TEXT("Get full graph data with all nodes, pins, and connections. Optional node_class_filter to include only matching node classes."),
		FMonolithActionHandler::CreateStatic(&HandleGetGraphData),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Optional(TEXT("graph_name"), TEXT("string"), TEXT("Graph name (defaults to first event graph)"))
			.Optional(TEXT("node_class_filter"), TEXT("string"), TEXT("Only include nodes whose class contains this substring"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("get_graph_summary"),
		TEXT("Get lightweight graph summary with node id/class/title and exec-only connections. Returns all graphs when graph_name is empty."),
		FMonolithActionHandler::CreateStatic(&HandleGetGraphSummary),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Optional(TEXT("graph_name"), TEXT("string"), TEXT("Graph name (returns all graphs when empty)"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("get_variables"),
		TEXT("Get all variables defined in a Blueprint"),
		FMonolithActionHandler::CreateStatic(&HandleGetVariables),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("get_execution_flow"),
		TEXT("Get linearized execution flow from an entry point"),
		FMonolithActionHandler::CreateStatic(&HandleGetExecutionFlow),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("entry_point"), TEXT("string"), TEXT("Event or function entry point name"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("search_nodes"),
		TEXT("Search for nodes in a Blueprint by title or function name"),
		FMonolithActionHandler::CreateStatic(&HandleSearchNodes),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("query"), TEXT("string"), TEXT("Search string to match against node titles and function names"))
			.Build());
}

// --- Shared helper ---

UBlueprint* FMonolithBlueprintActions::LoadBlueprint(const TSharedPtr<FJsonObject>& Params, FString& OutAssetPath)
{
	OutAssetPath = Params->GetStringField(TEXT("asset_path"));
	if (OutAssetPath.IsEmpty())
	{
		return nullptr;
	}
	return FMonolithAssetUtils::LoadAssetByPath<UBlueprint>(OutAssetPath);
}

// --- list_graphs ---

FMonolithActionResult FMonolithBlueprintActions::HandleListGraphs(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = LoadBlueprint(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("class"), BP->GetClass()->GetName());

	if (BP->ParentClass)
	{
		Root->SetStringField(TEXT("parent_class"), BP->ParentClass->GetName());
	}

	TArray<TSharedPtr<FJsonValue>> GraphsArr;
	MonolithBlueprintInternal::AddGraphArray(GraphsArr, BP->UbergraphPages, TEXT("event_graph"));
	MonolithBlueprintInternal::AddGraphArray(GraphsArr, BP->FunctionGraphs, TEXT("function"));
	MonolithBlueprintInternal::AddGraphArray(GraphsArr, BP->MacroGraphs, TEXT("macro"));
	MonolithBlueprintInternal::AddGraphArray(GraphsArr, BP->DelegateSignatureGraphs, TEXT("delegate_signature"));
	Root->SetArrayField(TEXT("graphs"), GraphsArr);

	return FMonolithActionResult::Success(Root);
}

// --- get_graph_data ---

FMonolithActionResult FMonolithBlueprintActions::HandleGetGraphData(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = LoadBlueprint(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString GraphName = Params->GetStringField(TEXT("graph_name"));
	FString ClassFilter = Params->GetStringField(TEXT("node_class_filter"));
	UEdGraph* Graph = MonolithBlueprintInternal::FindGraphByName(BP, GraphName);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("graph_name"), Graph->GetName());

	FString GraphType = TEXT("unknown");
	if (BP->UbergraphPages.Contains(Graph)) GraphType = TEXT("event_graph");
	else if (BP->FunctionGraphs.Contains(Graph)) GraphType = TEXT("function");
	else if (BP->MacroGraphs.Contains(Graph)) GraphType = TEXT("macro");
	else if (BP->DelegateSignatureGraphs.Contains(Graph)) GraphType = TEXT("delegate_signature");
	Root->SetStringField(TEXT("graph_type"), GraphType);

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (!ClassFilter.IsEmpty() && !Node->GetClass()->GetName().Contains(ClassFilter)) continue;
		NodesArr.Add(MakeShared<FJsonValueObject>(MonolithBlueprintInternal::SerializeNode(Node)));
	}
	Root->SetArrayField(TEXT("nodes"), NodesArr);

	return FMonolithActionResult::Success(Root);
}

// --- get_graph_summary ---

FMonolithActionResult FMonolithBlueprintActions::HandleGetGraphSummary(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = LoadBlueprint(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Lambda: summarize a single graph into a node array
	auto SummarizeGraph = [](UEdGraph* Graph, TArray<TSharedPtr<FJsonValue>>& OutNodes)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;

			TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
			NObj->SetStringField(TEXT("id"), Node->GetName());
			NObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			NObj->SetStringField(TEXT("title"),
				Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

			if (!Node->NodeComment.IsEmpty())
			{
				NObj->SetStringField(TEXT("comment"), Node->NodeComment);
			}

			// Collect exec-only output connections
			TArray<TSharedPtr<FJsonValue>> ExecTargets;
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) continue;
				if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;

				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked || !Linked->GetOwningNode()) continue;
					ExecTargets.Add(MakeShared<FJsonValueString>(Linked->GetOwningNode()->GetName()));
				}
			}
			if (ExecTargets.Num() > 0)
			{
				NObj->SetArrayField(TEXT("exec_targets"), ExecTargets);
			}

			OutNodes.Add(MakeShared<FJsonValueObject>(NObj));
		}
	};

	FString GraphName = Params->GetStringField(TEXT("graph_name"));

	// When graph_name is provided, return single graph at root level (backward compat)
	if (!GraphName.IsEmpty())
	{
		UEdGraph* Graph = MonolithBlueprintInternal::FindGraphByName(BP, GraphName);
		if (!Graph)
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		}

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("graph_name"), Graph->GetName());

		TArray<TSharedPtr<FJsonValue>> NodesArr;
		SummarizeGraph(Graph, NodesArr);
		Root->SetArrayField(TEXT("nodes"), NodesArr);
		Root->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

		return FMonolithActionResult::Success(Root);
	}

	// When graph_name is empty, return all graphs
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> GraphsArr;
	int32 TotalNodeCount = 0;

	struct FGraphArrayInfo
	{
		const TArray<TObjectPtr<UEdGraph>>* Graphs;
		const TCHAR* Type;
	};

	TArray<FGraphArrayInfo> GraphArrays = {
		{ &BP->UbergraphPages, TEXT("event_graph") },
		{ &BP->FunctionGraphs, TEXT("function") },
		{ &BP->MacroGraphs, TEXT("macro") },
		{ &BP->DelegateSignatureGraphs, TEXT("delegate_signature") }
	};

	for (const FGraphArrayInfo& Info : GraphArrays)
	{
		for (const auto& Graph : *Info.Graphs)
		{
			if (!Graph) continue;

			TSharedPtr<FJsonObject> GObj = MakeShared<FJsonObject>();
			GObj->SetStringField(TEXT("graph_name"), Graph->GetName());
			GObj->SetStringField(TEXT("graph_type"), Info.Type);

			TArray<TSharedPtr<FJsonValue>> NodesArr;
			SummarizeGraph(Graph, NodesArr);
			GObj->SetArrayField(TEXT("nodes"), NodesArr);
			GObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

			TotalNodeCount += Graph->Nodes.Num();
			GraphsArr.Add(MakeShared<FJsonValueObject>(GObj));
		}
	}

	Root->SetArrayField(TEXT("graphs"), GraphsArr);
	Root->SetNumberField(TEXT("graph_count"), GraphsArr.Num());
	Root->SetNumberField(TEXT("total_node_count"), TotalNodeCount);

	return FMonolithActionResult::Success(Root);
}

// --- get_variables ---

FMonolithActionResult FMonolithBlueprintActions::HandleGetVariables(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = LoadBlueprint(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> VarsArr;

	UClass* GenClass = BP->GeneratedClass;
	UObject* CDO = GenClass ? GenClass->GetDefaultObject(false) : nullptr;

	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		TSharedPtr<FJsonObject> VObj = MakeShared<FJsonObject>();
		VObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VObj->SetStringField(TEXT("type"),
			MonolithBlueprintInternal::ContainerPrefix(Var.VarType) +
			MonolithBlueprintInternal::PinTypeToString(Var.VarType));

		FString DefaultStr = Var.DefaultValue;
		if (DefaultStr.IsEmpty() && CDO && GenClass)
		{
			FProperty* Prop = GenClass->FindPropertyByName(Var.VarName);
			if (Prop)
			{
				const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);
				Prop->ExportTextItem_Direct(DefaultStr, ValuePtr, nullptr, CDO, PPF_None);
			}
		}
		VObj->SetStringField(TEXT("default_value"), DefaultStr);
		VObj->SetStringField(TEXT("category"), Var.Category.ToString());

		VObj->SetBoolField(TEXT("instance_editable"),
			(Var.PropertyFlags & CPF_DisableEditOnInstance) == 0);
		VObj->SetBoolField(TEXT("blueprint_read_only"),
			(Var.PropertyFlags & CPF_BlueprintReadOnly) != 0);
		VObj->SetBoolField(TEXT("expose_on_spawn"),
			(Var.PropertyFlags & CPF_ExposeOnSpawn) != 0);
		VObj->SetBoolField(TEXT("replicated"),
			(Var.PropertyFlags & CPF_Net) != 0);
		VObj->SetBoolField(TEXT("transient"),
			(Var.PropertyFlags & CPF_Transient) != 0);

		VarsArr.Add(MakeShared<FJsonValueObject>(VObj));
	}

	Root->SetArrayField(TEXT("variables"), VarsArr);
	return FMonolithActionResult::Success(Root);
}

// --- get_execution_flow ---

FMonolithActionResult FMonolithBlueprintActions::HandleGetExecutionFlow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = LoadBlueprint(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString EntryPoint = Params->GetStringField(TEXT("entry_point"));
	if (EntryPoint.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: entry_point"));
	}

	UEdGraphNode* EntryNode = nullptr;
	UEdGraph* FoundGraph = nullptr;

	auto SearchGraphs = [&](const TArray<TObjectPtr<UEdGraph>>& Graphs)
	{
		for (const auto& Graph : Graphs)
		{
			if (!Graph) continue;
			UEdGraphNode* Node = MonolithBlueprintInternal::FindEntryNode(Graph, EntryPoint);
			if (Node)
			{
				EntryNode = Node;
				FoundGraph = Graph;
				return;
			}
		}
	};

	SearchGraphs(BP->UbergraphPages);
	if (!EntryNode) SearchGraphs(BP->FunctionGraphs);
	if (!EntryNode) SearchGraphs(BP->MacroGraphs);

	if (!EntryNode)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Entry point not found: %s"), *EntryPoint));
	}

	TSet<UEdGraphNode*> Visited;
	TSharedPtr<FJsonObject> Flow = MonolithBlueprintInternal::TraceExecFlow(EntryNode, Visited);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("entry"), EntryPoint);
	Root->SetStringField(TEXT("graph"), FoundGraph->GetName());
	if (Flow)
	{
		Root->SetObjectField(TEXT("flow"), Flow);
	}

	return FMonolithActionResult::Success(Root);
}

// --- search_nodes ---

FMonolithActionResult FMonolithBlueprintActions::HandleSearchNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = LoadBlueprint(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString Query = Params->GetStringField(TEXT("query"));
	if (Query.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: query"));
	}

	FString QueryLower = Query.ToLower();
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Results;

	auto SearchGraphs = [&](const TArray<TObjectPtr<UEdGraph>>& Graphs, const FString& Type)
	{
		for (const auto& Graph : Graphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				FString ClassName = Node->GetClass()->GetName();
				FString FuncName;

				if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
				{
					FuncName = CallNode->FunctionReference.GetMemberName().ToString();
				}

				bool bMatch = Title.ToLower().Contains(QueryLower) ||
							  ClassName.ToLower().Contains(QueryLower) ||
							  FuncName.ToLower().Contains(QueryLower);

				if (bMatch)
				{
					TSharedPtr<FJsonObject> RObj = MakeShared<FJsonObject>();
					RObj->SetStringField(TEXT("graph"), Graph->GetName());
					RObj->SetStringField(TEXT("graph_type"), Type);
					RObj->SetStringField(TEXT("node_id"), Node->GetName());
					RObj->SetStringField(TEXT("class"), ClassName);
					RObj->SetStringField(TEXT("title"), Title);
					if (!FuncName.IsEmpty())
					{
						RObj->SetStringField(TEXT("function"), FuncName);
					}
					Results.Add(MakeShared<FJsonValueObject>(RObj));
				}
			}
		}
	};

	SearchGraphs(BP->UbergraphPages, TEXT("event_graph"));
	SearchGraphs(BP->FunctionGraphs, TEXT("function"));
	SearchGraphs(BP->MacroGraphs, TEXT("macro"));
	SearchGraphs(BP->DelegateSignatureGraphs, TEXT("delegate_signature"));

	Root->SetStringField(TEXT("query"), Query);
	Root->SetNumberField(TEXT("match_count"), Results.Num());
	Root->SetArrayField(TEXT("results"), Results);

	return FMonolithActionResult::Success(Root);
}
