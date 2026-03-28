using UnrealBuildTool;
using System.IO;

public class MonolithMesh : ModuleRules
{
	public MonolithMesh(ReadOnlyTargetRules Target) : base(Target)
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
			"MonolithIndex",
			"SQLiteCore",
			"UnrealEd",
			"EditorSubsystem",
			"MeshDescription",
			"StaticMeshDescription",
			"MeshConversion",
			"PhysicsCore",
			"NavigationSystem",
			"RenderCore",
			"RHI",
			"EditorScriptingUtilities",
			"Json",
			"JsonUtilities",
			"Slate",
			"SlateCore",
			"AssetRegistry",
			"AssetTools",
			"MeshReductionInterface"
		});

		// Optional: GeometryScripting (Tier 5 mesh operations only)
		string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
		string GeometryScriptingPath = Path.Combine(EngineDir,
			"Plugins", "Runtime", "GeometryScripting");
		if (Directory.Exists(GeometryScriptingPath))
		{
			PrivateDependencyModuleNames.Add("GeometryScriptingCore");
			PrivateDependencyModuleNames.Add("GeometryFramework");
			PrivateDependencyModuleNames.Add("GeometryCore");
			PublicDefinitions.Add("WITH_GEOMETRYSCRIPT=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_GEOMETRYSCRIPT=0");
		}
	}
}
