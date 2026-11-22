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
Supply your own fonts, icons, images and their notices. The starter also searches
installed system fonts when no application font is provided. System fonts are
used in place rather than copied into this directory.

`images/sample.svg`, `audio/sample.wav` and the English/German dictionaries are
original starter assets, Copyright 2026 59xa, licensed under Apache-2.0 as supplied
in the project's `LICENCE`. The sound is a short synthesised tone and only plays
when the user presses the playback button.

The generated project's `README.md` describes its working examples and the wider
SDK. Installed API guides are under `<SDK>/share/vibrance_engine/docs`.
