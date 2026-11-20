// P02 SpawnPacer - primary game module: demo workload and benchmark harness only.
// The reusable system lives in the SpawnPacerCore plugin; this module depends on it, never the reverse.

using UnrealBuildTool;

public class SpawnPacer : ModuleRules
{
	public SpawnPacer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		// SpawnPacerCore is private for now: no public header of this module includes a plugin header yet.
		// It becomes public in the step where a public header first does (rule in 05_Line_By_Line_Code_Explanation.md A3).
		PrivateDependencyModuleNames.AddRange(new string[] { "SpawnPacerCore" });
	}
}
