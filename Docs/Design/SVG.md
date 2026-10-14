# SVG

## What it is
- **SVG asset** (`UWidgetSVG`, "SVG", `NameTBD/Widgets/WidgetSVG.h`): made by importing a `.svg`.
  - It keeps the file's text (`SVGSource`, cooked: the game draws from it), its size (`SVGSize`, from width/height, else viewBox) and its colours.
  - It is **drawn from its vectors at the size it is shown on screen**, so it is sharp at any size, any DPI and under any scale animation. It is not drawn into a fixed texture.
- **Where it is drawn:**
  - **SVG Image** (`UWidgetSVGImage` / `SWidgetSVGImage`, palette NameTBD): the SVG counterpart of Image.
    - Settings: **SVG**, **Color And Opacity** (tint), **Fit** (`EWidgetSVGFit`: Contain (default, keeps proportions, centred) / Fill), **Override Image Size** + **Image Size** (default: the SVG's own size).
    - Blueprint: `SetSVG`, `SetColorAndOpacity`, `SetFit`, `SetImageSize`, `ClearImageSize`.
  - **Every plugin widget with an image** has an **Image Source** (`EWidgetImageSource`: **Texture 2D** (default everywhere, the brush as before) / **SVG**) and an SVG field shown for SVG:
    - Shape paints (fill, stroke, and the Flow List scroll bar's lines; Shapes.md).
    - The Flow List scroll bar's Knob and Dots (FlowList.md).
    - The Media Box's Error Image (MediaBox.md).
    - Niagara Particles' Particle Image (NiagaraUI.md): drawn once at a set resolution, since particles come in many sizes.
    - The Context Menu Button's Design Targets: a state's brush channel (Buttons.md). Shown on shapes, SVG Images, and Image / Border / Button brushes drawn at the widget's size.
  - **Engine widgets** (Image, Button styles...) take textures, so for them right-click an SVG > **Create SVG Texture** (below).
- **Colours (the "black SVG" fix):**
  - **Colors** (`TArray<FWidgetSVGColor>`, `EditFixedSize`) lists every colour the SVG uses (fills, strokes, gradient stops; first-use order; opacity left out), with **Draw As** next to each. Draw As alpha multiplies the SVG's own opacity.
  - **Draw In White (Tint With Widget)** (`bDrawInWhite`) draws every colour white keeping opacity, so the widget's tint colours it.
  - A shape with no fill is black in SVG: that is the black entry. Edited in the asset's Details (double-click: the generic asset editor).

## Drawing (`FWidgetSVGBrush`, `WidgetSVGBrush.h`)
- Held by a Slate widget, one per image it draws. `Get(SVG, Template, Geometry, LocalSize, Widget)` returns an `FSlateBrush` for the SVG drawn at `GetPixelSize`: LocalSize through the accumulated render transform, so DPI and render-transform scale count. Sides are capped at 4096. `GetAtPixelSize` is for a fixed size (Niagara).
- **Drawings** (`FWidgetSVGDrawing`):
  - One per (SVG, revision, pixel size), shared through a map of weak pointers, so the same icon at the same size is drawn once.
  - Each holds a Slate dynamic image (`GenerateDynamicImageResource`, BGRA, released when the last holder goes) as an `FSlateDynamicImageBrush`.
  - The brush handed out is that image with the template's look: Draw As, Tiling, Mirroring, Margin, Tint, outline. With no template, a plain Image. Image Size is LocalSize.
- **When drawn:**
  - On a worker thread (`Async(ThreadPool)`), then on the game thread the image is made and the waiting widgets are invalidated (Paint).
  - A widget showing nothing yet draws at once if the drawing is at most 512 x 512 px (an icon has no blank first frame); otherwise nothing is drawn until the first drawing arrives.
  - On a size change the last drawing is shown, stretched, until the new one arrives. Only one drawing is in flight per brush, so an animated size draws as fast as drawings finish, not once per frame.
  - The drawing scale covers the size both ways (a Fill-stretched box is drawn smaller one way, not bigger).
- **Revision:** `UWidgetSVG::GetRevision()` comes from a global counter, bumped by any edit, undo or reimport. Drawings are found by it, so an edit redraws on the next paint. It is not saved.
- **Rasterizer** (`WidgetSVGRaster.h/.cpp`: `Read`, `MergeColors`, `Draw`):
  - nanosvg, vendored header-only in `Source/ThirdParty/nanosvg` (upstream commit and licence in its README), on `NameTBD`'s private include path, in games too.
  - Compiled only in `WidgetSVGRaster.cpp`, as C++ inside `namespace NameTBDNanoSVG` (`NANOSVG_CPLUSPLUS` / `NANOSVGRAST_CPLUSPLUS`; the C headers it uses are included first, outside the namespace), so it never clashes with the engine's own copy.
  - Each call parses on its own (thread-safe). Recolouring edits the parsed paints (`0xAABBGGRR`) before rasterizing.
  - Output is straight-alpha RGBA, edge colours spread into clear pixels, swapped to BGRA.

## SVG Texture (for engine widgets)
- **What it is:** a plain **`UTexture2D`** drawn from an SVG at a fixed resolution, so every texture picker, `Set Brush From Texture` and thumbnail treat it as any image.
  - The SVG and settings ride on it as Asset User Data: **`UWidgetSVGTextureData`** (`WidgetSVGTexture.h`), editor-only (`IsEditorOnly`: a game has only the texture).
  - Made by **Create SVG Texture** on an SVG (`UWidgetSVGFactory::CreateSVGTextures`): `<SVG>_Texture` next to it, starting from the SVG's colours and keeping its source file for Reimport.
- **Resolution:** **Resolution Scale** (default 4) x the SVG size, longest side held to **Max Resolution** (2048).
  - Texture settings (`ApplyTextureSettings`): `TC_EditorIcon`, sRGB, `TEXTUREGROUP_UI`, Never Stream, mips (`TMGS_SimpleAverage`).
  - Image Size on drop is the texture size.
- **Editing:** **Colors**, **Draw In White**, the resolution and SVG Size are shown in an **SVG** category at the top of the texture's Details.
  - `FWidgetSVGTextureDetails` is a class layout on `Texture2D`, adding the data's properties as external object rows.
  - Edits redraw: `PreEditChange(nullptr)`, `Source.Init` + `UseHashAsGuid`, `PostEditChange`. Not while a picker or spin box is dragged. Undo redraws.
- **Why a texture carries user data rather than being a subclass:**
  - `UTexture2D` is `MinimalAPI`, so a plugin subclass fails to link.
  - An asset behind `ISlateTextureAtlasInterface` isn't listed by the Brush's Image picker.
  - Only exported or virtual calls are made on textures (`IInterface_AssetUserData` through the interface).

## Editor (`NameTBDEditor/Private/WidgetSVGFactory.*`)
- `UWidgetSVGFactory`: a text factory for `svg` that makes an SVG asset, at priority `DefaultImportPriority + 10`. It is also the `FReimportHandler` for SVG assets and for SVG Textures, at that priority (above the engine's texture reimporter).
- `FWidgetSVGAssetTypeActions`: User Interface category, an imported asset (Reimport, source file), and **Create SVG Texture**.
- `UWidgetSVGThumbnailRenderer`: the SVG drawn to fit into a throwaway transient texture, compiled at once (`FTextureCompilingManager::FinishCompilation`), on a dark tile; no realtime thumbnails.
- Strings in `NameTBDTooltips.h` (`SVG`), colours in `NameTBDColors.h` (`Colors::SVG`). Registered in `FNameTBDEditorModule`.

## Checked outside the engine
- nanosvg in the namespace compiles with g++ and links beside a plain C copy (no duplicate symbols).
- An SVG path with no fill parses as `0xFF000000` and draws white after recolouring; gradient stops are reached and recoloured.

## Still open
- **SVG features nanosvg lacks:** `<text>`, filters, masks, clip paths, `<use>`/symbols, most CSS (`<style>` beyond simple cases), images. Such parts are dropped. A file that relies on them needs flattening (e.g. text to paths) before import.
- **Not given an Image Source:**
  - Info Card settings' Icon (a soft texture).
  - A Media Box Clip's Poster (a texture shown under the video).
- Per-widget colour overrides (one SVG in several colours without Draw In White + tint) aren't there; the colours are the asset's.
- An SVG Texture's Image Size on drop is the texture size (Resolution Scale x the SVG size), not the SVG size: a plain Texture 2D can't report another size.

## Unverified (no engine here)
- That it compiles against 5.7. In particular:
  - `GenerateDynamicImageResource` / `ReleaseDynamicResource` taking BGRA bytes, and drawing sRGB.
  - The `FSlateDynamicImageBrush` constructor, and copying it into an `FSlateBrush`.
  - `FSlateRenderTransform::TransformVector`, `FTextureCompilingManager::FinishCompilation`.
  - `FAssetTypeActions_Base::GetActions(…, FToolMenuSection&)`, `UThumbnailRenderer::AllowsRealtimeThumbnails`.
  - `IInterface_AssetUserData` on `UTexture`, `AddExternalObjectProperty`, and `UAssetUserData` being subclassable from a plugin.
- That a brush holding a Slate dynamic image survives `UImage::SetBrush` / `UButton::SetStyle` and draws (a Design Target on an Image, Border or Button).
- That a dynamic image's UVs wrap past 1 for a stroke laid Along Outline (as a tiling texture's do).
- That a class layout on `Texture2D` is applied alongside the engine's `Texture` one.
- That non-power-of-two SVG Textures get mips in 5.7.
- That the factory wins `.svg` over Interchange / Epic's SVG Importer when those are on.
- How long drawing takes for big or complex SVGs (worker thread; the first small drawing is on the game thread).
