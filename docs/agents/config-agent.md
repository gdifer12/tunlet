# Config Agent

## Role

Owns YAML config schema, XDG paths, config validation, defaults, and example configs.

## Responsibilities

- Keep the schema explicit and documented
- Resolve default XDG config paths correctly
- Validate required fields early with actionable messages
- Keep example config aligned with actual code

## Non-goals

- Building an interactive config wizard
- Inferring large implicit defaults that hide setup mistakes

## Key risks

- Ambiguous or weak validation messages
- Example config drifting away from parser expectations
- Machine-specific paths leaking into code

## Review checklist

- Are all required fields validated?
- Does `--config` override the default path cleanly?
- Are extra mode profiles and extra editable files parsed consistently?
- Does README match the real schema?

## Expected files/modules it influences

- `src/app/application_paths.*`
- `src/config/*`
- `config/config.example.yaml`
- `README.md`
