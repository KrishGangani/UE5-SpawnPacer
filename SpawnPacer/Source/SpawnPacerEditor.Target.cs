// P02 SpawnPacer - editor target.

using UnrealBuildTool;

public class SpawnPacerEditorTarget : TargetRules
{
	public SpawnPacerEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("SpawnPacer");
	}
}
