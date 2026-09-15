# Application assets

Place application-owned fonts, translations, images, media, and other runtime
assets here. The starter copies this directory beside the executable during
builds and installs it under `bin/assets`.

For fonts and localisation, create:

```text
assets/
  fonts/
    Inter-Regular.ttf
  lang/
    en_us.json
```

Use a redistributable TrueType or OpenType font. A locale file is a flat UTF-8
JSON object:

```json
{
  "home.greeting": "Hello from Vibrance!"
}
```

Connect both in `src/main.cpp`:

```cpp
Engine::set_shared_locale("en_us");

options.localisationDirectory = "assets/lang";
options.fontPath = "assets/fonts/Inter-Regular.ttf";
```

Inside `options.build`, resolve a translation with:

```cpp
const Text greeting = ui_tr("home.greeting", "Hello from Vibrance!");
const std::string label = view.localisation.resolve(greeting);
```

Pass `label` to `view.ui.text(...)` for a fixed label. For automatic runtime
language changes, create the entity with the `Text` overload of
`view.ui.create_aligned_text(...)`, passing `view.localisation` and
`view.fontAtlas`.
