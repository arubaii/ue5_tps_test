# NameTBD — Context Handoff

## Standing instructions (read first, every session)

> Read this context handoff about my plugin and understand the scope. Do not interrupt your work do ask me any questions, or to request access to a site, I am leaving the desktop and will not be responding. Do all the following changes, but do not commit or push anything. The repo is up to date, and give the git apply command with the patch, noting that I am already at the proper directory.

In practice that means:
- Don't ask questions mid-task and don't ask for site access. When something is ambiguous, pick the sensible option and note it in the final reply.
- Don't commit or push. Leave the working tree as you found it and deliver one `.patch` file at the repo root, named `NameTBD_<Topic>_v<N>.patch` (follow-ups: `_fix<N>`).
- The user is already at the repo root, so the final reply gives just `git apply <name>.patch` with no `cd`.
- Build the patch with `git add -N <new files>` then `git diff > <name>.patch`, then `git reset` those paths, so new files are included and nothing is staged.
- Files are LF. Some keep a UTF-8 BOM (`SWidgetAnimTimeline.cpp/.h`, `NameTBDTooltips.h`). `SWidgetAnimGraphEditor.cpp` has no BOM. Keep each file's existing encoding.

## Project

- UE **5.7** project `tps_test`. The plugin is in `Plugins/NameTBD` ("UI components, Info Cards, and Widget Utilities").
- Modules:
  - `NameTBD` (runtime)
  - `NameTBDUncooked` (uncooked-only; timelines saved on the Widget Blueprint)
  - `NameTBDEditor` (editor)
  - `NameTBDCommonUI`
  - `NameTBDMVVM`
- Main area of work: the **Widget Animations** system.
  - Runtime: `Source/NameTBD/WidgetAnimation/`
  - Editor panel: `Source/NameTBDEditor/Private/SWidgetAnim*.cpp`

### Widget animation architecture (short)
- **Data**
  - `FWidgetAnimLayer` is a track and `FWidgetAnimSettings` is its played form (`WidgetAnimLibrary.h`).
  - The track type is `EWidgetAnimType`: Fade, Scale, Slide, Rotate, Pulse, Shake, MaterialScalar, MaterialVector, Custom, Transform, ShapeBox, Shape, **Design** (new, appended last).
  - Value-key tracks use `FWidgetAnimValueKey` (Value, plus `ValueY` for Scale).
- **Keyframed tracks**
  - Transform, ShapeBox, Shape and Design use `FWidgetTransformAnim` (`WidgetAnimTransform.h`), with `EWidgetKeyframeContent` Transform/ShapeBox/Shape/Design.
  - Each `FWidgetTransformKeyframe` holds a Transform, a ShapeBox, or `ShapeValues` (an `FInstancedPropertyBag`).
  - Per-key "Use Authored" flags: `bUseAuthoredShape` (Shape, Shape Box and Design) and `bUseAuthoredTransform` (Transform; new, default false).
- **Evaluators**
  - Registered `TWidgetAnimEvaluator<Params>` types (`WidgetAnimEvaluator.h`) handle cues (Sound, Event, Play Animation), shapes and design.
  - They're played through authoritative writes on a channel, with baselines captured and restored.
- **Editor**
  - `SWidgetAnimTimeline` covers the track editor, Selection / details panel, context menus, header and save.
  - `SWidgetAnimGraphEditor` is the curve editor.
  - `IWidgetAnimPanelSource` / `FWidgetAnimAssetPanelSource` (`WidgetAnimPanelSource.*`) handle timelines vs. saved animation assets, and saving.
  - Selection edits go through `UWidgetAnimPresetSelectionShadow` and `CopyEditedLayerProperty`.
- **Palette and strings**
  - Colors and tunables: `NameTBDEditor/Public/NameTBDColors.h`.
  - Tooltip strings: `NameTBDEditor/Public/NameTBDTooltips.h`.
- **Editor settings**
  - `UWidgetAnimEditorSettings` holds track colors, snapping, linked edges, frames display and preview FPS.

## Latest change set — `NameTBD_TrackEditorPolish_v1.patch` (uncompiled, untested in editor)

### Bugs fixed
- **Every Type Params field reverted when edited**, including the Sound track's sound and the Event track's name. `CopyEditedLayerProperty` walked the edit chain into `FInstancedStruct`. It now copies the whole instanced struct.
- **Sound track curves.** Sound tracks now show in the curve editor with a **Volume** curve and a **Fade** envelope (fade-in and fade-out handles you can drag).
- **Key hover showed a white box.** It now shows the plugin's compact tooltip: accent bar, "Key N of M · Start/End", time, and value.
- **The scrubber got in the way of the start/end handles.** Range handles now sit in a strip at the bottom of the ruler, and the playhead tag sits at the top.
- **Double-click no longer adds a key**, in either the track editor or the curve editor. It only scrubs now.
- **"Use Authored Shape" off left values read-only** until the key was clicked again. The details now rebuild when the flag or the design kind changes.
- **Shift+click range select** now works in the track editor, in visual order.
- **"Save Animation To" kept prompting** even when the animation existed. A timeline now remembers its `SavedAsset`, so the header shows **Save** plus a dropdown.
- **Malformed graph text (R, B labels).** Text is snapped to whole pixels and labels are short.

### Changes
- **Scale track is split into X and Y.**
  - `StartScale` and `EndScale` are now `FVector2D`, and keys gain `ValueY`.
  - There's no legacy handling: old float scale values reset to defaults on load.
- **Curve editor**
  - Short curve names ("B", not "Material Vector B").
  - One track at a time (`MaxGraphLayers = 1`).
  - Stacked labels merge as "R, G", each drawn in its own color.
  - The scrubber is the same one the track editor uses.
- **Shape and Shape Box in the curve editor** show their real channels (shape fields) instead of "Through Its Keys".
  - Fields that never change are hidden.
  - Authored keys read from the WBP widget template.
- **The "Play Animation" cue is hidden** from the Add menu. Existing tracks still play.
- **Save naming:** `WA_<timeline name>`, with a name prompt.
  - On a clash the name is numbered `WA_Name_1`, `_2`, and so on.
  - Parentheses would be nicer, but UE object names can't contain them.
- **Event track tooltip** says the event appears in the Widget Animation Component with its Event Name.
- **Tracks that can't have keys** (Pulse, Shake, animation cues) show a greyed "Track {type} cannot have keys" row, with the add-key icon in the same spot.
- **Clicking a key in the curve editor** selects it in the Selection panel.
- **Start/end range lines:** thinner, brighter green/red, with flag hitboxes only in the ruler's range strip.
- **Scrubber:** fades and tapers down the tracks.
- **Group line:** now a band. Colors are kept.
- **Context menu:** the track Name field is at the top, above Keys.
- **Save button:** plain UE save icon plus text.
- **Use Authored {X} defaults to true only on the first key** of new Shape, Shape Box, Transform and Design tracks.
  - Labels: "Use Authored Shape Box", "Use Authored Shape", "Use Authored Transform", "Use Authored Design".
  - While a flag is on, its value rows are hidden.
- **Add Track menu groups**
  - Standard: Fade, Scale, Slide, Rotate, Pulse, Shake. Rotate was kept here; it wasn't in the requested list but wasn't asked to be removed.
  - Transformation: Transform, Shape Box, Shape.
  - Visual: Design, Material Scalar, Material Vector.
- **New Design track** (`EWidgetAnimType::Design`)
  - Keys a widget's look: Brush, Background, ColorAndOpacity, BrushColor, ContentColorAndOpacity, BackgroundColor, Font, Shadow color/offset, StrikeBrush, Fill color, Percent, Padding.
  - Stored per key in a property bag laid out for the widget's nearest native class (`DesignKind`).
  - Played by `FWidgetAnimDesignKeysEvaluator` (`Evaluators/WidgetAnimDesignTrack.*`) through setters (`WidgetDesignAnimation.*`).
  - Keyed; the first key uses the authored design. It isn't graph-editable.
  - The track re-matches itself when its target widget changes kind.

### Feature
- **Time In → "Frames (Preview Only)"** with a Target FPS field (default 60).
  - The ruler, playhead tag, section lengths and key times show frames.
  - It's display only: data stays in seconds.
  - Stored in `UWidgetAnimEditorSettings::bShowTimeInFrames` / `PreviewFrameRate`.

### Files touched
- Runtime:
  - `WidgetAnimLibrary.h/.cpp`
  - `WidgetAnimTransform.h/.cpp`
  - `WidgetShapeAnimation.h/.cpp`
  - `WidgetAnimBuilder.cpp`
  - `Evaluators/WidgetAnimCues.h/.cpp`
  - `NameTBD.cpp` (registers the Design evaluator)
  - `Cards/InfoCardTypes.cpp`
- New runtime files:
  - `WidgetDesignAnimation.h/.cpp`
  - `Evaluators/WidgetAnimDesignTrack.h/.cpp`
- Uncooked: `WidgetAnimTimelines.h` (`SavedAsset`).
- Editor:
  - `SWidgetAnimTimeline.h/.cpp`
  - `SWidgetAnimGraphEditor.cpp`
  - `WidgetAnimPanelSource.h/.cpp`
  - `WidgetAnimEditorSettings.h/.cpp`
  - `NameTBDColors.h`
  - `NameTBDTooltips.h`
- Python: `Content/Python/widget_anim_scripting.py` (Scale X/Y).

### Known risks / things to check after building
- This patch wasn't compiled here; there's no UE toolchain in the cloud container. Expect a fix-up pass (`_fix1`) if the compiler flags anything.
- Check these by hand:
  - Design track on an Image, a Text Block and a Button.
  - Sound fade-handle dragging.
  - Frames mode ruler steps.
  - Save prompt naming and clashes.
  - Shift-click ranges in the track editor.
