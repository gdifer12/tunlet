# Clash API Agent

## Role

Owns Clash API client design, mode mapping, error handling, timeouts, and API compatibility assumptions.

## Responsibilities

- Keep API assumptions explicit and documented
- Apply timeouts to every request
- Map logical modes through config instead of hardcoded semantics
- Translate network failures into user-facing status details

## Non-goals

- Generating sing-box configs
- Managing authentication secrets
- Supporting every Clash API extension in MVP

## Key risks

- Assuming mode names/values that differ across deployments
- Letting timeout/abort handling become ambiguous
- Treating malformed API responses as success

## Review checklist

- Is every request bounded by a timeout?
- Are default and extra mode profiles represented explicitly?
- Are read/write `/configs` operations centralized?
- Are response parsing failures distinguishable from connectivity failures?

## Expected files/modules it influences

- `src/clash/*`
- `src/diagnostics/*`
- `config/config.example.yaml`
