<!-- SPDX-FileCopyrightText: NONE -->
<!-- SPDX-License-Identifier: CC0-1.0 -->

# Repository guide

## Project

Tremotesf is a desktop GUI client for Transmission's `transmission-daemon`, written in
C++23 with Qt 6 Widgets. It supports Windows, Linux/FreeBSD and macOS. The application
target and executable are named `tremotesf`; shared application code lives in the
`tremotesf_objects` object library. Do not infer Kodi integration from the checkout
directory name: inspect the relevant implementation before assuming such a feature.

## Code map

- `src/startup/`: application entry point (`main.cpp`), command-line parsing and
  platform-specific startup handling.
- `src/rpc/`: Transmission RPC, HTTP request routing, connection/server settings,
  torrents, trackers and peers. Start with `rpc.h`, `rpc.cpp` and `requestrouter.cpp`.
- `src/ui/screens/`: windows and dialogs; `mainwindow/` contains the main view,
  view model, torrent models and filters.
- `src/ui/itemmodels/`, `src/ui/widgets/`: shared Qt models, delegates and widgets.
- `src/coroutines/`: asynchronous Qt integration and coroutine lifetime management.
- `src/ipc/`: communication between application instances, using D-Bus or sockets.
- `src/log/`: logging and formatters. General parsing, settings and file utilities
  are directly under `src/`.
- `src/*_test.cpp` and nested `*_test.cpp`: Qt Test suites alongside implementation.
  Fixtures live in `src/rpc/test-data/` and `src/test-torrents/`.
- `translations/`: Qt Linguist `.ts` files; `data/`: icons, desktop metadata and gettext files.
- `cmake/`, `CMakePresets.json`, `vcpkg*.json`, `vcpkg-overlay-triplets/`: build settings
  and dependencies. `packaging/` and `.github/workflows/` cover packages and CI.
- `src/3rdparty/cpp-httplib/`: third-party Git submodule for HTTP tests; avoid editing
  vendored code unless the task specifically requires it.

## Build and run tests

Run commands from the repository root. Use `CMakeLists.txt`, `cmake/CommonOptions.cmake`
and `CMakePresets.json` as the build source of truth: README examples currently use
obsolete preset names and understate the CMake requirement.

Requirements include CMake 3.31+, Ninja, a compiler supporting the project's C++23
features, Qt 6.8+ (including LinguistTools and Test for tests), fmt 10.1+, cxxopts 3.2.1+
and KWidgetsAddons 6.11+. Linux/FreeBSD also require libpsl, Qt D-Bus, KWindowSystem
and gettext. Windows/macOS additionally use Qt SVG and GuiPrivate.

For Windows x64, use an initialized Visual Studio x64 developer shell with MSVC,
Windows 11 SDK, Ninja and `VCPKG_ROOT` pointing to an existing vcpkg installation:

```powershell
cmake --preset windows-x86_64-msvc-vcpkg -D BUILD_TESTING=ON
cmake --build --preset windows-x86_64-msvc-vcpkg-debug
ctest --preset windows-x86_64-msvc-vcpkg-debug
```

The Windows ARM64 and clang-cl alternatives are listed in `CMakePresets.json`.
vcpkg configuration may download and build substantial dependencies. These presets
use Ninja Multi-Config; keep the configuration consistent across build and test commands.

With system dependencies installed (for example on Linux/FreeBSD):

```sh
cmake --preset base -D BUILD_TESTING=ON
cmake --build --preset base-debug
ctest --preset base-debug
```

Preset build directories are `out/build/<configure-preset>`. To run a relevant subset,
append a CTest regex, e.g. `ctest --preset base-debug -R 'pathutils_test|tracker_test'`.
Run tests through CTest so fixture working directories are applied correctly.
`BUILD_TESTING` defaults to ON through CTest. `TREMOTESF_WITH_HTTPLIB` accepts `auto`,
`system`, `bundled`, or `none`; `none` omits `requestrouter_test`, so report that gap
when using it. For bundled httplib, initialize the submodule if needed with
`git submodule update --init --recursive`. `TREMOTESF_ASAN=ON` enables supported
AddressSanitizer configurations; see `cmake/CommonOptions.cmake` for platform limits.

## Editing conventions

- Follow nearby code and `.clang-format`: four spaces, no tabs, 120-column limit,
  attached braces, indented namespaces and left-aligned pointer/reference symbols.
  Include sorting is disabled. Use PascalCase types, camelCase methods and `m`-prefixed
  members as in the surrounding implementation.
- Preserve Qt ownership, signal/slot patterns and coroutine scope/lifetime conventions.
  Keep network operations asynchronous and UI work on the appropriate Qt thread.
- Keep platform-specific behavior in the existing suffixed files and platform guards;
  account for Windows, freedesktop Unix and macOS when changing shared code.
- Register new sources and test targets in `src/CMakeLists.txt`; Qt MOC/RCC generation
  is handled by CMake. Follow existing Qt Test classes, assertions and MOC includes.
- Format changed C++ files with a compatible clang-format. `clang-format-all.sh`
  formats broadly and excludes third-party code and `recoloringsvgiconengine.*`;
  avoid unrelated formatting changes. `.clang-tidy` supplies static-analysis rules.
- Wrap user-visible UI text in the existing Qt translation mechanism. Translations
  are managed through Transifex; avoid unrelated translation regeneration.
- Preserve SPDX headers and licensing conventions (`REUSE.toml`, `LICENSES/`). CI
  runs `reuse lint`; new files need appropriate license metadata.
- Keep generated files and build/dependency output in ignored locations such as
  `out/`, `build/` and `vcpkg-installed/`.

## Verification

Build the affected target and run relevant existing tests for behavior changes; add
regression coverage for bugs where practical. Use the full CTest suite for changes
spanning shared RPC, settings, coroutines or model infrastructure. For UI changes,
also check the affected interaction visually when a runnable build is available.
Report actual checks and any missing tools/dependencies; do not claim unrun tests passed.
If Git status fails because submodule metadata is inaccessible, inspect the parent
checkout with `git status --short --ignore-submodules=all` and report that limitation.
