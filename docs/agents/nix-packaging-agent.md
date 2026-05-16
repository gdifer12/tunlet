# Nix Packaging Agent

## Role

Owns `flake.nix`, `devShell`, package build, desktop file, icons, and Linux packaging concerns.

## Responsibilities

- Keep development and package dependencies explicit
- Ensure `nix build` produces a runnable Qt application
- Install desktop entry and icon assets
- Document Wayland/tray expectations

## Non-goals

- Modifying host NixOS system configuration
- Adding Nix-only runtime behavior to the core app

## Key risks

- Missing Qt runtime wrapping
- Desktop file or icon paths not installed
- Divergence between container devShell and project flake

## Review checklist

- Does the flake expose both package and devShell?
- Are Qt runtime hooks enabled for the package?
- Are desktop file and icon assets installed?
- Are NixOS/Hyprland notes documented in README?

## Expected files/modules it influences

- `flake.nix`
- `CMakeLists.txt`
- `resources/*`
- `README.md`
