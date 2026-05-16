# tunlet

`tunlet` is a small Linux Qt 6 desktop application for controlling existing `sing-box` Clash API endpoints and editing configured sing-box JSON rule-set files.

It is intentionally not a VPN manager, service manager, config generator, or network setup wizard. It assumes `sing-box` is already configured and already running.

## Features

- Single Clash API endpoint with logical `direct`, `proxy`, and `auto` profiles by default
- Additional named mode profiles configurable in YAML
- Tray menu for quick switching and status refresh
- Compact main window for control, diagnostics, and JSON rule-set editing
- JSON validation and safe-save with optional backups
- Optional external IP diagnostics, disabled by default
- Optional custom QSS theme file
- Nix flake with `devShell` and package build

## Build and run

### NixOS / flake-based development

The container and project use `nix-command` and `flakes` explicitly:

```bash
nix --extra-experimental-features 'nix-command flakes' develop /state/agent-env -c bash
cmake -S . -B build -G Ninja
cmake --build build
./build/tunlet
```

You can also build the package directly:

```bash
nix --extra-experimental-features 'nix-command flakes' build
```

### Desktop notes

- On Wayland/Hyprland, tray availability depends on a StatusNotifier-compatible host such as the Waybar tray module.
- If no tray host is available, the main window still works.
- The app follows the system theme by default and can optionally load a custom QSS file.

## Configuration

Default config path:

```text
~/.config/tunlet/config.yaml
```

Override at runtime:

```bash
./build/tunlet --config /path/to/config.yaml
```

An example configuration is provided at [config/config.example.yaml](/work/tunlet/config/config.example.yaml).

### Config overview

Top-level keys:

- `clashApi`
- `ruleSets`
- `diagnostics`
- `theme`
- `editing`

`clashApi.profiles` adds extra named mode mappings on top of the built-in default profiles:

- `direct -> direct`
- `proxy -> global`
- `auto -> rule`

Each extra profile may define:

- `name`
- `mode`
- `desc`

This avoids hardcoding the complete list of supported mode values in the UI.

## MVP behavior

- The app loads YAML config at startup and validates required fields.
- The current Clash mode is shown prominently and can be switched through configured profiles.
- Additional profiles are listed and can be switched from the main window or tray submenu.
- The UI also shows the `mode-list` reported by `/configs`, so you can see which backend modes are actually available.
- Rule-set files are edited as JSON text, validated before save, and written via safe-save semantics.
- The YAML app config can be edited from the UI with validation and safe-save.
- Diagnostics stay non-blocking and use explicit timeouts.

## Project layout

- `docs/agents`: architecture and review checklists
- `src/app`: bootstrap and path handling
- `src/core`: shared domain types
- `src/config`: YAML schema and loader
- `src/clash`: Clash API client and mode controller
- `src/rules`: JSON file handling
- `src/config/config_file_service.*`: UI-safe YAML config editing
- `src/diagnostics`: periodic health and optional external IP lookups
- `src/ui`: Qt Widgets main window and tray
- `src/theme`: optional QSS loading
- `tests`: unit tests

## Known assumptions

- Clash-compatible mode switching happens through `PATCH /configs` with a configured `mode` value.
- By default, tunlet maps logical `direct/proxy/auto` to Clash `direct/global/rule` and writes the built-in Clash values as `Direct/Global/Rule`.
- Extra profiles can be added on top of those defaults through `clashApi.profiles`.
- The first version does not manage the `sing-box` process lifecycle.
