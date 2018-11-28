# NERD SCRIPTING LANGUAGE

_Nerd_ is a simple lisp-based scripting language that compiles to a simple byte-code for execution.  Macros and readers are handled so the core of the language is simple but the final language is more sophisticated.
Most of the power of the language is within the byte-code itself.  For example, even though the core language doesn't support tables, the byte-code interpreter does.  This allows us to write code that reads table syntax and generates
the byte-code to use it, thus adding table syntax to the final language.  My minimising the primitives, we keep the byte-code simple.

## Building

There are 2 ways to build the executable.  One is via Visual Studio 2017, and another is via a batch file.  The
executables will be found in the folder `_bin/<platform>_<configuration>_nerd`, where `<platform>` is currently `Win64` and `<configuration>` is either `Debug` or `Release`.
The code-base is platform agnostic except for the platform_win32.c file that you can port to other operating systems.

### Building via Visual Studio 2017

To build **nerd.exe** (the compiler and byte-code VM):

* Run **gen.bat** to generate the solutions and projects inside the **_build** folder.
* Run **edit.bat** to run Visual Studio 2017 with the solution open.
* Use Visual Studio as normal to build either _Debug_ or _Release_ builds.

### Building via a batch file

To build **nerd.exe** run **build.bat**.  This will set up your CLI environment ready for Visual Studio command line tools,
build the release version of **nerd.exe** using _msbuild_.

## Cleaning

All files generated by the build are placed in folders that start with an underscore.  You can run **clean.bat**, which
will delete all those folders.

## Installation

The build environment provides a **install.bat** file that will copy the release version of **nerd.exe** to the folder
determined by the environment variable **INSTALL_PATH**.  If **INSTALL_PATH** is not defined, the batch file will
warn you of this fact.  This folder should be included in your system's **PATH** variable so it can be found.

The file **data/system.n** is also copied to the installation path.  **nerd.exe.** will look for this file when it starts and execute it.  This file contains the REPL and the reader.