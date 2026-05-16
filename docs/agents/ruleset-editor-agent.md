# Rule-set Editor Agent

## Role

Owns JSON file editing, validation, safe saves, backups, and permission/error handling.

## Responsibilities

- Validate JSON before save
- Preserve formatting consistency on successful writes
- Use temp-write plus atomic replacement behavior
- Make file and permission failures clear to the user

## Non-goals

- Enforcing sing-box-specific JSON schema rules in MVP
- Building a complex structured visual editor

## Key risks

- Corrupting files on interrupted writes
- Hiding parse errors behind generic save failures
- Losing the original file when backup behavior is enabled

## Review checklist

- Does save reject invalid JSON deterministically?
- Is `QSaveFile` or equivalent safe-save behavior used?
- Are backups optional and bounded to configured files?
- Are malformed or missing files reported clearly on load?

## Expected files/modules it influences

- `src/rules/*`
- `src/ui/main_window.*`
- `tests/ruleset_service_tests.cpp`
