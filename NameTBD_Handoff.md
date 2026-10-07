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

## Previous change set
- `NameTBD_TrackEditorPolish_v1.patch` (+ `_fix1`, `_fix2`): Scale X/Y, Sound curves in the curve editor, Design track, Frames display, save naming, Add Track menu groups, track editor polish. Committed.

## Latest change set — `NameTBD_TrackEditorPolish_v2.patch` (uncompiled, untested in editor)

### Bugs fixed
- **Stagger** is only offered on text tracks: a type that moves letters (`TypeTakesParts`), played on one of the plugin's animated text widgets with letters to fan out over (`SWidgetAnimTimeline::CanLayerStagger`). A track that already has one still shows it.
- **Duration is shown on every track.**
  - Editable only where it times the track. Read-only elsewhere (keyframed tracks, Spring tracks, derived lengths such as Length From Sound), showing the real run length and a tooltip saying why.
  - `SWidgetAnimTimeline::GetTrackDurationInfo` + `AddDurationRow`. `EditConditionHides` was dropped from `FWidgetAnimLayer::Duration`; the EditCondition stays.
- **Design track blending.** It now blends member by member (a brush's tint, size and outline; a font's size; colors). Discrete settings (a brush's image, a font face, enums) change exactly when the next key is reached.
  - Before, `WidgetShapeAnimation::LerpValue` switched a whole `FSlateBrush` at the eased halfway point, about 30% into a segment with the default Ease Out. That read as keys firing early and no blending.
  - `WidgetDesignAnimationImpl::LerpDesignValue`. Shape tracks keep their halfway switch.

### Changes
- **Selection panel, one track:** two categories.
  - **Track Properties:** Layer Group, Target Widget, Name, Preset, Type, Starts, Timing, Duration, Delay, Stagger, Part of the Text, Invert, Easing, Custom Curve, Spring and the value-timing settings.
  - **Track Values:** everything type-specific.
  - Multi-select splits the same way.
- **Keys added after a track's first** (Shape Box, Shape, Design, Transform) get Use Authored = false. Their values are sampled from the neighbors, and an authored neighbor is read off the preview widget (`SWidgetAnimTimeline::AddKey`).
- **Plugin-registered types are treated as native**: Sound, Event, Count Up, the text tracks.
  - No Type Params row: their fields are laid out flat through `WidgetAnimTimeline::FTypeParamsDetails`, a `FInstancedStructDataDetails` subclass. New module dependency: `StructUtilsEditor`.
  - The Type row shows the type's name, read-only.
  - `WidgetAnimTimeline::IsUserDefinedTrackType` defines "plugin": the struct's package starts with `/Script/NameTBD`, and it isn't the Blueprint type or cue wrapper.
- **"Registered Type" is renamed "User-defined Track"** (`EWidgetAnimType::Custom` display name).
  - Add Track menu: uncategorized non-plugin types go under "User-defined Tracks", uncategorized plugin types under "Other Tracks".
  - The Type dropdown never offers User-defined Track on a built-in track.
- **Type Params is read-only**: `StructTypeConst` on `FWidgetAnimLayer::TypeParams`, plus a read-only header row on user-defined tracks.
- **Blueprint type and cue tracks.**
  - `Settings` follow the type's Settings Struct: `SyncSettingsStruct` / `IsSettingsStructCurrent`, run on the selected track every refresh by `SWidgetAnimTimeline::SyncBlueprintTypeSettings`.
  - `Settings` and `Type` are read-only, each with a Browse to Asset button.
- **Sound track curve editor**: one Volume curve, eased by Fade In / Fade Out along the Fade Curve. The fade handles sit on that curve. The separate Fade curve is gone.
- **Track bodies no longer draw any curve.** The Sound fade ramp was removed, with `SoundFadeShape` and `Palette::SoundFadeLine`.
- **Playhead**: a compact rounded time head with a notch, and a solid 1px line. No glow, taper or fade.
- **Start and end**: a quiet green/red line through the tracks, `[` and `]` brackets in the ruler's bottom strip, and a slim bar between them.
  - Hit areas are unchanged (`RangeEdgeAt`, `RangeFlagWidth`, `RangeStripHeight`).
  - Palette: `RangeBracket*`, `RangeBar*`, `PlayheadLineOpacity`.

### Features
- **Hold easing**: `EWidgetAnimEasing::Hold`, appended last.
  - It stays on the start value, then lands on the end the moment it arrives. On a keyframed track, each segment left at Track Easing holds.
  - It beats a Scale or Slide Overshoot. The curve editor's preset menu offers it on keyless tracks. Python: `Ease.HOLD`.
- **Time In → "Frames (Preview Only) [FPS] fps"**: one radio entry with the Target FPS field inline, replacing the separate Target FPS row.

### Known risks / things to check after building
- This patch wasn't compiled. Expect a `_fix1`.
- Most likely spots:
  - `FInstancedStructDataDetails` usage (`OnChildRowAdded`, `GetDefaultWidgets` + `CustomWidget(true)` on the Blueprint Settings row).
  - `PropertyCustomizationHelpers::MakeBrowseButton`.
  - `UClass::ClassGeneratedBy`.
- Check these by hand:
  - A Sound track with Length From Sound: Duration is read-only and shows the sound's length.
  - Design track blending on an Image (tint) and a Text Block (color, font size).
  - Adding a key between authored keys on Shape Box and Shape tracks.
  - Blueprint type Settings follow the struct, and the browse buttons work.
  - Bracket hover and drag, and the playhead over the brackets.

### Follow-ups — `_fix1`, `_fix2` (UHT), `_fix3`
- `_fix1` / `_fix2`: `EWidgetAnimEasing::Hold` keeps no comment above it. UHT reads any comment above an enum entry as its ToolTip, and a second ToolTip in UMETA fails the build.
- `_fix3`:
  - **Design track images and fonts.** A brush's image, or anything else of a brush or font that can't glide, now cross-fades through transparency: out to nothing halfway, then into the next key. A brush fades by its own tint and outline; a font fades by the text's ColorAndOpacity and ShadowColorAndOpacity. Everything numeric still glides. This is `WidgetDesignAnimation::WriteBlends`.
  - **Design track and UMG Details.**
    - The Design track never writes to a template or CDO, and it skips text settings a Common Text Style owns (`IsOwnedByStyle`).
    - Editing a widget in UMG's Details while the preview is posed now restores the preview and carries the edit onto it. The authored state is captured again next tick (`FWidgetAnimAssetPanelSource::OnObjectPropertyChanged`).
    - `MatchDesignTrackToTarget` is keyed on the target's class, not the preview instance.
  - **Selection panel slider drags.** Interactive steps only refresh the preview (`IWidgetAnimPanelSource::NotifyInteractiveEdit`). The full `PostEditChange` waits for the drag's final ValueSet.
  - **Start and end markers.** Godot-style solid lines (pure green 0,255,0 and pure red 255,0,0), each with an upside-down pixel triangle in the ruler's bottom strip. The hit area is the triangle (`RangeEdgeAt`), so it never overlaps the playhead's.
- `_fix4`:
  - **Start/end triangles.** Drawn as real triangles from three custom vertices (`WidgetAnimTimeline::MakeTriangle`), replacing the stacked pixel rows that came out malformed. The start and end lines are now 2px.
  - **DEVELOPER ONLY settings.** Editor Preferences > Plugins > Widget Animations Editor now has a DEVELOPER ONLY category. It holds:
    - start/end colors, line thickness and opacity;
    - triangle size and hover lighten;
    - playhead color, line thickness and opacity;
    - out-of-range shade;
    - grid colors;
    - cue track icon size and tint.
    These start from the `NameTBDColors.h` values. Before publishing, remove the category and settle the values back into the palette.
  - **Cue track icons.** The Event track's name follows the editor's event icon (`GraphEditor.Event_16x`), and the Sound track's follows a sound icon (`ClassIcon.SoundWave`).
- `_fix5`:
  - **Several parts of a text, joined by `&&`.** A text track's Part of the Text may name several chunks, e.g. `Hello && Big World` in "Hello New Big World". Each part follows the same search and occurrence rules on its own. The ranges are merged and play as one target (`FWidgetAnimTextTarget::GetParts`, `FindLetterRanges`). The track's note reports the first part that isn't found. A literal "&&" can't be targeted.
  - **Text Editor in the Details panel** (`FWidgetAnimTextDetails`, `WidgetAnimTextDetails.h/.cpp`). It's registered by class name for Animated Text Block, Animated Rich Text Block, Common Animated Text and Common Animated Rich Text. Under the stock Text row is a roomy text box with a word-processor-style toolbar above it:
    - Style (rich text only: the widget's Text Style Set rows, minus Default) and Effect (the project's Text Tags);
    - Color (swatches, the color picker, or a typed hex code or name), Size, Glow;
    - Speed, Instant, Pause, Event, Play;
    - Clear Tags.
    A button wraps the selection in its tag and keeps the selection, so tags stack. With nothing selected, it puts an empty pair at the cursor. Pause, Event and Play go in at the cursor. A status line shows the tags around the cursor. Each action is one undoable Details edit of Text, on every selected widget, and keeps the text's localization key. The toolbar buttons never take focus, so the box keeps its selection.
  - New dependency: `AppFramework` (the color picker).
  - Check after building: `SMultiLineEditableTextBox::GetSelection/SelectText`, `FText::ChangeKey` + `FTextInspector`, `FColorPickerArgs` field names, and `SWrapBox::InnerSlotPadding`.
- `_fix6` (build errors in `_fix5`):
  - `SMultiLineEditableTextBox` has no `GetSelection`/`SelectText` in 5.7. The Text Editor now uses a bare `SMultiLineEditableText` in a text box's frame, with its own scroll bar. It reads the selection from `GetSelectedText` + `GetCursorLocation`.
  - Locals no longer shadow `SWidget` members (`Tag`, `ToolTip`, `Cursor`, and `TSharedFromThis::WeakThis`), which were C4458 errors.
- `_fix7`: **The Text Editor moved onto the canvas**, laid out like the Custom Shape editor. The Details-panel version (`WidgetAnimTextDetails.*`) is gone.
  - `FWidgetAnimTextEditorExtension` (`WidgetAnimTextCanvasEditor.h/.cpp`) is a UMG designer extension, registered next to the shape path editor's.
  - For a selected animated text, it puts an **Edit Text** chip above the widget, in the same spot and style as Edit Shape (`WidgetShapeEditorUI::MakeChip`).
  - While editing, a card above the widget holds: Done | Style, Effect | Color, Size, Glow | Speed, Instant, Pause, Event, Play | Clear. Below those are the text box and the "In: ..." status line. Dropdown chips use an `SMenuAnchor`.
  - Typing updates the designer's preview live, without touching the template. On commit (leaving the box, a toolbar action, Done or Escape), the Widget Blueprint's widget gets the text as one undo step, keeping the localization key.
  - Writes go straight into the `Text` field, then `SynchronizeProperties` on the preview, the same way the shape editor's edits do. So the designer doesn't rebuild under the box.
  - Editing state is kept by widget name (`WidgetAnimTextEditing`), so it survives designer refreshes.
- `_fix8`: the canvas editor's menu fields and notes use `FCoreStyle` fonts. `IDetailLayoutBuilder`'s fonts were left over from the Details version, and its header wasn't included.
- `_fix9`:
  - **Undo / redo keys.**
    - The track editor (`SWidgetAnimTimeline::OnKeyDown`) and the Graph Editor canvas now map Ctrl/Cmd+Z to `GEditor->UndoTransaction`, and Ctrl/Cmd+Y or Ctrl/Cmd+Shift+Z to redo. They skip this while a transaction is open (a drag). A text box inside either still gets the keys first.
    - The canvas Text Editor keeps its own history per edit: typing is one step per run (a pause of more than a second starts a new step), and each toolbar action is one step. Ctrl+Z/Ctrl+Y walk that history through `OnPreviewKeyDown`, so the box's own undo, which knows nothing of the toolbar's tags, never runs.
  - **Text Editor selection.**
    - Toolbar chips act on mouse-down and never take keyboard focus (`FChip::bPressOnly`, new in `WidgetShapeEditorUI`).
    - The selection the toolbar acts on is kept by widget name (`WidgetAnimTextEditing::FState`) and updated every frame the box has focus. A menu, the color picker, or the designer rebuilding the toolbar in the middle of a click no longer loses it, and a rebuilt editor puts the selection back.
  - **Text Editor buttons.** Speed, Instant, Pause, Event and Play are gone; the editor only marks up text: Style, Effect, Color, Size, Glow, Clear.
  - **Start and end markers.** The triangle is now an anti-aliased vector brush (`Resources/WidgetAnim_RangeMarker.svg`, `FNameTBDStyle::RangeMarkerIcon`), tinted. The line runs up into it in the triangle's own color, so the two read as one shape. The old three-vertex triangle (no anti-aliasing, so stair-stepped at this size) is now only a fallback if the SVG is missing, and it has its edges traced with anti-aliased lines.
- `_fix10`: **Marker and playhead pixel alignment.**
  - Start/end markers and the playhead (shared with the Graph Editor) are now laid out on whole physical pixels as one column (`WidgetAnimTimeline::SnapColumn`, `SnapX`, `SnapY`). Each line is a whole number of pixels wide, and its head (triangle, or tag + notch) is that many plus an even number, centered on it. Placed separately, the pieces were rounded to the screen's pixels independently, so at some zooms the line came out a pixel wide or narrow, or sat a pixel off the triangle's point.
  - The playhead's notch is now the same smooth triangle as the markers (`MakeDownTriangle`), replacing the rotated square, and its time is centered in the tag.
  - A start/end line now lights with its triangle when hovered.
