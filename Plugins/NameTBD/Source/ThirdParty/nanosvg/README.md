# nanosvg (vendored)

Upstream: https://github.com/memononen/nanosvg, `src/nanosvg.h` and `src/nanosvgrast.h` at commit
`239e102ec2c691f2902e20ace2ed36ee4a35cfe6`, unmodified. zlib licence (`LICENSE.txt`).

Compiled only in `NameTBD/Widgets/WidgetSVGRaster.cpp`, inside a namespace with `NANOSVG_CPLUSPLUS` /
`NANOSVGRAST_CPLUSPLUS`, so its symbols never meet another copy (the engine's Slate has its own). It is in games too:
SVG assets are drawn at runtime, at the size they are shown.
