using System.IO;
using UnrealBuildTool;

// Hosts VST 3 instruments (any .vst3: Garritan's ARIA Player, sfizz, Pianoteq...). Steinberg's VST 3 SDK 3.8.1 (MIT,
// github.com/steinbergmedia) is compiled from source here: pluginterfaces and the hosting part of public.sdk, in sdk/.
// Only FPianoVst3Plugin is exported; the SDK's types stay inside this module.
public class PianoVst3 : ModuleRules
{
	public PianoVst3(ReadOnlyTargetRules Target) : base(Target)
	{
		// the SDK files compile on their own, without Unreal headers
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bEnableExceptions = true; // std::filesystem in the module loader
		bEnableObjCAutomaticReferenceCounting = true; // the SDK's Mac module loader requires it
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Off;

		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "sdk"));
		PrivateDefinitions.Add("_CRT_SECURE_NO_WARNINGS"); // the SDK's C string calls
		PublicDependencyModuleNames.Add("Core");

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.AddRange(new string[] { "Cocoa", "CoreFoundation" });
		}
	}
}
