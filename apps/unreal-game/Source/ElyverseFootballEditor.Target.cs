using UnrealBuildTool;

public class ElyverseFootballEditorTarget : TargetRules
{
	public ElyverseFootballEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new[] { "ElyverseAdapter", "ElyverseFootball" });
	}
}
