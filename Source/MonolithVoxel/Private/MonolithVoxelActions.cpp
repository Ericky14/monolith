#include "MonolithVoxelActions.h"
#include "MonolithAssetUtils.h"
#include "MonolithParamSchema.h"
#include "VoxelGraph.h"
#include "VoxelTerminalGraph.h"
#include "VoxelParameter.h"
#include "Surface/VoxelSmartSurfaceType.h"
#include "Surface/VoxelSmartSurfaceTypeGraph.h"
#include "Graphs/VoxelVolumeGraph.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace MonolithVoxelInternal
{
    /**
     * Load a VoxelGraph or SmartSurfaceType from asset_path.
     * SmartSurfaceType wraps a graph, so we unwrap to the underlying UVoxelGraph.
     */
    UVoxelGraph *LoadVoxelGraph(const TSharedPtr<FJsonObject> &Params, FString &OutAssetPath, FString &OutAssetType)
    {
        OutAssetPath = Params->GetStringField(TEXT("asset_path"));
        if (OutAssetPath.IsEmpty())
            return nullptr;

        // Try direct UVoxelGraph load first (covers VoxelVolumeGraph, SmartSurfaceTypeGraph, etc.)
        if (UVoxelGraph *Graph = FMonolithAssetUtils::LoadAssetByPath<UVoxelGraph>(OutAssetPath))
        {
            OutAssetType = Graph->GetClass()->GetName();
            return Graph;
        }

        // Try as SmartSurfaceType (which wraps a graph)
        if (UVoxelSmartSurfaceType *SST = FMonolithAssetUtils::LoadAssetByPath<UVoxelSmartSurfaceType>(OutAssetPath))
        {
            OutAssetType = TEXT("VoxelSmartSurfaceType");
            return SST->Graph;
        }

        return nullptr;
    }

    FString PinTypeToString(const FEdGraphPinType &PinType)
    {
        if (!PinType.PinSubCategoryObject.IsValid())
        {
            return PinType.PinCategory.ToString();
        }
        return PinType.PinCategory.ToString() + TEXT(":") + PinType.PinSubCategoryObject->GetName();
    }

    TSharedPtr<FJsonObject> SerializePin(const UEdGraphPin *Pin)
    {
        TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
        PinObj->SetStringField(TEXT("id"), Pin->PinId.ToString());
        PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
        PinObj->SetStringField(TEXT("direction"),
                               Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
        PinObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));

        if (!Pin->DefaultValue.IsEmpty())
        {
            PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
        }
        if (Pin->DefaultObject)
        {
            PinObj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
        }

        TArray<TSharedPtr<FJsonValue>> ConnArr;
        for (const UEdGraphPin *Linked : Pin->LinkedTo)
        {
            if (!Linked || !Linked->GetOwningNode())
                continue;
            FString ConnId = FString::Printf(TEXT("%s.%s"),
                                             *Linked->GetOwningNode()->GetName(),
                                             *Linked->PinName.ToString());
            ConnArr.Add(MakeShared<FJsonValueString>(ConnId));
        }
        PinObj->SetArrayField(TEXT("connected_to"), ConnArr);
        return PinObj;
    }

    TSharedPtr<FJsonObject> SerializeNode(UEdGraphNode *Node)
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

        TArray<TSharedPtr<FJsonValue>> PinsArr;
        for (const UEdGraphPin *Pin : Node->Pins)
        {
            if (!Pin || Pin->bHidden)
                continue;
            PinsArr.Add(MakeShared<FJsonValueObject>(SerializePin(Pin)));
        }
        NObj->SetArrayField(TEXT("pins"), PinsArr);

        return NObj;
    }
}

// --- Registration ---

void FMonolithVoxelActions::RegisterActions()
{
    FMonolithToolRegistry &Registry = FMonolithToolRegistry::Get();

    Registry.RegisterAction(TEXT("voxel"), TEXT("get_graph_info"),
                            TEXT("Get overview of a Voxel graph asset: type, terminal graphs, parameter count, base graph chain. Works with VoxelVolumeGraph, VoxelSmartSurfaceTypeGraph, and VoxelSmartSurfaceType assets."),
                            FMonolithActionHandler::CreateStatic(&HandleGetGraphInfo),
                            FParamSchemaBuilder()
                                .Required(TEXT("asset_path"), TEXT("string"), TEXT("Voxel graph or SmartSurfaceType asset path"))
                                .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("list_terminal_graphs"),
                            TEXT("List all terminal graphs (sub-graphs) in a Voxel graph asset with their GUIDs, display names, and node counts."),
                            FMonolithActionHandler::CreateStatic(&HandleListTerminalGraphs),
                            FParamSchemaBuilder()
                                .Required(TEXT("asset_path"), TEXT("string"), TEXT("Voxel graph asset path"))
                                .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("get_graph_data"),
                            TEXT("Get full node and pin data for a terminal graph. Returns all nodes with their pins, connections, and default values."),
                            FMonolithActionHandler::CreateStatic(&HandleGetGraphData),
                            FParamSchemaBuilder()
                                .Required(TEXT("asset_path"), TEXT("string"), TEXT("Voxel graph asset path"))
                                .Optional(TEXT("terminal_graph"), TEXT("string"), TEXT("Terminal graph name or GUID (defaults to main graph)"))
                                .Optional(TEXT("node_class_filter"), TEXT("string"), TEXT("Only include nodes whose class contains this substring"))
                                .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("get_graph_summary"),
                            TEXT("Get lightweight summary of a terminal graph: node IDs, classes, titles, and exec connections only."),
                            FMonolithActionHandler::CreateStatic(&HandleGetGraphSummary),
                            FParamSchemaBuilder()
                                .Required(TEXT("asset_path"), TEXT("string"), TEXT("Voxel graph asset path"))
                                .Optional(TEXT("terminal_graph"), TEXT("string"), TEXT("Terminal graph name or GUID (defaults to main graph)"))
                                .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("get_parameters"),
                            TEXT("Get all parameters defined on a Voxel graph with their names, types, categories, and inheritance info."),
                            FMonolithActionHandler::CreateStatic(&HandleGetParameters),
                            FParamSchemaBuilder()
                                .Required(TEXT("asset_path"), TEXT("string"), TEXT("Voxel graph asset path"))
                                .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("search_nodes"),
                            TEXT("Search for nodes in a Voxel graph by title, class name, or pin name. Searches all terminal graphs."),
                            FMonolithActionHandler::CreateStatic(&HandleSearchNodes),
                            FParamSchemaBuilder()
                                .Required(TEXT("asset_path"), TEXT("string"), TEXT("Voxel graph asset path"))
                                .Required(TEXT("query"), TEXT("string"), TEXT("Search string to match against node titles, classes, and pin names"))
                                .Build());

    Registry.RegisterAction(TEXT("voxel"), TEXT("get_node_details"),
                            TEXT("Get full pin dump for a single node by node_id. Searches all terminal graphs."),
                            FMonolithActionHandler::CreateStatic(&HandleGetNodeDetails),
                            FParamSchemaBuilder()
                                .Required(TEXT("asset_path"), TEXT("string"), TEXT("Voxel graph asset path"))
                                .Required(TEXT("node_id"), TEXT("string"), TEXT("Node ID (from get_graph_data or search_nodes)"))
                                .Build());
}

// --- Helpers ---

#if WITH_EDITOR
static UVoxelTerminalGraph *FindTerminalGraph(UVoxelGraph *VoxelGraph, const FString &TerminalGraphRef)
{
    // Default to main terminal graph
    if (TerminalGraphRef.IsEmpty())
    {
        if (VoxelGraph->HasMainTerminalGraph())
        {
            return &VoxelGraph->GetMainTerminalGraph();
        }
        // Fall back to first available
        TVoxelSet<FGuid> Guids = VoxelGraph->GetTerminalGraphs();
        for (const FGuid &Guid : Guids)
        {
            if (UVoxelTerminalGraph *TG = VoxelGraph->FindTerminalGraph(Guid))
            {
                return TG;
            }
        }
        return nullptr;
    }

    // Try as GUID first
    FGuid ParsedGuid;
    if (FGuid::Parse(TerminalGraphRef, ParsedGuid))
    {
        return VoxelGraph->FindTerminalGraph(ParsedGuid);
    }

    // Try by display name
    TVoxelSet<FGuid> Guids = VoxelGraph->GetTerminalGraphs();
    for (const FGuid &Guid : Guids)
    {
        UVoxelTerminalGraph *TG = VoxelGraph->FindTerminalGraph(Guid);
        if (TG && TG->GetDisplayName() == TerminalGraphRef)
        {
            return TG;
        }
    }

    return nullptr;
}
#endif

// --- get_graph_info ---

FMonolithActionResult FMonolithVoxelActions::HandleGetGraphInfo(const TSharedPtr<FJsonObject> &Params)
{
#if WITH_EDITOR
    FString AssetPath, AssetType;
    UVoxelGraph *VoxelGraph = MonolithVoxelInternal::LoadVoxelGraph(Params, AssetPath, AssetType);
    if (!VoxelGraph)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel graph not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("asset_path"), AssetPath);
    Root->SetStringField(TEXT("asset_type"), AssetType);
    Root->SetStringField(TEXT("graph_type"), VoxelGraph->GetGraphTypeName());

    if (!VoxelGraph->Description.IsEmpty())
    {
        Root->SetStringField(TEXT("description"), VoxelGraph->Description);
    }
    if (!VoxelGraph->Category.IsEmpty())
    {
        Root->SetStringField(TEXT("category"), VoxelGraph->Category);
    }

    // Terminal graphs
    TVoxelSet<FGuid> Guids = VoxelGraph->GetTerminalGraphs();
    Root->SetNumberField(TEXT("terminal_graph_count"), Guids.Num());

    // Parameters
    Root->SetNumberField(TEXT("parameter_count"), VoxelGraph->NumParameters());

    // Base graph chain
    auto BaseGraphs = VoxelGraph->GetBaseGraphs();
    if (BaseGraphs.Num() > 1) // First is self
    {
        TArray<TSharedPtr<FJsonValue>> BaseArr;
        for (int32 i = 1; i < BaseGraphs.Num(); i++)
        {
            BaseArr.Add(MakeShared<FJsonValueString>(BaseGraphs[i]->GetPathName()));
        }
        Root->SetArrayField(TEXT("base_graphs"), BaseArr);
    }

    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Voxel graph inspection requires editor build"));
#endif
}

// --- list_terminal_graphs ---

FMonolithActionResult FMonolithVoxelActions::HandleListTerminalGraphs(const TSharedPtr<FJsonObject> &Params)
{
#if WITH_EDITOR
    FString AssetPath, AssetType;
    UVoxelGraph *VoxelGraph = MonolithVoxelInternal::LoadVoxelGraph(Params, AssetPath, AssetType);
    if (!VoxelGraph)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel graph not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("asset_path"), AssetPath);

    TArray<TSharedPtr<FJsonValue>> GraphsArr;
    TVoxelSet<FGuid> Guids = VoxelGraph->GetTerminalGraphs();

    for (const FGuid &Guid : Guids)
    {
        UVoxelTerminalGraph *TG = VoxelGraph->FindTerminalGraph(Guid);
        if (!TG)
            continue;

        TSharedPtr<FJsonObject> GObj = MakeShared<FJsonObject>();
        GObj->SetStringField(TEXT("guid"), Guid.ToString());
        GObj->SetStringField(TEXT("display_name"), TG->GetDisplayName());

        UEdGraph &EdGraph = TG->GetEdGraph();
        GObj->SetNumberField(TEXT("node_count"), EdGraph.Nodes.Num());

        // Mark main graph
        if (VoxelGraph->HasMainTerminalGraph() &&
            &VoxelGraph->GetMainTerminalGraph() == TG)
        {
            GObj->SetBoolField(TEXT("is_main"), true);
        }

        GraphsArr.Add(MakeShared<FJsonValueObject>(GObj));
    }

    Root->SetArrayField(TEXT("terminal_graphs"), GraphsArr);
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Voxel graph inspection requires editor build"));
#endif
}

// --- get_graph_data ---

FMonolithActionResult FMonolithVoxelActions::HandleGetGraphData(const TSharedPtr<FJsonObject> &Params)
{
#if WITH_EDITOR
    FString AssetPath, AssetType;
    UVoxelGraph *VoxelGraph = MonolithVoxelInternal::LoadVoxelGraph(Params, AssetPath, AssetType);
    if (!VoxelGraph)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel graph not found: %s"), *AssetPath));
    }

    FString TerminalRef = Params->GetStringField(TEXT("terminal_graph"));
    FString ClassFilter = Params->GetStringField(TEXT("node_class_filter"));

    UVoxelTerminalGraph *TG = FindTerminalGraph(VoxelGraph, TerminalRef);
    if (!TG)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Terminal graph not found: %s"), *TerminalRef));
    }

    UEdGraph &EdGraph = TG->GetEdGraph();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("asset_path"), AssetPath);
    Root->SetStringField(TEXT("terminal_graph"), TG->GetDisplayName());
    Root->SetNumberField(TEXT("node_count"), EdGraph.Nodes.Num());

    TArray<TSharedPtr<FJsonValue>> NodesArr;
    for (UEdGraphNode *Node : EdGraph.Nodes)
    {
        if (!Node)
            continue;
        if (!ClassFilter.IsEmpty() && !Node->GetClass()->GetName().Contains(ClassFilter))
            continue;
        NodesArr.Add(MakeShared<FJsonValueObject>(MonolithVoxelInternal::SerializeNode(Node)));
    }
    Root->SetArrayField(TEXT("nodes"), NodesArr);

    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Voxel graph inspection requires editor build"));
#endif
}

// --- get_graph_summary ---

FMonolithActionResult FMonolithVoxelActions::HandleGetGraphSummary(const TSharedPtr<FJsonObject> &Params)
{
#if WITH_EDITOR
    FString AssetPath, AssetType;
    UVoxelGraph *VoxelGraph = MonolithVoxelInternal::LoadVoxelGraph(Params, AssetPath, AssetType);
    if (!VoxelGraph)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel graph not found: %s"), *AssetPath));
    }

    FString TerminalRef = Params->GetStringField(TEXT("terminal_graph"));
    UVoxelTerminalGraph *TG = FindTerminalGraph(VoxelGraph, TerminalRef);
    if (!TG)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Terminal graph not found: %s"), *TerminalRef));
    }

    UEdGraph &EdGraph = TG->GetEdGraph();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("asset_path"), AssetPath);
    Root->SetStringField(TEXT("terminal_graph"), TG->GetDisplayName());

    TArray<TSharedPtr<FJsonValue>> NodesArr;
    for (UEdGraphNode *Node : EdGraph.Nodes)
    {
        if (!Node)
            continue;

        TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
        NObj->SetStringField(TEXT("id"), Node->GetName());
        NObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        NObj->SetStringField(TEXT("title"),
                             Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

        if (!Node->NodeComment.IsEmpty())
        {
            NObj->SetStringField(TEXT("comment"), Node->NodeComment);
        }

        // Only include connections (not full pin data)
        TArray<TSharedPtr<FJsonValue>> ConnArr;
        for (const UEdGraphPin *Pin : Node->Pins)
        {
            if (!Pin || Pin->bHidden || Pin->Direction != EGPD_Output)
                continue;
            for (const UEdGraphPin *Linked : Pin->LinkedTo)
            {
                if (!Linked || !Linked->GetOwningNode())
                    continue;
                TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
                ConnObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
                ConnObj->SetStringField(TEXT("to_node"), Linked->GetOwningNode()->GetName());
                ConnObj->SetStringField(TEXT("to_pin"), Linked->PinName.ToString());
                ConnArr.Add(MakeShared<FJsonValueObject>(ConnObj));
            }
        }
        if (ConnArr.Num() > 0)
        {
            NObj->SetArrayField(TEXT("connections"), ConnArr);
        }

        NodesArr.Add(MakeShared<FJsonValueObject>(NObj));
    }
    Root->SetArrayField(TEXT("nodes"), NodesArr);

    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Voxel graph inspection requires editor build"));
#endif
}

// --- get_parameters ---

FMonolithActionResult FMonolithVoxelActions::HandleGetParameters(const TSharedPtr<FJsonObject> &Params)
{
#if WITH_EDITOR
    FString AssetPath, AssetType;
    UVoxelGraph *VoxelGraph = MonolithVoxelInternal::LoadVoxelGraph(Params, AssetPath, AssetType);
    if (!VoxelGraph)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel graph not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("asset_path"), AssetPath);

    TArray<TSharedPtr<FJsonValue>> ParamsArr;
    VoxelGraph->ForeachParameter([&](const FGuid &Guid, const FVoxelParameter &Param)
                                 {
		TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
		PObj->SetStringField(TEXT("guid"), Guid.ToString());
		PObj->SetStringField(TEXT("name"), Param.Name.ToString());
		PObj->SetStringField(TEXT("type"), Param.Type.ToString());
		PObj->SetBoolField(TEXT("inherited"), VoxelGraph->IsInheritedParameter(Guid));

		if (!Param.Category.IsEmpty())
		{
			PObj->SetStringField(TEXT("category"), Param.Category);
		}
		if (!Param.Description.IsEmpty())
		{
			PObj->SetStringField(TEXT("description"), Param.Description);
		}

		ParamsArr.Add(MakeShared<FJsonValueObject>(PObj)); });

    Root->SetArrayField(TEXT("parameters"), ParamsArr);
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Voxel graph inspection requires editor build"));
#endif
}

// --- search_nodes ---

FMonolithActionResult FMonolithVoxelActions::HandleSearchNodes(const TSharedPtr<FJsonObject> &Params)
{
#if WITH_EDITOR
    FString AssetPath, AssetType;
    UVoxelGraph *VoxelGraph = MonolithVoxelInternal::LoadVoxelGraph(Params, AssetPath, AssetType);
    if (!VoxelGraph)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel graph not found: %s"), *AssetPath));
    }

    FString Query = Params->GetStringField(TEXT("query"));
    if (Query.IsEmpty())
    {
        return FMonolithActionResult::Error(TEXT("query parameter is required"));
    }

    FString QueryLower = Query.ToLower();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("asset_path"), AssetPath);

    TArray<TSharedPtr<FJsonValue>> ResultsArr;
    TVoxelSet<FGuid> Guids = VoxelGraph->GetTerminalGraphs();

    for (const FGuid &Guid : Guids)
    {
        UVoxelTerminalGraph *TG = VoxelGraph->FindTerminalGraph(Guid);
        if (!TG)
            continue;

        UEdGraph &EdGraph = TG->GetEdGraph();
        FString TGName = TG->GetDisplayName();

        for (UEdGraphNode *Node : EdGraph.Nodes)
        {
            if (!Node)
                continue;

            FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
            FString ClassName = Node->GetClass()->GetName();

            bool bMatch = Title.ToLower().Contains(QueryLower) ||
                          ClassName.ToLower().Contains(QueryLower);

            // Also search pin names and default values
            if (!bMatch)
            {
                for (const UEdGraphPin *Pin : Node->Pins)
                {
                    if (!Pin || Pin->bHidden)
                        continue;
                    if (Pin->PinName.ToString().ToLower().Contains(QueryLower) ||
                        Pin->DefaultValue.ToLower().Contains(QueryLower) ||
                        (Pin->DefaultObject && Pin->DefaultObject->GetName().ToLower().Contains(QueryLower)))
                    {
                        bMatch = true;
                        break;
                    }
                }
            }

            if (bMatch)
            {
                TSharedPtr<FJsonObject> NObj = MonolithVoxelInternal::SerializeNode(Node);
                NObj->SetStringField(TEXT("terminal_graph"), TGName);
                ResultsArr.Add(MakeShared<FJsonValueObject>(NObj));
            }
        }
    }

    Root->SetArrayField(TEXT("results"), ResultsArr);
    Root->SetNumberField(TEXT("match_count"), ResultsArr.Num());
    return FMonolithActionResult::Success(Root);
#else
    return FMonolithActionResult::Error(TEXT("Voxel graph inspection requires editor build"));
#endif
}

// --- get_node_details ---

FMonolithActionResult FMonolithVoxelActions::HandleGetNodeDetails(const TSharedPtr<FJsonObject> &Params)
{
#if WITH_EDITOR
    FString AssetPath, AssetType;
    UVoxelGraph *VoxelGraph = MonolithVoxelInternal::LoadVoxelGraph(Params, AssetPath, AssetType);
    if (!VoxelGraph)
    {
        return FMonolithActionResult::Error(FString::Printf(TEXT("Voxel graph not found: %s"), *AssetPath));
    }

    FString NodeId = Params->GetStringField(TEXT("node_id"));
    if (NodeId.IsEmpty())
    {
        return FMonolithActionResult::Error(TEXT("node_id parameter is required"));
    }

    // Search all terminal graphs for the node
    TVoxelSet<FGuid> Guids = VoxelGraph->GetTerminalGraphs();
    for (const FGuid &Guid : Guids)
    {
        UVoxelTerminalGraph *TG = VoxelGraph->FindTerminalGraph(Guid);
        if (!TG)
            continue;

        UEdGraph &EdGraph = TG->GetEdGraph();
        for (UEdGraphNode *Node : EdGraph.Nodes)
        {
            if (Node && Node->GetName() == NodeId)
            {
                TSharedPtr<FJsonObject> Root = MonolithVoxelInternal::SerializeNode(Node);
                Root->SetStringField(TEXT("asset_path"), AssetPath);
                Root->SetStringField(TEXT("terminal_graph"), TG->GetDisplayName());
                return FMonolithActionResult::Success(Root);
            }
        }
    }

    return FMonolithActionResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));
#else
    return FMonolithActionResult::Error(TEXT("Voxel graph inspection requires editor build"));
#endif
}
