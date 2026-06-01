# obs-titles — Native OBS Plugin for Animated Titles

A fully native C++/Qt OBS plugin providing After Effects–style title creation,
management, and real-time compositing.

---

## Architecture

```
obs-titles-plugin/
├── CMakeLists.txt
├── data/
│   └── locale/
│       └── en-US.ini
└── src/
    ├── plugin-main.h / .cpp   ← OBS module entry point
    ├── title-data.h  / .cpp   ← Data model (Title, Layer, Keyframe) + JSON persistence
    ├── title-source.h / .cpp  ← OBS source: Cairo renderer → gs_texture
    ├── title-dock.h  / .cpp   ← OBS Dock: title list panel
    └── title-editor.h / .cpp  ← After Effects–style editor (Canvas + LayerStack + Timeline + Properties)
```

### Component Map

| Component | OBS Integration | Purpose |
|---|---|---|
| `TitleSource` | `obs_source_type INPUT` | Renders a title to the OBS video mix per-frame via Cairo → `gs_texture` |
| `TitleDock` | `obs_frontend_add_dock()` | Floating/dockable title list with scene-add button |
| `TitleEditor` | `QDialog` (non-modal) | Full AE-style editor with canvas, layer stack, timeline, properties |
| `TitleDataStore` | Singleton | Owns all `Title` objects; serialises to `obs-titles/titles.json` |

---

## Dependencies

| Library | Purpose |
|---|---|
| **OBS Studio** (libobs + obs-frontend-api) | Plugin API, graphics, frontend dock |
| **Qt 5.15+ or Qt 6** | All UI widgets |
| **Cairo** | CPU-side 2D compositing for source rendering |
| **Pango + PangoCairo** | Font layout and text rendering |
| **nlohmann/json** | JSON serialisation (fetched automatically by CMake) |

---

## Build Instructions

### Linux (Ubuntu 22.04+)

```bash
# 1. Install dependencies
sudo apt install \
  cmake ninja-build \
  libobs-dev obs-frontend-api-dev \
  qtbase5-dev libqt5widgets5 \
  libcairo2-dev libpango1.0-dev

# 2. Configure
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 3. Build
cmake --build build

# 4. Install (OBS plugin path)
sudo cmake --install build
# or symlink:
ln -s $(pwd)/build/obs-plugins/obs-titles.so \
      ~/.config/obs-studio/plugins/obs-titles/bin/64bit/obs-titles.so
```

### macOS

```bash
brew install cmake cairo pango pkg-config

cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DOBS_SOURCE_DIR=/path/to/obs-studio

cmake --build build
```

### Windows (MSYS2 / vcpkg)

```bat
vcpkg install cairo pango[fontconfig] obs-studio qt6-base

cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
```

---

## Data Format

Titles are saved in the OBS profile config directory:

```
%APPDATA%\obs-studio\plugin_config\obs-titles\titles.json   (Windows)
~/.config/obs-studio/plugin_config/obs-titles/titles.json   (Linux)
~/Library/Application Support/obs-studio/plugin_config/obs-titles/titles.json (macOS)
```

### Title JSON Schema (abbreviated)

```json
[
  {
    "id": "uuid",
    "name": "My Lower Third",
    "duration": 5.0,
    "bg_color": 0,
    "width": 1920,
    "height": 1080,
    "layers": [
      {
        "id": "uuid",
        "name": "Title Text",
        "type": 0,
        "visible": true,
        "in_time": 0.0,
        "out_time": 5.0,
        "pos_x": { "static_value": 960.0, "keyframes": [] },
        "pos_y": { "static_value": 900.0, "keyframes": [] },
        "opacity": {
          "static_value": 1.0,
          "keyframes": [
            { "time": 0.0, "value": 0.0, "easing": 2 },
            { "time": 0.5, "value": 1.0, "easing": 2 }
          ]
        },
        "text_content": "Breaking News",
        "font_family": "Helvetica Neue",
        "font_size": 72,
        "font_bold": true,
        "text_color": 4294967295
      }
    ]
  }
]
```

### Easing values

| Value | Easing |
|---|---|
| 0 | Linear |
| 1 | Ease In |
| 2 | Ease Out |
| 3 | Ease In/Out |
| 4 | Cubic Bezier |
| 5 | Hold (jump cut) |

---

## Extending the Plugin

### Adding a new layer type

1. Add a value to `enum class LayerType` in `title-data.h`
2. Add rendering logic in `title-source.cpp → render_title_frame()` (Cairo)
3. Add Qt paint logic in `title-editor.cpp → CanvasPreview::render_to_pixmap()`
4. Add UI controls in `PropertiesPanel`
5. Add JSON serialisation in `layer_to_json()` / `layer_from_json()`

### Adding keyframe support for a property in the editor

The `TimelineWidget` already draws keyframe diamonds for all `AnimatedProperty`
fields. To allow adding keyframes from the Properties panel, add a "◆ Add KF"
button next to the property spinbox that calls:

```cpp
Keyframe kf;
kf.time  = playhead_;
kf.value = spn_px_->value();
kf.easing = EasingType::EaseInOut;
layer_->pos_x.keyframes.push_back(kf);
std::sort(layer_->pos_x.keyframes.begin(), layer_->pos_x.keyframes.end(),
          [](auto &a, auto &b){ return a.time < b.time; });
emit property_changed();
```

---

## Roadmap / TODO

- [ ] Image layer type with file picker
- [ ] Shape (ellipse, polygon) layer type
- [ ] Gradient fills
- [ ] Keyframe editor: drag to move keyframes on timeline
- [ ] Keyframe editor: right-click → set easing
- [ ] Bezier curve editor overlay (velocity graph)
- [ ] Template system: save/load title presets
- [ ] Playlist mode: auto-advance through titles
- [ ] GPU-accelerated rendering path (replace Cairo with GS effects)
- [ ] Live preview in dock (thumbnail strip)
- [ ] Undo/redo stack (Qt QUndoStack)
- [ ] Multi-select layers
- [ ] Text drop-shadow property
- [ ] Export title as PNG sequence
