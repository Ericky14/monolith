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
            "Json",
            "JsonUtilities",
            "Voxel",
            "VoxelGraph",
            "VoxelGraphEditor"
        });
    }
}
