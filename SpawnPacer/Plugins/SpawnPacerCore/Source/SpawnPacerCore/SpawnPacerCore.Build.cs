// SpawnPacerCore - the reusable runtime module (spawn scheduler, cost model, pool, reset contract, telemetry).
// It must not depend on any game module.

using UnrealBuildTool;

public class SpawnPacerCore : ModuleRules
{
	public SpawnPacerCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
