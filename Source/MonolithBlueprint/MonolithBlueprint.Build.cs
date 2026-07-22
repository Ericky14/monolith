using UnrealBuildTool;

public class MonolithBlueprint : ModuleRules
{
	public MonolithBlueprint(ReadOnlyTargetRules Target) : base(Target)
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
			"BlueprintGraph",
			"BlueprintEditorLibrary",
			"SubobjectDataInterface",
			"Kismet",
			"KismetCompiler",
			"EditorScriptingUtilities",
			"EnhancedInput",
			// UK2Node_EnhancedInputAction (blueprint.add_enhanced_input_event). Editor/UncookedOnly
			// module shipped with the always-enabled EnhancedInput plugin; MonolithBlueprint is
			// itself an editor module, so depending on an UncookedOnly module is release-safe.
			"InputBlueprintNodes",
			"Json",
			"JsonUtilities"
			// (Historical: StructUtils was added here by PR #40 but is deprecated
			// since UE 5.5 — FInstancedStruct relocated into CoreUObject and resolves
			// transparently via the existing CoreUObject Public dep above.)
		});
	}
}
