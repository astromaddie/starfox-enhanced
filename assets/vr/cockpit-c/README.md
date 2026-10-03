# Cockpit C presentation assets

These thirteen rear modules and flat materials are newly authored assets. Seven
come from the user-approved C package. Wearer requests on October 3 added three
`rear_bulkhead_*` boxes, closing the cabin behind the seat at shoulder height, and
three `rear_hoop_*` beams, joining the window-frame strut tips to the bulkhead rim
and to each other. The JSON retains the editable module pivots, triangles
and per-face material assignments; OBJ/MTL files are the corresponding editable
exports. `tools/generate_cockpit_assets.py` compiles only this new geometry and
material metadata into `src/vr/cockpit_assets.inc`.

No cartridge mesh, texture, screenshot or extracted pixel data is included.
The runtime decodes COCKPIT and the live player from the user's asset bundle.
Original and EX have different descriptor addresses/palettes but the same
approved front positions and face ranges. A geometry-only signature and exact
range checks reject incompatible material mappings before rendering.

The editable exports retain the approved base geometry. Runtime presentation
calibration widens the upper shell by 70% and lowers it by 16 cm, blending to
the unchanged instrument face. This applies to front and rear coordinates;
the complete source-derived ship hull, repair effect and pilot reference remain fixed.
