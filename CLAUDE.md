# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and run

### Main plugin build
The project expects xmake to be configured explicitly before building.

- Configure xmake for the normal release build used in this repo:
  - `xmake f --qt="/home/haiku/program/qt" --arch=arm64-v8a --toolchain=zig --cross=aarch64-linux-gnu.2.27 -m release -vD`
- Build after configuration:
  - `xmake`

If `xmake` starts failing with missing-file or stale-config style errors, the known recovery flow is:
- `xmake f -c`
- re-run the full `xmake f --qt="/home/haiku/program/qt" --arch=arm64-v8a --toolchain=zig --cross=aarch64-linux-gnu.2.27 -m release -vD`
- `xmake`

The main shared library target is `bili_plugin`, defined in `xmake.lua`.

### Go sidecars
- Build both Go binaries: `./go_server/build.sh`
- Manually build the main API server:
  - `cd go_server/main && CGO_ENABLED=0 go build -ldflags="-s -w" -trimpath -o ../server`
- Manually build the SMS login helper:
  - `cd go_server/sms && CGO_ENABLED=0 go build -ldflags="-s -w" -trimpath -o ../bili-sms`

### Packaging
- Build everything and assemble the distributable zip: `./pak.sh`

`pak.sh` packages these runtime artifacts together:
- `build/linux/arm64-v8a/release/libbili_plugin.so`
- `qml/`
- `metadata.json`
- `icon.png`
- `go_server/server`
- `go_server/bili-sms`

### Running services manually during development
The plugin normally starts the main Go server itself from C++.

- Run the main local API server manually:
  - `cd go_server/main && PORT=8000 DEBUG=true go run .`
- Run the SMS login helper manually:
  - `cd go_server/sms && go run .`

Ports used by the repo:
- Main local API: `127.0.0.1:8000`
- SMS login helper: `0.0.0.0:8666`

### Tests and linting
No dedicated lint target, test suite, or single-test command was found in this repository.
There are no committed `*_test.go`, `tests/`, or xmake test targets at the repo root.

## High-level architecture

This repository is a Bilibili client plugin for a fixed `320x170` touch UI. It is not a single app binary: it is a plugin bundle made of QML UI, a Qt/C++ shared library, and two Go executables.

`metadata.json` is the runtime entry contract:
- `main_qml`: `qml/main.qml`
- `main_so`: `libbili_plugin.so`

### Layer 1: QML UI
The UI entry point is `qml/main.qml`.

Key characteristics:
- `main.qml` owns page routing, back-stack behavior, page transition animation, and scroll-position restoration.
- Pages are loaded through `Loader`s rather than a single stack component.
- Some loaders stay alive while a page still exists in `pageStack`, so state preservation depends on the current `active` conditions in `main.qml`.
- QML pages are thin: they mostly bind to `BiliController` properties/models and call `Q_INVOKABLE` methods.

Important implication: when changing navigation or page lifetime behavior, check `navigateTo()`, `goBack()`, `pageStack`, and each page loader’s `active` expression in `qml/main.qml`, because that is where state retention is implemented.

### Layer 2: Qt/C++ plugin runtime
The plugin registration and runtime bootstrapping live in `src/BiliController.cpp`.

That file is responsible for:
- Registering `BiliController` and list models as QML types
- Starting and stopping the local Go API server process
- Adding QML import paths
- Registering the `image://bili` image provider

The core application logic centers on `BiliController`:
- `src/BiliController.cpp` / `src/BiliController.h`: QML-facing controller, plugin registration, runtime bootstrapping, shared state, and thin external API wrappers.
- `src/modules/**`: feature implementations for feed, search, video detail, playback, comments, favorites, history, login, seasons, UP pages, and image-viewer preparation.

`BiliController` is the boundary between QML and the rest of the system:
- QML should call controller methods instead of making direct network requests.
- Most UI state exposed to QML is either a `Q_PROPERTY` on `BiliController` or a `QAbstractListModel` from `BiliModels`.

### Layer 3: Models and parsing
`src/BiliModels.h` / `src/BiliModels.cpp` define the shared list models used across the UI:
- video lists
- search results
- comments and replies
- video parts
- favorite folders

A lot of cross-page behavior depends on model role names and shared parsing helpers. For example, multi-part video UI relies on `partCount`, which is populated in `VideoListModel::parseVideoItem()` and then consumed by QML card components.

Important implication: if a badge or field appears correct in one page but not another, compare whether that page is reusing the shared parser/model path or manually constructing `VideoItem`s in `src/modules/**`.

### Layer 4: Networking inside the plugin
`src/BiliNetwork.*` is the shared network layer.

Important facts:
- It defaults to `http://127.0.0.1:8000` as the API base.
- It owns request cancellation and download flows.
- QML image loading is split from JSON API loading: covers/avatars commonly go through the `image://bili` provider instead of directly hitting remote URLs.

Important implication: when debugging image problems, check both the page’s `coverUrl` binding and the `BiliImageProvider`/`BiliNetwork` path, not just the JSON API response.

### Layer 5: Local Go services
The Go code under `go_server/` is a local service layer in front of Bilibili.

#### `go_server/main`
`go_server/main/main.go` runs the main local HTTP API server on port `8000`.

Its job is to:
- Proxy and normalize upstream Bilibili APIs
- Manage login cookies and local auth state
- Serve endpoints consumed by `BiliController`
- Support playback-related and subtitle-related flows

#### `go_server/sms`
`go_server/sms/main.go` is a separate helper for SMS login, served on port `8666`.
It exposes the local verification page and the polling endpoints used by the C++ login flow.

## Runtime request flow
A typical data flow is:
1. User interacts with a QML page
2. The page calls a `BiliController` method
3. `BiliController` uses `BiliNetwork`
4. `BiliNetwork` calls the local Go server
5. The Go server calls upstream Bilibili APIs
6. JSON comes back into `BiliController` / models
7. QML updates via bound properties and list models

This means many bugs that look like “UI-only” issues are actually caused by one of three layers:
- page bindings in QML
- manual data mapping in `src/modules/**`
- upstream field passthrough in the local Go server

## Deployment assumptions
The plugin code assumes deployment under `/userdisk/PenMods/plugins/bili_plugin/`.
Hard-coded paths in the C++ plugin point there for:
- the Go `server` executable
- QML import paths
- bundled runtime assets

Important implication: if local development diverges from packaged behavior, verify whether the issue is path-sensitive rather than logic-sensitive.

## Practical editing guidance
- For UI changes, check whether the page uses shared components from `qml/components/` before editing page-local markup.
- For data bugs across multiple pages, inspect shared model parsing in `BiliModels.cpp` and manual `VideoItem` construction in `src/modules/**`.
- For anything involving login, cookies, QR flow, or SMS flow, follow the full chain across QML -> `BiliController`/`src/modules/login` -> `go_server/main` or `go_server/sms`.
- For navigation/state-loss regressions, start from `qml/main.qml`, not the individual page first.
