# Supplied SNES Arwing cockpit

The user supplied `1-Custom-_-Edited-Star-Fox-Customs-Arwing-Arwing-SNES-.zip`
on 2026-10-08 and requested this custom model as the VR cockpit. The archive
contains `SNES Arwing - High poly/Snes_Arwing.obj` and `Arwing.png`, with no author
or license notice. No attribution or license is inferred here. The source OBJ
and palette SHA-256 values are recorded in `geometry.json`.

`geometry.json` retains OBJ coordinates, face identifiers, palette RGB values,
polygon edges and Blender's concave-polygon tessellation. The four translucent
canopy panes (faces 4, 12, 16, 17) are omitted, leaving unobstructed VR windows.
Twenty-one zero-area or near-collinear source triangles (at the six-decimal
OBJ precision) are omitted. The resulting ship has 497 triangles (459
hull/interior and 38 canopy frame); its proportions are unchanged.
No cartridge, ROM or extracted game artwork is included.

To import a supplied source folder with Blender 5.1:

```
blender --background --python-exit-code 1 --python tools/import_arwing_cockpit.py -- /path/to/source-folder
python3 tools/generate_cockpit_assets.py
```

Normal builds need neither Blender nor an OBJ importer. The generator compiles
the checked-in JSON into `src/vr/cockpit_assets.inc` and `--check` detects stale
output. The pilot fit is uniform 1.75 metres per source unit, with the eye at
OBJ (0, 1.05, 0.40). The existing native instruments sit just above the supplied
sloped dashboard, with dark insets sized to connected live instrument rows for
white-text contrast (the separated boss row gets its own inset). The art sits 8 mm above the face and the inset 2 mm behind
the art. Native gameplay state still owns wing damage and the repair
flash's visibility and colour; cockpit presentation applies them to this mesh.
Chase-view and world ship models retain the native art.

Verification status: host geometry and runtime tests only; headset-unverified.
