// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SkyBoxGenerator : ModuleRules
{
	public SkyBoxGenerator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "HeadMountedDisplay",
                "Slate",
                "SlateCore",
                "Renderer",
                "RenderCore",
                "RHI"
            }
            );

        if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Win32))
        {
            //PublicDefinitions.Add("GOOGLE_PROTOBUF_NO_RTTI=1");
            //PublicDefinitions.Add("GOOGLE_PROTOBUF_USE_UNALIGNED=0");
            //PublicDefinitions.Add("GPR_FORBID_UNREACHABLE_CODE=0");

            PublicIncludePaths.Add("D:/grpc_sdk/win64_ue426/include");
            PublicLibraryPaths.Add("D:/grpc_sdk/win64_ue426/lib");

            PublicAdditionalLibraries.Add("address_sorting.lib");
            PublicAdditionalLibraries.Add("cares.lib");
            PublicAdditionalLibraries.Add("gpr.lib");
            PublicAdditionalLibraries.Add("grpc.lib");
            PublicAdditionalLibraries.Add("grpc++.lib");
            PublicAdditionalLibraries.Add("grpc++_reflection.lib");
            PublicAdditionalLibraries.Add("libprotobuf.lib");
            PublicAdditionalLibraries.Add("upb.lib");
            PublicAdditionalLibraries.Add("re2.lib");

            //gRPC自带的库（不能用，链接不过）
            //PublicAdditionalLibraries.Add("crypto.lib");
            //PublicAdditionalLibraries.Add("ssl.lib");
            //PublicAdditionalLibraries.Add("zlibstatic.lib");

            //UE自带的库（编译gRPC时需要指定）
            //AddEngineThirdPartyPrivateStaticDependencies(Target, "CryptoPP");  //不需要
            AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");

            PublicAdditionalLibraries.Add("absl_hash.lib");
            PublicAdditionalLibraries.Add("absl_city.lib");
            PublicAdditionalLibraries.Add("absl_wyhash.lib");
            PublicAdditionalLibraries.Add("absl_raw_hash_set.lib");
            PublicAdditionalLibraries.Add("absl_hashtablez_sampler.lib");
            PublicAdditionalLibraries.Add("absl_exponential_biased.lib");
            PublicAdditionalLibraries.Add("absl_statusor.lib");
            PublicAdditionalLibraries.Add("absl_bad_variant_access.lib");
            PublicAdditionalLibraries.Add("absl_status.lib");
            PublicAdditionalLibraries.Add("absl_cord.lib");
            PublicAdditionalLibraries.Add("absl_str_format_internal.lib");
            PublicAdditionalLibraries.Add("absl_synchronization.lib");
            PublicAdditionalLibraries.Add("absl_stacktrace.lib");
            PublicAdditionalLibraries.Add("absl_symbolize.lib");
            PublicAdditionalLibraries.Add("absl_debugging_internal.lib");
            PublicAdditionalLibraries.Add("absl_demangle_internal.lib");
            PublicAdditionalLibraries.Add("absl_graphcycles_internal.lib");
            PublicAdditionalLibraries.Add("absl_malloc_internal.lib");
            PublicAdditionalLibraries.Add("absl_time.lib");
            PublicAdditionalLibraries.Add("absl_strings.lib");
            PublicAdditionalLibraries.Add("absl_throw_delegate.lib");
            PublicAdditionalLibraries.Add("absl_strings_internal.lib");
            PublicAdditionalLibraries.Add("absl_base.lib");
            PublicAdditionalLibraries.Add("absl_spinlock_wait.lib");
            PublicAdditionalLibraries.Add("absl_int128.lib");
            PublicAdditionalLibraries.Add("absl_civil_time.lib");
            PublicAdditionalLibraries.Add("absl_time_zone.lib");
            PublicAdditionalLibraries.Add("absl_bad_optional_access.lib");
            PublicAdditionalLibraries.Add("absl_raw_logging_internal.lib");
            PublicAdditionalLibraries.Add("absl_log_severity.lib");
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
        {
        }
    }
}
