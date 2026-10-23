using UnrealBuildTool;

public class PianoTrainerTarget : TargetRules
{
	public PianoTrainerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PianoTrainer");
	}
}
