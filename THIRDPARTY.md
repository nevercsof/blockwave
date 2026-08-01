# Third-party code and tools

Everything linked into BLOCKWAVE must be GPLv3-compatible. Record every addition here (CLAUDE.md legal rule).

## Linked / compiled in

| Component | Version | License | Use | Compatibility |
|---|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | 8.0.8 (pinned in CMakeLists.txt) | GPLv3 (open-source option of dual license) | Framework: plugin wrappers, DSP utilities, GUI | ✅ same license |

## Build/QA tools (not linked into the binary)

| Tool | License | Use |
|---|---|---|
| [pluginval](https://github.com/Tracktion/pluginval) | GPLv3 | Plugin validation, strictness 10, local + CI |
| CMake, Ninja | BSD-3 / Apache-2.0 | Build system |
| GitHub Actions runners | — | Windows x64 CI build + validation |

No Mojang/Minecraft assets, fonts, textures, or coined names anywhere in this repository (CLAUDE.md brand rule).
