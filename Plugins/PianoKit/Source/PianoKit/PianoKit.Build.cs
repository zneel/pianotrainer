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
			"PianoVst3", // VST 3 instruments
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

		// The piano samples are not shipped: Piano in 162 (Ivy Audio) may be used but not redistributed, so each
		// player keeps their own copy in an Instruments folder (UPianoPlayer::GetInstrumentFolders).
	}
}
