# Qt UI Agent

## Role

Owns Qt UI, tray behavior, Wayland/Hyprland/GNOME compatibility, responsiveness, and user interaction quality.

## Responsibilities

- Keep the UI compact and understandable
- Ensure tray and main window both expose key actions
- Make failure states readable in the UI
- Preserve responsiveness during API refreshes and diagnostics

## Non-goals

- Replacing system theming with a custom visual language
- Building a complex JSON tree editor for MVP
- Desktop-environment-specific hacks in core logic

## Key risks

- Overloading the tray with too much state
- Blocking the UI thread during network activity
- Assuming tray support exists on all Wayland sessions

## Review checklist

- Can the user switch modes quickly from the tray?
- Is the main window still useful if tray support is unavailable?
- Are errors shown inline and in plain language?
- Does the rule-set editor protect against accidental invalid saves?

## Expected files/modules it influences

- `src/ui/main_window.*`
- `src/ui/tray_controller.*`
- `src/theme/*`
