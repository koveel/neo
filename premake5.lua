workspace "Neo"
	architecture "x86_64"
	startproject "neoc"

	configurations
	{
		"Debug",
		"Release",
	}
	
	flags
	{
		"MultiProcessorCompile"
	}

outputdir = "%{cfg.buildcfg}"
llvm_path = "C:/dev/llvm-project"

project "neoc"
	location "neoc"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"
	
	pchheader "pch.h"
	pchsource "neoc/src/pch.cpp"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp",
	}	

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
	}

	includedirs
	{
		"%{prj.name}/src",
		"%{llvm_path}/llvm/include",
		"%{llvm_path}/build/include",
	}	

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		defines "NEOC_DEBUG"
		libdirs "%{llvm_path}/build/Debug/lib"
		links   {
			"Ws2_32.lib",
			"%{llvm_path}/build/Debug/lib/**.lib"
		}
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		defines "NEOC_RELEASE"
		libdirs "%{llvm_path}/build/Release/lib"
		links   {
			"Ws2_32.lib",
			"%{llvm_path}/build/Release/lib/**.lib"
		}
		optimize "on"

project "neo"
	location "neo"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"
	
	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp",
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
	}

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		defines "NEO_DEBUG"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		defines "NEO_RELEASE"
		optimize "on"