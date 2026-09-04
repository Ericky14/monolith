using UnrealBuildTool;

// Monolith's Dataflow node library.
//
// Exists because the engine ships NO node for combining two Dataflow meshes:
// searched Dataflow/DataflowNodes, MeshResizing, ChaosClothAsset and
// ChaosOutfitAsset — there is selection, transform and sampling, but no
// append/merge/union. The MeshResizing garment-refit pipeline needs one, because
// MeshWrap conforms the WHOLE source topology and an assembled MetaHuman body
// carries no head (the face is a separate mesh). Without a merge, Manny's head
// has no target to project onto and the RBF sample set gets poisoned.
public class MonolithDataflow : ModuleRules
{
	public MonolithDataflow(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"MonolithCore",
			// Dataflow node/graph plumbing. DataflowCore owns FDataflowNode and the
			// DATAFLOW_NODE_* macros; DataflowEngine owns UDataflowMesh.
			"DataflowCore",
			"DataflowEngine",
			// FDynamicMesh3 + FDynamicMeshEditor::AppendMesh do the actual merge.
			"GeometryCore",
			"DynamicMesh"
		});
	}
}
