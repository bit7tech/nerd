-- Nerd build script

rootdir = path.join(path.getdirectory(_SCRIPT), "..")

filter { "platforms:Win64" }
	system "Windows"
	architecture "x64"


-- Solution
solution "nerd"
	language "C"
	configurations { "Debug", "Release" }
	platforms { "Win64" }
	location "../_build"
    debugdir "../data"
    characterset "MBCS"

    -- Solution-wide defines
	defines {
		"_CRT_SECURE_NO_WARNINGS",
	}

    --linkoptions "/opt:ref"
    editandcontinue "off"

	configuration "Debug"
		defines { "_DEBUG" }
		flags { "FatalWarnings" }
		symbols "on"

	configuration "Release"
		defines { "NDEBUG" }
        flags { "FatalWarnings" }
		optimize "full"

	-- Projects
	project "nerd"
		targetdir "../_bin/%{cfg.platform}_%{cfg.buildcfg}_%{prj.name}"
		objdir "../_obj/%{cfg.platform}_%{cfg.buildcfg}_%{prj.name}"
        kind "ConsoleApp"
		files {
            "../src/**.c",
            "../src/**.h",
            "../README.md",
		}
        includedirs {
            "../src",
        }

        -- Libraries to link to (libraries only have release versions)
        links {
        }

        -- Defines to make
        defines {
        }

        -- Where to find the libs
        libdirs {
        }

        -- Debug only linking
        configuration "Debug"
            links {
            }

        -- Release only linking
        configuration "Release"
            links {
            }

        -- Uncomment this to copy contents of data directory next to the exe.
        -- Shouldn't need this since we can set the debug directory to the data directory.
        --
        -- postbuildcommands {
        --     "copy \"" .. path.translate(path.join(rootdir, "data", "*.*")) .. '" "' ..
        --         path.translate(path.join(rootdir, "_Bin", "%{cfg.platform}", "%{cfg.buildcfg}", "%{prj.name}")) .. '"'
        -- }

        -- Windows-only defines
		configuration "Win*"
			defines {
				"WIN32",
			}
			flags {
				"StaticRuntime",
				"NoMinimalRebuild",
				"NoIncrementalLink",
			}
            linkoptions {
                "/DEBUG:FULL"
            }

