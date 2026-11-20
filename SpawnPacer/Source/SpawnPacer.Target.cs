// P02 SpawnPacer - game target (standalone/packaged builds).

using UnrealBuildTool;

public class SpawnPacerTarget : TargetRules
{
	public SpawnPacerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("SpawnPacer");
	}
}
