# tunlet

`tunlet` is a small Linux Qt 6 desktop application for controlling existing `sing-box` Clash API endpoints and editing configured sing-box JSON rule-set files.

It is intentionally not a VPN manager, service manager, config generator, or network setup wizard. It assumes `sing-box` is already configured and already running.

## Features

- Single Clash API endpoint with built-in logical `direct`, `proxy`, and `auto` profiles by default
- Additional named mode profiles configurable in YAML
- Optional background current-mode synchronization against the Clash API
- Tray menu for quick switching and background runtime refresh
- Compact main window for control, diagnostics, and JSON rule-set editing
- JSON validation and safe-save with optional backups
- Optional command-driven connection diagnostics for IP, delay, DNS, and GeoLite2 location
- Optional dual-sink file logging with human-readable text, JSON Lines output, and size-based rotation
- Built-in generated QSS theme with optional token, template, and raw-QSS overrides
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
- The app uses a built-in generated QSS theme and can optionally load external token, template, or raw-QSS overrides.

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
- `logging`
- `ui`

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

`dynamic_cache` resolves location through an external HTTPS API and stores normalized responses in a local JSON cache. The default provider is `ipwhois` via `https://ipwho.is/`. Cache entries are keyed by provider + public IP and reused until explicitly refreshed. Normal runtime refreshes re-read the local cache only. The exception is cold-miss bootstrap: on initial startup and after a real mode switch, tunlet will fetch and persist the current IP if no cache entry exists yet. The dedicated `Refresh location data` action still remains the explicit way to refresh the current record. If refresh fails and a stale cache entry exists, tunlet keeps using the stale location and marks it as such in the UI.

`clashApi.profiles` adds extra named mode mappings on top of the built-in default profiles unless `clashApi.disableDefaultProfiles` is set to `true`:

- `direct -> direct`
- `proxy -> global`
- `auto -> rule`

Each extra profile may define:

- `name`
- `mode`
- `desc`

`clashApi.modeSyncIntervalMs` controls how often tunlet re-reads the current backend mode from the Clash API in the background. Use `0` to disable background mode sync.

This avoids hardcoding the complete list of supported mode values in the UI.

`tray` controls background tray behavior:

- `keepRunningWithoutWindow`: when `true`, closing the main window hides it to tray instead of exiting
- `startHidden`: when `true`, and a tray host is available, tunlet starts without showing the main window
- `interactiveRefreshIntervalMs`: while a native tray menu session is considered active, tunlet reruns the combined runtime refresh at this faster cadence

`theme` controls the generated application theme:

- `themePath`: optional `custom.theme.json` override; deep-merges over the built-in theme tokens
- `templatePath`: optional `custom.qss.in` override; replaces the built-in QSS template used for rendering
- `qssPath`: optional plain QSS overlay appended after the rendered generated theme

All theme paths are optional and resolve through `configRoute` when relative. `Save and apply` and `Reload` re-read theme tokens, template overrides, and raw QSS overlays without restart. If a raw `qssPath` overlay fails to load, tunlet still applies the rendered generated theme and reports a warning. If token loading or template rendering fails, tunlet keeps the previously active stylesheet instead of applying a broken theme.

`logging` controls optional file logging:

- `enabled`: enables or disables file logging entirely
- `level`: minimum level written to sinks: `info`, `warning`, or `error`
- `textPath`: optional human-readable text log sink
- `jsonlPath`: optional JSON Lines log sink
- `rotation.enabled`: enables or disables size-based rotation for both sinks
- `rotation.maxFileBytes`: rotate a sink before appending the next entry that would exceed this size
- `rotation.keepFiles`: number of rotated archives to keep per sink, not counting the active file

If `logging.enabled` is `true`, at least one sink path must be configured. If both sinks are configured they must point to different files. Relative sink paths resolve through `configRoute`.

If `logging.rotation.enabled` is `true`, both `rotation.maxFileBytes` and `rotation.keepFiles` are required. Rotation uses numbered suffixes such as `tunlet.log.1` and `tunlet.jsonl.1`, keeps the active base file separate from the archive count, and applies live on `Save and apply` and `Reload`. If an existing sink file already exceeds the configured limit, tunlet rotates it immediately when the logger is reopened.

`ui` controls keyboard behavior and copyable UI text:

- `textSelection.enableInformationalLabels`: when `true`, most informational labels in the UI can be selected and copied with the mouse
- `keyboard.shortcuts.*`: Qt key-sequence strings for close, page navigation, selector opening, refresh, validate, save, and reload actions
- any shortcut entry may be set to an empty string to disable that binding

`Save and apply` and `Reload` on the `Settings / Info` page re-apply runtime configuration without restarting the process. This includes logging level and sink paths. `tray.startHidden` is the exception: it is stored immediately but only affects the next launch.

## MVP behavior

- The app loads YAML config at startup and validates required fields.
- The current Clash mode is shown prominently and can be switched through configured profiles.
- `Refresh runtime` and optional background mode sync both re-read the current Clash mode so UI and tray can catch external mode changes.
- `Refresh runtime` tracks a combined diagnostics result: `Last reload` means the last successful IP/timing/DNS refresh, failed refreshes clear delay values and mark the remaining diagnostics values stale instead of pretending new data arrived.
- The tray tooltip shows mode, API reachability, IP, and delay from the latest known snapshot.
- Right-click opens a native tray menu built from standard actions and separators rather than redundant section-label rows.
- Opening the tray menu triggers an immediate runtime refresh and then uses `tray.interactiveRefreshIntervalMs` only while that native tray-menu session is active; outside that session, idle diagnostics cadence still comes only from `diagnostics.refreshIntervalMs`.
- On native tray hosts, closure detection is best-effort: tunlet uses Qt menu callbacks when they arrive and an internal fallback timeout when they do not.
- `Refresh` and mode changes use best-effort menu reopening after the action; some tray hosts may still close the menu because native menu persistence is platform-dependent.
- Additional profiles are listed and can be switched from the main window or tray menu.
- The UI also shows the `mode-list` reported by `/configs`, so you can see which backend modes are actually available.
- Logging supports text and JSONL sinks simultaneously, applies sink/path/level/rotation changes live, and can stay append-only when rotation is disabled.
- Rule-set files are edited as JSON text, validated before save, and written via safe-save semantics.
- The YAML app config can be edited from the UI with validation and safe-save.
- Diagnostics stay non-blocking and use explicit timeouts.
- Runtime refresh and config-reload buttons disable briefly while their async refresh cycle is in flight.

## Project layout

- `docs/agents`: architecture and review checklists
- `src/app`: bootstrap and path handling
- `src/core`: shared domain types
- `src/config`: YAML schema and loader
- `src/clash`: Clash API client and mode controller
- `src/logging`: live-configurable file logging service
- `src/rules`: JSON file handling
- `src/config/config_file_service.*`: UI-safe YAML config editing
- `src/diagnostics`: periodic health plus command-driven connection probes
- `src/ui`: Qt Widgets main window and tray
- `src/theme`: built-in theme token/template loading and external theme overrides
- `tests`: unit tests

Theme editing details are documented in [docs/theme.md](/work/tunlet/docs/theme.md).

## Known assumptions

- Clash-compatible mode switching happens through `PATCH /configs` with a configured `mode` value.
- By default, tunlet maps logical `direct/proxy/auto` to Clash `direct/global/rule` and writes the built-in Clash values as `Direct/Global/Rule`.
- Those built-in profiles can be disabled through `clashApi.disableDefaultProfiles`, in which case only explicitly configured profiles are exposed.
- The first version does not manage the `sing-box` process lifecycle.
