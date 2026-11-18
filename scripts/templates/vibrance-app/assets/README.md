# Application assets

This directory belongs to the application. The SDK helper copies it beside the
executable during builds and installs it under `bin/assets`.

A simple layout is:

```text
assets/
  fonts/Example-Regular.ttf
  lang/en_gb.json
  images/
```

Set `UiWindowOptions::fontPath` before building text and set
`localisationDirectory` to your dictionaries. A locale file contains a flat UTF-8
JSON object:

```json
{
  "home.greeting": "Hello from vibranceUI"
}
```

Select the locale with `Engine::set_shared_locale("en_gb")` and resolve
`ui_tr("home.greeting", "Hello")` through the window's localisation context.
Localisation-aware builder overloads retain bindings for later locale changes.
Supply your own fonts, icons, images and their notices. The starter's first
window uses shapes so it can run before you add a font.

See [Applications](../../../../docs/applications.md) and
[User interface](../../../../docs/user-interface.md) for complete setup and
appearance APIs.
