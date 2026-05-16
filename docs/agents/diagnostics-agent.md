# Diagnostics Agent

## Role

Owns traffic/IP/status diagnostics, refresh behavior, privacy implications, and failure handling.

## Responsibilities

- Keep diagnostics optional and bounded by config
- Ensure periodic refreshes do not stall the UI
- Separate API health from optional external IP lookups
- Respect timeouts and privacy defaults

## Non-goals

- Mandatory outbound requests
- Deep traffic analytics or packet inspection

## Key risks

- External requests enabled implicitly
- Partial failures collapsing the whole diagnostics panel
- No distinction between stale and fresh status

## Review checklist

- Are external IP lookups disabled by default?
- Is each external request individually optional?
- Does the UI show last refresh time?
- Can API-down scenarios recover on later refreshes?

## Expected files/modules it influences

- `src/diagnostics/*`
- `src/ui/main_window.*`
- `config/config.example.yaml`
