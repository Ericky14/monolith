using System.IO;
using UnrealBuildTool;

public class MonolithVoxel : ModuleRules
{
    public MonolithVoxel(ReadOnlyTargetRules Target) : base(Target)
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
            "UnrealEd",
            "AssetTools",   // IAssetTools::DuplicateAsset (duplicate_graph_template)
            "Json",
            "JsonUtilities",
            "VoxelCore",
            "VoxelCoreEditor", // FVoxelTransaction (headless graph authoring change tracking)
            "Voxel",
            "VoxelGraph",
            "VoxelGraphEditor"
        });

        // VP2 header-hygiene workaround: several public Voxel headers we need for stamp/layer
        // authoring (e.g. VoxelLayerBase.h) transitively #include headers that live in Voxel's
        // Private folder (VoxelStampTree.h, ...). Dependent modules only get Voxel's Public paths,
        // so add Voxel's Private dir to resolve them. Voxel is junctioned at Plugins/Voxel, i.e.
        // a sibling of this plugin (PluginDirectory = .../Plugins/Monolith).
        PrivateIncludePaths.Add(Path.Combine(PluginDirectory, "..", "Voxel", "Source", "Voxel", "Private"));
    }
}
