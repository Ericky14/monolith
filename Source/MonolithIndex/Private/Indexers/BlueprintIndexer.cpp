#include "Indexers/BlueprintIndexer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_InputKey.h"
#include "K2Node_EnhancedInputAction.h"
#include "InputAction.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

bool FBlueprintIndexer::IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset);
	if (!Blueprint) return false;

	// Index all graphs
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph)
		{
			IndexGraph(Graph, DB, AssetId);
		}
	}

	// Index variables
	IndexVariables(Blueprint, DB, AssetId);

	return true;
}

void FBlueprintIndexer::IndexGraph(UEdGraph* Graph, FMonolithIndexDatabase& DB, int64 AssetId)
{
	if (!Graph) return;

	// Map from UEdGraphNode* to DB node ID for connection resolution
	TMap<UEdGraphNode*, int64> NodeIdMap;

	// Index all nodes
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		FIndexedNode IndexedNode;
		IndexedNode.AssetId = AssetId;
		IndexedNode.NodeName = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		IndexedNode.NodeClass = Node->GetClass()->GetName();
		IndexedNode.PosX = Node->NodePosX;
		IndexedNode.PosY = Node->NodePosY;
		// Addressing: which graph, and the node id the blueprint actions accept.
		IndexedNode.GraphName = Graph->GetName();
		IndexedNode.NodeObjectName = Node->GetName();

		// Determine node type.
		//
		// Enhanced Input events are checked BEFORE UK2Node_Event: they are not UK2Node_Event
		// subclasses, but ordering matters for the legacy input nodes below, and grouping the
		// input cases together keeps the "what handles this key" contract in one place.
		if (UK2Node_EnhancedInputAction* InputNode = Cast<UK2Node_EnhancedInputAction>(Node))
		{
			// call_target carries the InputAction name so FindInputHandlers can join
			// key -> action -> this node without a project-wide text scan.
			IndexedNode.NodeType = TEXT("InputEvent");
			if (InputNode->InputAction)
			{
				IndexedNode.CallTarget = InputNode->InputAction->GetName();

				auto PropsObj = MakeShared<FJsonObject>();
				PropsObj->SetStringField(TEXT("input_action"), InputNode->InputAction->GetName());
				PropsObj->SetStringField(TEXT("input_action_path"), InputNode->InputAction->GetPathName());
				FString PropsStr;
				auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PropsStr);
				FJsonSerializer::Serialize(PropsObj, *Writer, true);
				IndexedNode.Properties = PropsStr;
			}
		}
		else if (UK2Node_InputKey* LegacyKeyNode = Cast<UK2Node_InputKey>(Node))
		{
			// FCS hides much of its input in these legacy nodes, which bypass Enhanced Input
			// entirely. Indexing them by KEY (not action) is what makes a key search find them.
			IndexedNode.NodeType = TEXT("InputEvent");
			IndexedNode.CallTarget = LegacyKeyNode->InputKey.GetFName().ToString();
		}
		else if (Cast<UK2Node_Event>(Node))
		{
			IndexedNode.NodeType = TEXT("Event");
		}
		else if (UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node))
		{
			IndexedNode.NodeType = TEXT("FunctionCall");
			// The join key for "who calls X". node_name is the display title and cannot serve.
			IndexedNode.CallTarget = FuncNode->FunctionReference.GetMemberName().ToString();

			auto PropsObj = MakeShared<FJsonObject>();
			PropsObj->SetStringField(TEXT("function"),
				FuncNode->FunctionReference.GetMemberName().ToString());
			if (FuncNode->FunctionReference.GetMemberParentClass())
			{
				PropsObj->SetStringField(TEXT("target_class"),
					FuncNode->FunctionReference.GetMemberParentClass()->GetName());
			}
			FString PropsStr;
			auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PropsStr);
			FJsonSerializer::Serialize(PropsObj, *Writer, true);
			IndexedNode.Properties = PropsStr;
		}
		else if (Cast<UK2Node_VariableGet>(Node) || Cast<UK2Node_VariableSet>(Node))
		{
			IndexedNode.NodeType = TEXT("Variable");
		}
		else
		{
			IndexedNode.NodeType = TEXT("Other");
		}

		int64 NodeId = DB.InsertNode(IndexedNode);
		if (NodeId >= 0)
		{
			NodeIdMap.Add(Node, NodeId);
			IndexPinDefaults(Node, DB, NodeId);
		}
	}

	// Index connections by walking output pins
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		int64* SourceNodeId = NodeIdMap.Find(Node);
		if (!SourceNodeId) continue;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;

				int64* TargetNodeId = NodeIdMap.Find(LinkedPin->GetOwningNode());
				if (!TargetNodeId) continue;

				FIndexedConnection Conn;
				Conn.SourceNodeId = *SourceNodeId;
				Conn.SourcePin = Pin->PinName.ToString();
				Conn.TargetNodeId = *TargetNodeId;
				Conn.TargetPin = LinkedPin->PinName.ToString();

				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					Conn.PinType = TEXT("Exec");
				}
				else
				{
					Conn.PinType = Pin->PinType.PinCategory.ToString();
				}

				DB.InsertConnection(Conn);
			}
		}
	}
}

void FBlueprintIndexer::IndexPinDefaults(UEdGraphNode* Node, FMonolithIndexDatabase& DB, int64 NodeId)
{
	if (!Node) return;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		// Only INPUT pins carry authored literals, and a CONNECTED pin's default is dead data -
		// indexing it would return matches for values the graph never uses.
		if (!Pin || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() > 0)
		{
			continue;
		}

		// The 'self' pin's default is the target library's CDO (KismetMathLibrary and friends) -
		// present on a large share of nodes and never something anyone searches for.
		if (Pin->PinName == UEdGraphSchema_K2::PN_Self)
		{
			continue;
		}

		FIndexedPinDefault Entry;
		Entry.NodeId = NodeId;
		Entry.PinName = Pin->PinName.ToString();

		// A pin's value lives in ONE of three places depending on its type. Reading only
		// DefaultValue (the obvious one) silently misses every text and object literal -
		// exactly the values worth searching for (item rows, montages, data tables).
		if (Pin->DefaultObject)
		{
			Entry.ValueKind = TEXT("object");
			Entry.ValueText = Pin->DefaultObject->GetPathName();
		}
		else if (!Pin->DefaultTextValue.IsEmpty())
		{
			Entry.ValueKind = TEXT("text");
			Entry.ValueText = Pin->DefaultTextValue.ToString();
		}
		else if (!Pin->DefaultValue.IsEmpty())
		{
			Entry.ValueKind = TEXT("string");
			Entry.ValueText = Pin->DefaultValue;
		}
		else
		{
			continue;
		}

		// Drop values that carry no information: an unset struct, a zero, an empty row handle.
		// Without this the table fills with tens of thousands of "(DataTable=None,RowName=\"\")"
		// rows and a literal search returns mostly noise, which defeats the point of indexing
		// literals at all.
		static const TSet<FString> EmptyValues = {
			TEXT("()"), TEXT("0"), TEXT("0.0"), TEXT("0.000000"), TEXT("None"), TEXT("false"),
			TEXT("(DataTable=None,RowName=\"\")"), TEXT("(X=0.000000,Y=0.000000,Z=0.000000)"),
			TEXT("(X=0.000000,Y=0.000000)")
		};
		if (EmptyValues.Contains(Entry.ValueText))
		{
			continue;
		}

		// Structs serialise to long literals - keep the row searchable without bloating the DB.
		static constexpr int32 MaxValueChars = 512;
		if (Entry.ValueText.Len() > MaxValueChars)
		{
			Entry.ValueText.LeftInline(MaxValueChars);
		}

		DB.InsertPinDefault(Entry);
	}
}

void FBlueprintIndexer::IndexVariables(UBlueprint* Blueprint, FMonolithIndexDatabase& DB, int64 AssetId)
{
	if (!Blueprint) return;

	for (const FBPVariableDescription& VarDesc : Blueprint->NewVariables)
	{
		FIndexedVariable Var;
		Var.AssetId = AssetId;
		Var.VarName = VarDesc.VarName.ToString();
		Var.VarType = VarDesc.VarType.PinCategory.ToString();
		Var.Category = VarDesc.Category.ToString();
		Var.DefaultValue = VarDesc.DefaultValue;
		if (Var.DefaultValue.IsEmpty() && Blueprint->GeneratedClass)
		{
			UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject(false);
			if (CDO)
			{
				FProperty* Prop = Blueprint->GeneratedClass->FindPropertyByName(VarDesc.VarName);
				if (Prop)
				{
					const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);
					Prop->ExportTextItem_Direct(Var.DefaultValue, ValuePtr, nullptr, CDO, PPF_None);
				}
			}
		}
		Var.bIsExposed = !!(VarDesc.PropertyFlags & CPF_ExposeOnSpawn);
		Var.bIsReplicated = !!(VarDesc.PropertyFlags & CPF_Net);

		DB.InsertVariable(Var);
	}
}
