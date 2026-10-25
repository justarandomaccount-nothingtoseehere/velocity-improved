## Requirements

- Visual Studio 2026 with the Desktop development with C++ workload
- MSVC v145 C++ x64/x86 build tools and a Windows 10/11 SDK
- x64 CPU with AVX2 support

FreeType is vendored under `cs2/project/external/xdraw/dependencies/freetype`;
there is no vcpkg manifest or dependency restore step.

## Build Guide

Open `cs2/velocity.slnx` in Visual Studio, select **Debug | x64** or
**Release | x64**, and choose **Build > Build Solution**. You can also open
`cs2/velocity.vcxproj` directly.

Alternatively, from a Developer PowerShell at the repository root:

```powershell
msbuild cs2\velocity.vcxproj /m /p:Configuration=Debug /p:Platform=x64
msbuild cs2\velocity.vcxproj /m /p:Configuration=Release /p:Platform=x64
```

Outputs are `cs2/bin/velocity-debug.dll` and `cs2/bin/velocity.dll`.
Intermediate files are isolated under `cs2/bin/intermediates/<Configuration>/`.
Use Visual Studio's Clean/Rebuild commands, or add `/t:Clean` or `/t:Rebuild`
to the corresponding MSBuild command.

Both configurations target C++20, x64 and AVX2 with the `v145` toolset. The
project is a plain header + translation-unit build: there are no C++20
modules, no precompiled header and no vcpkg manifest. Sources are listed
explicitly in `velocity.vcxproj`; a new `.cpp` must be added to its
`<ClCompile>` item group.

## Contributing

Contributions are welcome. If you want to help, open a pull request with a clear
description of what changed and why.

Match the style of the file you are editing. The de facto style uses tabs,
Allman braces, a space inside parentheses, `PascalCase` types and methods, and
`snake_case` members with an `m_` prefix. Files are lowercase `snake_case`
with `.hpp` headers and `.cpp` implementation units. Do not add comments to
new or edited code.

Bundled code under `project/external/` retains its upstream style and filenames.
Windows ABI declarations, engine-facing names in strings, and third-party API
names must retain their required spelling.

## Why

I was bored.
