workspace "joy"
  configurations { "debug", "release" }

project "joy"
  kind "ConsoleApp"
  language "C++"
  targetdir "%{cfg.buildcfg}/bin"

  files { "src/**.hpp", "src/**.cpp" }

  includedirs { "/usr/include/libevdev-1.0/" }
  links { "udev", "evdev" }

  warnings "Extra"

  filter "configurations:debug"
    defines { "DEBUG" }
    symbols "On"
    warnings "Extra"

  filter "configurations:release"
    defines { "NDEBUG" }
    optimize "On"

  filter "toolset:gcc"
    buildoptions { "-std=c++17" }

