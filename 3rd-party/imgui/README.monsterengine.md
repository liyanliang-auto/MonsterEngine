# Dear ImGui dependency

This directory contains the source files used by MonsterEngine's existing UI
integration. The bundled header identifies the version as `1.93.0 WIP`
(`IMGUI_VERSION_NUM = 19292`).

Upstream: https://github.com/ocornut/imgui

License: [MIT](LICENSE.txt).

The original repository entry was an unresolved Git submodule without a
`.gitmodules` configuration. Required source/header files are now stored here
so a normal clone can build the same integration. No ImGui API upgrade is
performed; only trailing whitespace has been normalized.
