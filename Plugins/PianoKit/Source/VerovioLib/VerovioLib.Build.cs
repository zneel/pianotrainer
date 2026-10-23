using System.IO;
using UnrealBuildTool;

// Verovio 6.3.0 (LGPL-3.0), built by ThirdParty/build_win64.bat (Windows) or build_mac.sh (macOS).
// PianoKit resolves its C API at runtime (VerovioApi.cpp), so this module only stages the shared library
// and its data; nothing links against it and it can be swapped for another build.
public class VerovioLib : ModuleRules
{
	public VerovioLib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string Root = Path.Combine(PluginDirectory, "ThirdParty", "Verovio");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			RuntimeDependencies.Add(Path.Combine(Root, "Win64", "verovio.dll"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			RuntimeDependencies.Add(Path.Combine(Root, "Mac", "libverovio.dylib"));
		}

		// Music fonts and glyph tables, read by Verovio from disk at runtime.
		RuntimeDependencies.Add(Path.Combine(Root, "data", "..."), StagedFileType.NonUFS);
		// Leipzig as a font file (OFL), registered with lunasvg for the SMuFL glyphs Verovio writes as text.
		RuntimeDependencies.Add(Path.Combine(Root, "fonts", "Leipzig.ttf"), StagedFileType.NonUFS);
	}
}
