using UnrealBuildTool;

public class PianoTrainerEditorTarget : TargetRules
{
	public PianoTrainerEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PianoTrainer");
	}
}
