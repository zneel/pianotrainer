using UnrealBuildTool;

public class PianoKit : ModuleRules
{
	public PianoKit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MIDIDevice",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"Json",
			"VerovioLib",
			"LunaSVG",
			"AudioExtensions",
			"SignalProcessing",
			"MetasoundEngine",
			"MetasoundFrontend",
			"MetasoundGraphCore",
			"MetasoundStandardNodes",
			"Harmonix",
			"HarmonixDsp",
			"HarmonixMidi",
			"HarmonixMetasound",
		});

		PrivateDefinitions.AddRange(new string[]
		{
			"METASOUND_PLUGIN=PianoKit",
			"METASOUND_MODULE=PianoKit",
		});

		// sfizz 1.2.3 (BSD-2), built by ThirdParty/build_win64.bat / build_mac.sh; its C API is resolved at runtime
		// (SfizzApi.cpp), so it is only staged here.
		string Sfizz = System.IO.Path.Combine(PluginDirectory, "ThirdParty", "Sfizz");
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			RuntimeDependencies.Add(System.IO.Path.Combine(Sfizz, "Win64", "sfizz.dll"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			RuntimeDependencies.Add(System.IO.Path.Combine(Sfizz, "Mac", "libsfizz.dylib"));
		}
		RuntimeDependencies.Add(System.IO.Path.Combine(Sfizz, "LICENSE.txt"), StagedFileType.NonUFS);
		RuntimeDependencies.Add(System.IO.Path.Combine(Sfizz, "LICENSE.abseil.txt"), StagedFileType.NonUFS);

		// Salamander Grand Piano V3 (CC-BY 3.0): the SFZ, its tables and FLAC samples, read by sfizz from disk, plus
		// its licence and credit; shipped as loose files next to the app.
		RuntimeDependencies.Add(System.IO.Path.Combine(PluginDirectory, "ThirdParty", "Salamander", "..."), StagedFileType.NonUFS);
	}
}
