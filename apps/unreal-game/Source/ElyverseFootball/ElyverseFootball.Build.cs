using UnrealBuildTool;

public class ElyverseFootball : ModuleRules
{
	public ElyverseFootball(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new[] { "ElyverseAdapter", "WebBrowser", "Json" });
	}
}
