using System.IO;
using UnrealBuildTool;

public class PianoTrainer : ModuleRules
{
	public PianoTrainer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"ApplicationCore",
			"PianoKit",
			"AudioMixer",
			"ProceduralMeshComponent",
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("comdlg32.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.AddRange(new string[] { "AppKit", "UniformTypeIdentifiers" });
		}

		// Sample scores, readable from disk in packaged builds.
		RuntimeDependencies.Add(Path.Combine(ModuleDirectory, "..", "..", "Scores", "..."), StagedFileType.NonUFS);
	}
}
