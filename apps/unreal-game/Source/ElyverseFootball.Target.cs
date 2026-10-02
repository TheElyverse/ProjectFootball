using UnrealBuildTool;

public class ElyverseFootballTarget : TargetRules
{
	public ElyverseFootballTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new[] { "ElyverseAdapter", "ElyverseFootball" });
	}
}
