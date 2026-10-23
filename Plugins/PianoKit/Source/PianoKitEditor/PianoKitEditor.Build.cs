using UnrealBuildTool;

// Editor-only tools for PianoKit: the PianoPatchBuilder commandlet that turns a sample manifest into a Fusion patch.
public class PianoKitEditor : ModuleRules
{
	public PianoKitEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"Json",
			"MaterialEditor",
		});
	}
}
