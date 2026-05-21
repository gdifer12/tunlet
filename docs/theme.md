# Theme Layout

The built-in application theme is generated at runtime from two embedded resources:

- `resources/themes/default.theme.json`
- `resources/styles/default.qss.in`

`default.theme.json` is the source of truth for colors, font families, font sizes, and a few shared radii. `default.qss.in` keeps the existing selector structure and references those values with `{{path.to.token}}` placeholders.

## What each config field does

The `theme` block in `config.yaml` supports three optional inputs:

- `theme.themePath`: path to a `custom.theme.json` file. Its contents deep-merge over the built-in token tree, so you can override only a few colors or fonts.
- `theme.templatePath`: path to a `custom.qss.in` file. If present, tunlet renders this template instead of the built-in one, still using the merged token set.
- `theme.qssPath`: path to a plain `.qss` file appended after the rendered generated theme. This is the compatibility layer for raw selector overrides.

Relative paths resolve through `configRoute`.

## Where to change colors and fonts

- Change built-in colors in `resources/themes/default.theme.json` under `color.*`.
- Change built-in fonts and font sizes in `resources/themes/default.theme.json` under `font.*`.
- Change selector structure in `resources/styles/default.qss.in`.

If you only want to customize the palette or fonts for one deployment, prefer `theme.themePath` over editing the built-in files.

## How theme application works

At startup and on runtime config apply, `ThemeLoader` performs these steps:

1. Load the built-in token JSON.
2. Deep-merge `theme.themePath` if configured.
3. Load the built-in template or `theme.templatePath` if configured.
4. Render the QSS template by replacing `{{token.path}}` placeholders.
5. Append `theme.qssPath` if configured.
6. Apply the final stylesheet to `QApplication`.

If token loading or template rendering fails, tunlet leaves the current stylesheet unchanged and surfaces a warning. If only the raw QSS overlay fails, the generated theme still applies.

## Adding another custom theme

The lightest-weight path is:

1. Copy the parts you want to override into a `custom.theme.json`.
2. Set `theme.themePath` to that file.
3. Use `theme.templatePath` only if you need selector-level changes.
4. Use `theme.qssPath` only for final raw QSS overrides that do not fit the token/template model.

## Verification

After theme changes:

- build the project
- open the main pages and settings/rules editors
- verify hover, pressed, disabled, and focus states
- grep for unexpected hardcoded colors or fonts in `resources/styles/default.qss.in`
