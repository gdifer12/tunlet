# tunlet

`tunlet` is a small Linux Qt 6 desktop application for controlling existing `sing-box` Clash API endpoints and editing configured sing-box JSON rule-set files.

It is intentionally not a VPN manager, service manager, config generator, or network setup wizard. It assumes `sing-box` is already configured and already running.

## Features

- Single Clash API endpoint with built-in logical `direct`, `proxy`, and `auto` profiles by default
- Additional named mode profiles configurable in YAML
- Tray menu for quick switching and background runtime refresh
- Compact main window for control, diagnostics, and JSON rule-set editing
- JSON validation and safe-save with optional backups
- Optional command-driven connection diagnostics for IP, delay, DNS, and GeoLite2 location
- Optional custom QSS theme file
- Nix flake with `devShell` and package build

## Build and run

### NixOS / flake-based development

The project ships its own flake and can be built directly from the repository:

```bash
nix --extra-experimental-features 'nix-command flakes' develop -c bash
cmake -S . -B build -G Ninja
cmake --build build
./build/tunlet
```

You can also build the package directly:

```bash
nix --extra-experimental-features 'nix-command flakes' build
```

If you are using a separate container-level devshell such as `/state/agent-env`, treat that as environment-specific convenience only. It is not required by this repository and is not part of the project layout.

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

- `configRoute`
- `clashApi`
- `ruleSets`
- `diagnostics`
- `theme`
- `editing`
- `tray`

`configRoute` is the base directory used to resolve relative file paths in the config, for example `~/.config`.

`diagnostics.connection` configures the three external probes and the GeoIP backend:

- `ipv4`: command used to resolve public IPv4
- `timing`: command used to resolve DNS/connect/TLS/total delay
- `dns`: command used to resolve DNS TXT diagnostics
- `location.mode`: `disabled`, `local_db`, or `dynamic_cache`
- `location.localDb.databasePath`: local `GeoLite2-City.mmdb` path used to map the resolved IP to a location
- `location.localDb.asnDatabasePath`: optional local `GeoLite2-ASN.mmdb` path used to enrich ASN/org data
- `location.localDb.downloadUrl`: optional URL to a ready-to-use city `.mmdb`; if `databasePath` is missing, tunlet creates the parent directory and downloads the DB on demand
- `location.dynamicCache.*`: HTTPS GeoIP provider settings, JSON cache path, TTL, jitter, and manual refresh behavior

`local_db` uses the MaxMind GeoLite2 database format. In many setups you should expect to obtain `GeoLite2-City.mmdb` manually and place it at `location.localDb.databasePath`. You should download GeoLite2 City from MaxMind using your own MaxMind account ([GeoLite2 data © MaxMind](https://www.maxmind.com/en/geolite-free-ip-geolocation-data)). The optional `downloadUrl` exists only for explicit auto-bootstrap setups where you already control a compatible `.mmdb` download source.

`dynamic_cache` resolves location through an external HTTPS API and stores normalized responses in a local JSON cache. The default provider is `ipwhois` via `https://ipwho.is/`. Cache entries are keyed by provider + public IP and reused until explicitly refreshed. Normal runtime refreshes re-read the local cache only; the dedicated `Refresh location data` action updates the single record for the current public IP. If refresh fails and a stale cache entry exists, tunlet keeps using the stale location and marks it as such in the UI.

`clashApi.profiles` adds extra named mode mappings on top of the built-in default profiles unless `clashApi.disableDefaultProfiles` is set to `true`:

- `direct -> direct`
- `proxy -> global`
- `auto -> rule`

Each extra profile may define:

- `name`
- `mode`
- `desc`

This avoids hardcoding the complete list of supported mode values in the UI.

`tray` controls background tray behavior:

- `keepRunningWithoutWindow`: when `true`, closing the main window hides it to tray instead of exiting
- `startHidden`: when `true`, and a tray host is available, tunlet starts without showing the main window

`Save and apply` and `Reload` on the `Settings / Info` page re-apply runtime configuration without restarting the process. `tray.startHidden` is the exception: it is stored immediately but only affects the next launch.

## MVP behavior

- The app loads YAML config at startup and validates required fields.
- The current Clash mode is shown prominently and can be switched through configured profiles.
- Additional profiles are listed and can be switched from the main window or tray menu.
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
- `src/diagnostics`: periodic health plus command-driven connection probes
- `src/ui`: Qt Widgets main window and tray
- `src/theme`: optional QSS loading
- `tests`: unit tests

## Known assumptions

- Clash-compatible mode switching happens through `PATCH /configs` with a configured `mode` value.
- By default, tunlet maps logical `direct/proxy/auto` to Clash `direct/global/rule` and writes the built-in Clash values as `Direct/Global/Rule`.
- Those built-in profiles can be disabled through `clashApi.disableDefaultProfiles`, in which case only explicitly configured profiles are exposed.
- The first version does not manage the `sing-box` process lifecycle.
