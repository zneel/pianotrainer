using System.IO;
using UnrealBuildTool;

// lunasvg 3.5.0 + plutovg (MIT), static library built by ThirdParty/build_win64.bat / build_mac.sh.
public class LunaSVG : ModuleRules
{
	public LunaSVG(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string Root = Path.Combine(PluginDirectory, "ThirdParty", "LunaSVG");
		PublicSystemIncludePaths.Add(Path.Combine(Root, "include"));
		PublicDefinitions.Add("LUNASVG_BUILD_STATIC=1");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(Root, "Win64", "lunasvg.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(Path.Combine(Root, "Mac", "liblunasvg.a"));
		}
	}
}
