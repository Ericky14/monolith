// Headless voxel graph authoring: voxel.add_node / connect_pins / remove_node.
//
// LINKAGE RULE (hard-won): the VoxelGraphEditor node/schema classes
// (UVoxelGraphNode_Struct, UVoxelGraphSchema, ...) ship WITHOUT module API macros,
// so their out-of-line symbols cannot be linked from here. Everything below reaches
// them exclusively through (a) UClass lookup by path, (b) engine base-class virtuals
// (UEdGraphNode / UEdGraphSchema), and (c) property reflection for the node's Struct
// payload. The exported surface we lean on: FVoxelInstancedStruct +
// GetDerivedStructs (VoxelCore), FVoxelNode / FVoxelNode_UFunction /
// UVoxelFunctionLibrary / GVoxelGraphTracker (VoxelGraph), FVoxelTransaction
// (VoxelCoreEditor).
//
// Change tracking: every mutation sits inside an FVoxelTransaction on the EdGraph,
// whose destructor fires UVoxelEdGraph::PostEditChangeProperty ->
// GVoxelGraphTracker->NotifyEdGraphChanged -> (queued) terminal-graph re-translate.
// The tracker only runs its queue on editor Tick, so each action ends with an
// explicit Flush() - otherwise a save that follows immediately persists the EdGraph
// with a STALE serialized graph (the thing the compiler actually reads).

#include "MonolithVoxelActions.h"
#include "MonolithAssetUtils.h"
#include "MonolithParamSchema.h"

#include "VoxelGraph.h"
#include "VoxelTerminalGraph.h"
#include "VoxelGraphTracker.h"
#include "VoxelNode.h"
#include "Nodes/VoxelNode_UFunction.h"
#include "VoxelFunctionLibrary.h"
#include "VoxelTransaction.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "UObject/UObjectHash.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace MonolithVoxelGraphAuthor
{
#if WITH_EDITOR
    UVoxelGraph *LoadGraph(const TSharedPtr<FJsonObject> &Params, FString &OutAssetPath)
    {
        OutAssetPath = Params->GetStringField(TEXT("asset_path"));
        return FMonolithAssetUtils::LoadAssetByPath<UVoxelGraph>(OutAssetPath);
    }

    UVoxelTerminalGraph *ResolveTerminalGraph(UVoxelGraph *VoxelGraph, const FString &Ref)
    {
        if (Ref.IsEmpty())
        {
            if (VoxelGraph->HasMainTerminalGraph())
            {
                return &VoxelGraph->GetMainTerminalGraph();
            }
        }
        FGuid ParsedGuid;
        if (FGuid::Parse(Ref, ParsedGuid))
        {
            return VoxelGraph->FindTerminalGraph(ParsedGuid);
        }
        TVoxelSet<FGuid> Guids = VoxelGraph->GetTerminalGraphs();
        for (const FGuid &Guid : Guids)
        {
            UVoxelTerminalGraph *TG = VoxelGraph->FindTerminalGraph(Guid);
            if (TG && (Ref.IsEmpty() || TG->GetDisplayName() == Ref))
            {
                return TG;
            }
        }
        return nullptr;
    }

    UEdGraphNode *FindNode(UEdGraph &EdGraph, const FString &NodeId)
    {
        for (UEdGraphNode *Node : EdGraph.Nodes)
        {
            if (Node && Node->GetName() == NodeId)
            {
                return Node;
            }
        }
        return nullptr;
    }

    UEdGraphPin *FindPin(UEdGraphNode *Node, const FString &PinRef)
    {
        for (UEdGraphPin *Pin : Node->Pins)
        {
            if (Pin && (Pin->PinName.ToString() == PinRef || Pin->PinId.ToString() == PinRef))
            {
                return Pin;
            }
        }
        return nullptr;
    }

    FString NormalizeName(const FString &In)
    {
        return In.Replace(TEXT(" "), TEXT("")).ToLower();
    }

    // Resolve what to place: an FVoxelNode-derived USTRUCT (by struct name or display
    // name) or a UVoxelFunctionLibrary UFUNCTION (by function name). Exact struct-name
    // matches bypass the Abstract/Internal filter (deliberate power-user escape hatch);
    // display-name search applies it, mirroring the editor's node menu.
    UScriptStruct *ResolveNodeStruct(const FString &Request, UFunction *&OutFunction, FString &OutMatched)
    {
        OutFunction = nullptr;
        const FString Wanted = NormalizeName(Request);

        TVoxelArray<UScriptStruct *> Structs = GetDerivedStructs<FVoxelNode>();
        for (UScriptStruct *Struct : Structs)
        {
            if (Struct->GetName() == Request ||
                Struct->GetStructCPPName() == Request)
            {
                OutMatched = Struct->GetName();
                return Struct;
            }
        }
        for (UScriptStruct *Struct : Structs)
        {
            if (Struct->HasMetaData(TEXT("Abstract")) || Struct->HasMetaData(TEXT("Internal")))
            {
                continue;
            }
            if (NormalizeName(Struct->GetName()) == Wanted ||
                NormalizeName(Struct->GetName()) == TEXT("voxelnode_") + Wanted ||
                NormalizeName(Struct->GetName()) == TEXT("voxeltemplatenode_") + Wanted)
            {
                OutMatched = Struct->GetName();
                return Struct;
            }
            const TSharedRef<FVoxelNode> Instance = MakeSharedStruct<FVoxelNode>(Struct);
            if (NormalizeName(Instance->GetDisplayName()) == Wanted)
            {
                OutMatched = Struct->GetName();
                return Struct;
            }
        }

        // UFunction node ("GetPosition2D", "BreakVector2D", ...)
        TArray<UClass *> Libraries;
        GetDerivedClasses(UVoxelFunctionLibrary::StaticClass(), Libraries, true);
        for (UClass *Library : Libraries)
        {
            for (TFieldIterator<UFunction> It(Library, EFieldIteratorFlags::ExcludeSuper); It; ++It)
            {
                UFunction *Function = *It;
                if (Function->HasMetaData(TEXT("Internal")))
                {
                    continue;
                }
                if (NormalizeName(Function->GetName()) == Wanted)
                {
                    OutFunction = Function;
                    OutMatched = Library->GetName() + TEXT(".") + Function->GetName();
                    return FVoxelNode_UFunction::StaticStruct();
                }
            }
        }
        return nullptr;
    }

    TSharedPtr<FJsonObject> SerializePins(UEdGraphNode *Node)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("node_id"), Node->GetName());
        Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        TArray<TSharedPtr<FJsonValue>> Pins;
        for (const UEdGraphPin *Pin : Node->Pins)
        {
            if (!Pin || Pin->bHidden)
            {
                continue;
            }
            TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
            P->SetStringField(TEXT("name"), Pin->PinName.ToString());
            P->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"));
            if (!Pin->DefaultValue.IsEmpty())
            {
                P->SetStringField(TEXT("default"), Pin->DefaultValue);
            }
            Pins.Add(MakeShared<FJsonValueObject>(P));
        }
        Obj->SetArrayField(TEXT("pins"), Pins);
        return Obj;
    }
#endif
}

// --- add_node ---

static FMonolithActionResult HandleGraphAddNode(const TSharedPtr<FJsonObject> &Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelGraphAuthor;

    FString AssetPath;
    UVoxelGraph *VoxelGraph = LoadGraph(Params, AssetPath);
    if (!VoxelGraph)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel graph not found: %s"), *AssetPath));
    }
    UVoxelTerminalGraph *TG = ResolveTerminalGraph(VoxelGraph, Params->GetStringField(TEXT("terminal_graph")));
    if (!TG)
    {
        return FMonolithActionResult::Error(TEXT("Terminal graph not found"));
    }

    const FString Request = Params->GetStringField(TEXT("node"));
    UFunction *Function = nullptr;
    FString Matched;
    UScriptStruct *NodeStruct = ResolveNodeStruct(Request, Function, Matched);
    if (!NodeStruct)
    {
        return FMonolithActionResult::Error(FString::Printf(
            TEXT("No FVoxelNode struct or UVoxelFunctionLibrary function matches '%s'"), *Request));
    }

    UClass *NodeClass = FindObject<UClass>(nullptr, TEXT("/Script/VoxelGraphEditor.VoxelGraphNode_Struct"));
    if (!NodeClass)
    {
        return FMonolithActionResult::Error(TEXT("VoxelGraphNode_Struct class not found (VoxelGraphEditor not loaded?)"));
    }
    FProperty *StructProp = NodeClass->FindPropertyByName(TEXT("Struct"));
    if (!StructProp)
    {
        return FMonolithActionResult::Error(TEXT("VoxelGraphNode_Struct.Struct property not found"));
    }

    UEdGraph &EdGraph = TG->GetEdGraph();
    UEdGraphNode *Node = nullptr;
    {
        const FVoxelTransaction Transaction(&EdGraph, TEXT("Monolith: add voxel graph node"));
        EdGraph.Modify();
        Node = NewObject<UEdGraphNode>(&EdGraph, NodeClass, NAME_None, RF_Transactional);

        // Payload BEFORE Finalize: AllocateDefaultPins builds pins from the struct.
        FVoxelInstancedStruct *Instanced = StructProp->ContainerPtrToValuePtr<FVoxelInstancedStruct>(Node);
        Instanced->InitializeAs(NodeStruct);
        if (Function)
        {
            static_cast<FVoxelNode_UFunction *>(Instanced->GetStructMemory())->SetFunction_EditorOnly(Function);
        }

        EdGraph.AddNode(Node, false, false); // fires GRAPHACTION_AddNode -> tracker notify
        Node->CreateNewGuid();
        Node->PostPlacedNewNode();
        Node->AllocateDefaultPins();
        double X = 0, Y = 0;
        Params->TryGetNumberField(TEXT("x"), X);
        Params->TryGetNumberField(TEXT("y"), Y);
        Node->NodePosX = static_cast<int32>(X);
        Node->NodePosY = static_cast<int32>(Y);
    }
    VoxelGraph->MarkPackageDirty();
    if (GVoxelGraphTracker)
    {
        GVoxelGraphTracker->Flush();
    }

    TSharedPtr<FJsonObject> Result = SerializePins(Node);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("matched"), Matched);
    Result->SetStringField(TEXT("note"), TEXT("Not saved to disk yet - use editor.save_packages."));
    return FMonolithActionResult::Success(Result);
#else
    return FMonolithActionResult::Error(TEXT("Voxel graph editing requires editor build"));
#endif
}

// --- connect_pins ---

static FMonolithActionResult HandleGraphConnectPins(const TSharedPtr<FJsonObject> &Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelGraphAuthor;

    FString AssetPath;
    UVoxelGraph *VoxelGraph = LoadGraph(Params, AssetPath);
    if (!VoxelGraph)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel graph not found: %s"), *AssetPath));
    }
    UVoxelTerminalGraph *TG = ResolveTerminalGraph(VoxelGraph, Params->GetStringField(TEXT("terminal_graph")));
    if (!TG)
    {
        return FMonolithActionResult::Error(TEXT("Terminal graph not found"));
    }
    UEdGraph &EdGraph = TG->GetEdGraph();

    const FString FromNodeId = Params->GetStringField(TEXT("from_node"));
    const FString FromPinRef = Params->GetStringField(TEXT("from_pin"));
    const FString ToNodeId = Params->GetStringField(TEXT("to_node"));
    const FString ToPinRef = Params->GetStringField(TEXT("to_pin"));

    UEdGraphNode *FromNode = FindNode(EdGraph, FromNodeId);
    UEdGraphNode *ToNode = FindNode(EdGraph, ToNodeId);
    if (!FromNode || !ToNode)
    {
        return FMonolithActionResult::Error(FString::Printf(
            TEXT("Node not found: %s"), !FromNode ? *FromNodeId : *ToNodeId));
    }
    UEdGraphPin *FromPin = FindPin(FromNode, FromPinRef);
    UEdGraphPin *ToPin = FindPin(ToNode, ToPinRef);
    if (!FromPin || !ToPin)
    {
        return FMonolithActionResult::Error(FString::Printf(
            TEXT("Pin not found: %s"), !FromPin ? *FromPinRef : *ToPinRef));
    }
    if (FromPin->Direction != EGPD_Output || ToPin->Direction != EGPD_Input)
    {
        return FMonolithActionResult::Error(TEXT("from_pin must be an output and to_pin an input"));
    }

    const UEdGraphSchema *Schema = EdGraph.GetSchema();
    const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
    if (Response.Response == CONNECT_RESPONSE_DISALLOW)
    {
        return FMonolithActionResult::Error(FString::Printf(
            TEXT("Connection disallowed: %s"), *Response.Message.ToString()));
    }

    bool bConnected = false;
    {
        const FVoxelTransaction Transaction(&EdGraph, TEXT("Monolith: connect voxel graph pins"));
        bConnected = Schema->TryCreateConnection(FromPin, ToPin);
    }
    VoxelGraph->MarkPackageDirty();
    if (GVoxelGraphTracker)
    {
        GVoxelGraphTracker->Flush();
    }

    // Promotion can reconstruct nodes, invalidating every pin pointer - re-find by
    // name and report the ACTUAL link state, never the request.
    FromNode = FindNode(EdGraph, FromNodeId);
    ToNode = FindNode(EdGraph, ToNodeId);
    bool bLinked = false;
    if (FromNode && ToNode)
    {
        if (UEdGraphPin *NewToPin = FindPin(ToNode, ToPinRef))
        {
            for (const UEdGraphPin *Linked : NewToPin->LinkedTo)
            {
                if (Linked && Linked->GetOwningNode() == FromNode)
                {
                    bLinked = true;
                    break;
                }
            }
        }
    }
    if (!bConnected || !bLinked)
    {
        return FMonolithActionResult::Error(FString::Printf(
            TEXT("TryCreateConnection %s and link verification %s (%s)"),
            bConnected ? TEXT("succeeded") : TEXT("failed"),
            bLinked ? TEXT("passed") : TEXT("failed"),
            *Response.Message.ToString()));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("from"), FromNodeId + TEXT(".") + FromPinRef);
    Result->SetStringField(TEXT("to"), ToNodeId + TEXT(".") + ToPinRef);
    Result->SetBoolField(TEXT("linked"), true);
    Result->SetStringField(TEXT("response"), Response.Message.ToString());
    Result->SetStringField(TEXT("note"), TEXT("Not saved to disk yet - use editor.save_packages."));
    return FMonolithActionResult::Success(Result);
#else
    return FMonolithActionResult::Error(TEXT("Voxel graph editing requires editor build"));
#endif
}

// --- remove_node ---

static FMonolithActionResult HandleGraphRemoveNode(const TSharedPtr<FJsonObject> &Params)
{
#if WITH_EDITOR
    using namespace MonolithVoxelGraphAuthor;

    FString AssetPath;
    UVoxelGraph *VoxelGraph = LoadGraph(Params, AssetPath);
    if (!VoxelGraph)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel graph not found: %s"), *AssetPath));
    }
    UVoxelTerminalGraph *TG = ResolveTerminalGraph(VoxelGraph, Params->GetStringField(TEXT("terminal_graph")));
    if (!TG)
    {
        return FMonolithActionResult::Error(TEXT("Terminal graph not found"));
    }
    UEdGraph &EdGraph = TG->GetEdGraph();

    const FString NodeId = Params->GetStringField(TEXT("node_id"));
    UEdGraphNode *Node = FindNode(EdGraph, NodeId);
    if (!Node)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));
    }
    if (!Node->CanUserDeleteNode())
    {
        return FMonolithActionResult::Error(FString::Printf(
            TEXT("Node %s refuses deletion (e.g. Output Height) - not removed"), *NodeId));
    }

    const FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
    {
        const FVoxelTransaction Transaction(&EdGraph, TEXT("Monolith: remove voxel graph node"));
        Node->Modify();
        Node->BreakAllNodeLinks();
        EdGraph.RemoveNode(Node); // fires GRAPHACTION_RemoveNode -> tracker notify
    }
    VoxelGraph->MarkPackageDirty();
    if (GVoxelGraphTracker)
    {
        GVoxelGraphTracker->Flush();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("removed"), NodeId);
    Result->SetStringField(TEXT("title"), Title);
    Result->SetStringField(TEXT("note"), TEXT("Not saved to disk yet - use editor.save_packages."));
    return FMonolithActionResult::Success(Result);
#else
    return FMonolithActionResult::Error(TEXT("Voxel graph editing requires editor build"));
#endif
}

// --- Registration ---

void FMonolithVoxelActions::RegisterGraphAuthorActions()
{
    FMonolithToolRegistry &Registry = FMonolithToolRegistry::Get();

    Registry.RegisterAction(TEXT("voxel"), TEXT("add_node"),
                            TEXT("Add a node to a voxel graph, headless. 'node' resolves three ways: exact FVoxelNode struct name (VoxelNode_SampleHeightmap, VoxelTemplateNode_Subtract - bypasses menu filters), display name (\"Sample Heightmap\", \"Smooth Step\"), or a UVoxelFunctionLibrary function name (GetPosition2D, BreakVector2D). Output nodes are template-only and cannot be added. Returns node_id + pins. Follow with connect_pins / set_pin_default, then editor.save_packages."),
                            FMonolithActionHandler::CreateStatic(&HandleGraphAddNode),
                            FParamSchemaBuilder()
                                .Required(TEXT("asset_path"), TEXT("string"), TEXT("Voxel graph asset path"))
                                .Required(TEXT("node"), TEXT("string"), TEXT("Struct name, display name, or function name"))
                                .Optional(TEXT("x"), TEXT("number"), TEXT("Node X position"))
                                .Optional(TEXT("y"), TEXT("number"), TEXT("Node Y position"))
                                .Optional(TEXT("terminal_graph"), TEXT("string"), TEXT("Terminal graph name or GUID (defaults to main)"))
                                .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("connect_pins"),
                            TEXT("Connect an output pin to an input pin in a voxel graph via the graph schema (handles float<->buffer promotion and conversion nodes; an already-linked input is rewired, not errored). Link state is VERIFIED by read-back after the call - promotion may reconstruct nodes. Follow with editor.save_packages."),
                            FMonolithActionHandler::CreateStatic(&HandleGraphConnectPins),
                            FParamSchemaBuilder()
                                .Required(TEXT("asset_path"), TEXT("string"), TEXT("Voxel graph asset path"))
                                .Required(TEXT("from_node"), TEXT("string"), TEXT("Source node id (output side)"))
                                .Required(TEXT("from_pin"), TEXT("string"), TEXT("Source pin name"))
                                .Required(TEXT("to_node"), TEXT("string"), TEXT("Destination node id (input side)"))
                                .Required(TEXT("to_pin"), TEXT("string"), TEXT("Destination pin name"))
                                .Optional(TEXT("terminal_graph"), TEXT("string"), TEXT("Terminal graph name or GUID (defaults to main)"))
                                .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("remove_node"),
                            TEXT("Remove a node from a voxel graph (links broken first). Nodes that refuse deletion (Output Height) are left alone with an error. Follow with editor.save_packages."),
                            FMonolithActionHandler::CreateStatic(&HandleGraphRemoveNode),
                            FParamSchemaBuilder()
                                .Required(TEXT("asset_path"), TEXT("string"), TEXT("Voxel graph asset path"))
                                .Required(TEXT("node_id"), TEXT("string"), TEXT("Node id (from add_node/get_graph_data/search_nodes)"))
                                .Optional(TEXT("terminal_graph"), TEXT("string"), TEXT("Terminal graph name or GUID (defaults to main)"))
                                .Build());
}
