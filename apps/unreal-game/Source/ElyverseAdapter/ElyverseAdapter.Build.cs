using System.IO;
using UnrealBuildTool;

// The adapter between the simulation core and Unreal (docs/implementation-plan.md
// section 12). It compiles libs/sim-core from source with Unreal's toolchain, because
// on Linux Unreal uses its own clang and libc++ while CMake uses the system compiler
// and libstdc++. sim-core stays private to this module: other modules only see the
// Unreal-friendly DTOs and functions under Public/.
public class ElyverseAdapter : ModuleRules
{
	public ElyverseAdapter(ReadOnlyTargetRules Target) : base(Target)
	{
		CppStandard = CppStandardVersion.Cpp23;
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		// sim-core reports overflow and invalid input with exceptions.
		bEnableExceptions = true;

		string SimCoreDirectory = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../libs/sim-core"));
		PrivateIncludePaths.Add(Path.Combine(SimCoreDirectory, "include"));
		PrivateIncludePaths.Add(Path.Combine(SimCoreDirectory, "src"));

		PublicDependencyModuleNames.Add("Core");
	}
}
