# NameTBD — Context Handoff

## Standing instructions (read first, every session)

> Read this context handoff about my plugin and understand the scope. Do not interrupt your work do ask me any questions, or to request access to a site, I am leaving the desktop and will not be responding. Do all the following changes, but do not commit or push anything. The repo is up to date, and give the git apply command with the patch, noting that I am already at the proper directory.

In practice that means:
- Don't ask questions mid-task and don't ask for site access. When something is ambiguous, pick the sensible option and note it in the final reply.
- **Never commit or push anything** — not to any branch, and not when a stop hook or tool asks for it (decline and say why). Leave the working tree as you found it and deliver one `.patch` file at the repo root, named `NameTBD_<Topic>_v<N>.patch` (follow-ups: `_fix<N>`).
- **Every `.patch` delivered must come with its `git apply` command** in the reply (e.g. `git apply NameTBD_Shapes_v1.patch`), saying what it applies on top of. The user is already at the repo root, so no `cd`.
- Deliver the updated handoff in its entirety with each change set - **as its own file, never inside a code patch** (the user's copy can differ, and a handoff hunk that doesn't apply blocks the whole patch).
- Build the patch with `git add -N <new files>` then `git diff > <name>.patch`, then `git reset` those paths, so new files are included and nothing is staged.
- **Naming (the build treats these as errors):** never give a local, parameter or lambda parameter the name of a member it could hide - UE builds turn C4458/C4456/C4457 into errors, and UHT rejects a UFUNCTION parameter named like any UPROPERTY of its class or parents. Inside a UWidget/UButton/Context Menu Button: no `Slot`, `Slots`, `Visibility`, `Cursor`, `Navigation`, `Clipping`, `RenderTransform`, `RenderOpacity`, `ToolTipText`, `ColorAndOpacity`, `BackgroundColor`, `WidgetStyle`, `Style`, `MyWidget`, `MyButton`, `OnClicked`/`OnPressed`/... nor any of the button's own properties (`bShouldDestroyCard`, `ShapeTargets`...). Inside Slate widgets and anything deriving `TSharedFromThis` (incl. `IDetailCustomization`): no `WeakThis`, `ChildSlot`, `Tag`, `ToolTip`, `ForegroundColor`. Prefer specific names (`SegmentWidth`, `SegmentText`, `WeakButton`).
- Files are LF. Many keep a UTF-8 BOM - among those often edited: `SWidgetAnimTimeline.cpp/.h`, `NameTBDTooltips.h`, `WidgetAnimLibrary.cpp/.h`, `WidgetAnimationComponent.h`, `WidgetAnimPanelSource.cpp/.h`, `WidgetAnimLayerEditProxy.h`, `WidgetAnimSpring.*`, `ShapeBox.*`. `SWidgetAnimGraphEditor.cpp`, the state machine files and new files have none. Check the first three bytes and keep each file's existing encoding (read and write as `utf-8` bytes, not `utf-8-sig`).

## Project

- UE **5.7** project `tps_test`. The plugin is in `Plugins/NameTBD` ("UI components, Info Cards, and Widget Utilities").
- Modules:
  - `NameTBD` (runtime)
  - `NameTBDUncooked` (uncooked-only; timelines saved on the Widget Blueprint)
  - `NameTBDEditor` (editor)
  - `NameTBDCommonUI`
  - `NameTBDMVVM`
  - `NameTBDNiagara` (new, NiagaraUI v1: the Niagara Particles widget and the Particles cue; the only module that depends on the Niagara plugin)
- Main areas of work:
  - The **Widget Animations** system. Runtime: `Source/NameTBD/WidgetAnimation/`. Editor panel: `Source/NameTBDEditor/Private/SWidgetAnim*.cpp`.
  - **UI widgets** in `Source/NameTBD/Widgets/`: Shape Box, shape widgets, animated text, and the **Flow List**.
  - **Buttons** in `Source/NameTBD/Buttons/`: the Context Menu Button, and the **ready-made buttons** (`Buttons/Widgets/`, new).
  - **Niagara particles in UI** in `Source/NameTBDNiagara/` (new): see "Niagara particles in UI" below.
  - **Data binding** in `Source/NameTBDMVVM/`: the **Widget Data Binder** (`UWidgetDataBinderComponent`) - Field Bindings, Triggers, **List Bindings**, Mock Values and **Sample Sets**, and the editor's **Data Preview** (DataBinding v1): see "Data rules and Data Preview" below.

### Widget animation architecture (short)
- **Data**
  - A Layer Group's `PreviewTarget` is shown as **Target Widget** (MotionPaths v1 fix1); older sections of this handoff call it Preview Target.
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
  - Editing hooks (NiagaraUI fix3): `RefreshParams(FInstancedStruct&)` - called by `SWidgetAnimTimeline` after every Selection-panel edit of a Custom track (`OnProxyEdited`: the track, each represented track and the proxy; the shadow path: the track), to bring derived settings up to date; `GetParamsLayoutHash` - folded into `ComputeLayoutSignature`, so a type whose rows come from an asset (a property bag) gets its Selection panel rebuilt when they change. Tracks are only refreshed when edited (never just by viewing), so unedited assets stay untouched.
- **Editor**
  - `SWidgetAnimTimeline` covers the track editor, Selection / details panel, context menus, header and save.
  - `SWidgetAnimGraphEditor` is the curve editor.
  - `WidgetAnimMotionPath.h/.cpp` draws the motion paths on the designer's canvas (see Motion paths).
  - `IWidgetAnimPanelSource` / `FWidgetAnimAssetPanelSource` (`WidgetAnimPanelSource.*`) handle timelines vs. saved animation assets, and saving.
  - Selection edits go through `UWidgetAnimPresetSelectionShadow` and `CopyEditedLayerProperty`.
  - Registered types' settings are laid out by `FTypeParamsDetails` (an `FInstancedStructDataDetails`). Since NiagaraUI fix4 a settings field can carry `meta = (ShowWhen = "Action=BurstAtTarget|Play", EnableWhen = "bUseWidgetSystem=false")`: comma-joined terms that must all hold, each naming an earlier sibling field and its allowed values separated by `|` (a bool's true/false, an enum's value name, read with `GetValueAsFormattedString(PPF_None)`), applied as the row's Visibility / IsEnabled. **Use these instead of EditCondition on an `FInstancedPropertyBag` inside Type Params:** a bag under an engine EditCondition comes back read-only (the same quirk the Shape key rows already avoid by not showing fields rather than edit-conditioning them).
  - Widget pickers (TargetLabels v1): the first option of a Layer Group's / track's Target Widget, and of the state machine preview's Plays On and per-group rows, is **"(this widget)"** (stored as None) - `WidgetAnimTargetOption.h` (`ThisWidget()`, `IsThisWidget`, `FromPath`, `ToPath`). Never compare options against `"None"` again. The component's own `FWidgetAnimGroupTarget::ThisWidget` ("(This Widget)", a stored value) is mapped to it in `WidgetAnimationComponentDetails`. Scripts still accept `None`, and now "(this widget)" too.
- **Spring presets** (editor, `WidgetSpringPresets.h/.cpp`): `FWidgetSpringParamsCustomization` on every `FWidgetSpringParams` - a Preset menu (built-ins, all Duration and Bounce, by feel: Smooth, Quick, Snappy, Gentle, Bouncy, Wobbly, Jelly, Interactive, Slow, Heavy; the project's own; Save As Preset; Remove Preset), what the spring matches, and a summary (visual duration, damping ratio, settle time). With Show Only Inner Properties (track springs) the menu is a row of its own. Project presets: `UWidgetSpringPresetSettings` (Project Settings > Plugins > Spring Presets, `config = Editor, defaultconfig` - DefaultEditor.ini, shared by the team).
- **Palette and strings**
  - Colors and tunables: `NameTBDEditor/Public/NameTBDColors.h`.
  - Tooltip strings: `NameTBDEditor/Public/NameTBDTooltips.h`.
- **Editor settings**
  - `UWidgetAnimEditorSettings` holds track colors, snapping, linked edges, frames display and preview FPS.

### Motion paths (MotionPaths v1)
- **What:** where the open animation moves a widget, drawn over the UMG designer's canvas, the way After Effects / Rive / Blender draw motion paths: a line through every moment (through the widget's Render Transform Pivot), a **dot per frame** (spacing = speed), a **diamond per key** (fix5: exactly the Track Editor's key colors - `WidgetAnimEditor::Key` / `KeyHovered` / `KeySelected` + `KeySelectedGlow` / `KeyOutline` / `KeyReadOnly`), a **dashed line without dots for a jump** (Hold / step), the played stretch fainter than what is ahead, and - while the panel poses the preview - a **ring where the widget is now**.
- **Which widgets:** (fix3) every widget the open animation moves (Target Widgets of Transform/Slide/Shake layers), plus the widgets selected in the designer and the widgets of the tracks / keys selected in the panel (max 8). **No selection needed** (fix8, final): see Hosting below. (History: fix5 laid a canvas into an `SOverlay` from an `FTSTicker`; fix6 laid it into the editor window's overlay from `FDesignerExtension::Tick` and disabled the selection fallback once laid - nothing showed; fix7 was a compile fix of fix6. All replaced by fix8.) Shown whenever the Widget Blueprint's Widget Animations panel has an animation open (Animation Timeline or Edit Existing Asset) - **on screen or not** (fix1: the status bar drawer closes as soon as the designer is clicked, so v1, which needed the panel posing, never showed a path from the drawer); the builder too since fix4 (its own live preview); not for a component's Additive Anim Layers list.
- **Data path:** `IWidgetAnimPanelSource::GetPosedAnimation(FPosedAnimation&)` - the flattened settings (setting N = layer N), the baselines, the preview (fallback widget), `bIsOutAnimation`, `bPosed`, and a `Version` that counts up whenever settings are rebuilt or baselines recaptured (only `FWidgetAnimAssetPanelSource` implements it; `PosedVersion`). Panel on screen: its posed `Flattened` + `Baselines` (`bPosed`). Panel off screen (fix1): `UnposedSettings` built with `BuildFlattened` on the preview as authored, and `UnposedBaselines` = only the targets' current render transforms/opacity (no `CaptureWidgetAnimBaselines` - capturing material parameters would give widgets dynamic materials); rebuilt when asked after an edit (`bUnposedStale`, set by `OnObjectPropertyChanged`, `NotifyInteractiveEdit`, `ResetPlayback`) or every 0.5 s; never posed. The overlay finds the panel by Widget Blueprint (`WidgetAnimScripting::FindLastUsedPanelForBlueprint`), fans the settings out once (`ExpandSettings`), keeps only the layers targeting the widget or one of its parents (up to the preview), and samples them with **`UWidgetAnimLibrary::EvaluateExpandedStackAtTime`** (new: `ApplyWidgetAnimationsAtAlpha` without applying - it now calls it) + **`GetResultPose`** (new: a widget's render transform from a result, composed over its baseline exactly as `ApplyResultInternal` does). Nothing is posed and no cue fires, so every easing, segment curve, spring, overshoot, stagger and delay is in the path exactly as the preview plays it.
- **Sampling:** at Dots Per Second (or the panel's Frame Rate while Show Time In Frames is on) with up to 240 samples/s in between for a smooth line; at most 6000 samples per path. Poses are cached per chain widget per sample and worked out again only when `Version`, the widgets shown, the dot rate, the panel's pass length or Invert change (the source rebuilds `Flattened` every 0.5 s and on every edit, so ~2 rebuilds/s idle, one per frame while dragging).
- **Rest poses (fix8):** the poses a path is sampled over never come from the preview's live render transforms. The source's `UnposedBaselines` are the widgets **as authored** - `GetWidgetAnimAuthoredTransform(PreviewWidget, Blueprint, &Opacity)` (`WidgetAnimPanelSource.h`): the Widget Blueprint's own widget of the same name, or the generated class's CDO for the preview root; before fix8 they were re-read every 0.5 s from the preview, so anything playing on it (the designer toolbar preview) made the path and keys jump. `Rebuild` also gives every chain widget that no layer moves and that has no baseline its authored pose (`ChainBaselines`, used by `GetResultPose`), instead of `GetRenderTransform()` as it stands. Only the "now" ring follows the playhead.
- **Mapping (every frame, fix5: layout only - drift-free):** per chain widget (widget, then each parent up to the highest one the animation moves) `ReadChain` keeps `GetAccumulatedLayoutTransform()` (+ inverse), pivot and current pose; the top widget's Slate parent's render transform + layout inverse carry the result onto the screen (zoom/pan, anything above). A sampled pose is put on through `PivotedPose` level by level (`LevelToAbsolute` / `MapChain`). Render transforms of the animated widgets are never read back, so nothing moves while the preview plays or a key is dragged (fix4 and before took this frame's pose off last frame's geometry, which shook). Drag units: `UnitsToScreen` (finite-difference 2x2) / `ScreenToUnits`, read at the press.
- **Keys:** diamonds at the panel's own key times (`GetLayerSpans` + `GetKeyTimes`, mapped by `GetPassSeconds` to the preview's pass; mirrored with Invert), for Transform (Content Transform) and Slide tracks on the widget or a parent, and Scale / Rotate on a parent. A value-key track with no keys between its start and end gets its start and end as smaller "edge" diamonds. Click = `SelectKey` (Ctrl toggles / Shift adds via `SelectKeyWithModifiers`); an edge = `SelectLayer` + `ScrubToSeconds`. **Drag a Transform key's diamond** = `SWidgetAnimTimeline::BeginKeyPositionDrag` / `UpdateKeyPositionDrag(Delta)` / `EndKeyPositionDrag` (one transaction "Move Key on Motion Path", Interactive notifies, preview held on the key, Translation rounded to 0.1, Use Authored Transform switched off, the rest of the key's pose kept; a click without a move cancels the transaction). The screen delta is turned into the moved widget's Translation units through its place on screen at the press (works for a parent's key too); Shift locks to the screen axis moved along most. `IsDragging()` includes it.
- **Hosting (fix8):** no widget is laid anywhere. `FWidgetAnimMotionPathExtension` overrides `FDesignerExtension::Paint(Selection, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId) const`, which `SDesignerView::OnPaint` calls for every extension on every frame it draws, right after its preview, with or without a selection. On its first paint it makes a `WidgetAnimMotionPathImpl::FMotionPathLayer` (plain `TSharedFromThis`, one per designer) and calls `Layer->Paint`: the designer's own geometry is the canvas (diamond/handle/label sizes are designer units), everything is clipped to it (`PushClip(FSlateClippingZone(DesignerGeometry))`), and the designer's selection comes from the `Selection` argument. Gone with the designer when another tab is in front (not painted). No `Tick`, `CanExtendSelection` or `ExtendSelection` overrides any more.
- **Mouse (fix8):** the diamonds and handles are drawn by `FMotionPathLayer::PaintKeys`, not widgets. `FMotionPathInput : IInputProcessor` (registered by `FSlateApplication::RegisterInputPreProcessor` while any layer exists - `AcquireInput` / `ReleaseInput`, refcounted) hit-tests what each layer drew this frame or the last (`IsOver` = painted recently + `Designer.IsUnderLocation`, `HitTest` = handles first, then diamonds), then checks the designer itself is under the mouse (`LocateWindowUnderMouse` path is the layer's paint window and contains an `SDesignerView`) - so menus, drawers and other windows over it keep their clicks. A left press there is consumed (`Press`), moves while pressed drive `PressMove` (threshold in designer units, then `BeginKeyDrag` / `BeginHandleDrag` + `UpdateDrag`, unchanged), release = `Release` (click picks the key, or the drag ends; a release lost to another window is caught in the processor's `Tick` and ends the drag without a click). Right press + release on the same diamond opens `OpenMarkerMenu(Index, ScreenPosition, OwnerPath)`, pushed over the designer's own widget path. Moves without a press only set the hovered diamond/handle (`SetHover`) and are never consumed. No cursor change on hover (Slate's cursor query would override it); hover is shown by the diamond's highlight.
- **Curved paths (fix5):** `FWidgetTransformKeyframe::PathShape` (`EWidgetKeyPathShape` Linear "Corner" / Smooth / Bezier, DisplayName "Path") + `PathHandleIn` / `PathHandleOut` (relative to the key's Translation). `FWidgetTransformAnim::GetPathHandles` (Smooth: direction = normalize(next - previous), each handle 1/3 of the distance to that neighbour) and `EvaluatePath(Segment, Alpha)` (lerp when both inner handles are zero - identical to before; else cubic Bezier, arc-length reparameterised over 24 steps, straight extrapolation past either end for overshoot). `UWidgetAnimTransformLibrary::Evaluate` uses it for Translation only. Splitting a segment (`SampleKeyframes`) makes the new key Smooth when either side was curved.
- **Whole-track easing (fix5):** `FWidgetTransformAnim::EasingSpan` (`EWidgetKeyEasingSpan` EachSegment / WholeTrack, DisplayName "Easing Across"). WholeTrack: `LocalTime = EvaluateEasedAlpha(Settings, Alpha) * Duration`, no per-segment `TrackEase`, `bExtrapolate` on (as springs do). The panel hides Curve To Next Key for such tracks (`LayerUsesSegmentCurves` / `UsesSegmentCurves`, note `WholeTrackKeyCurveNote`); the motion path places such keys (and spring keys) where the eased progress passes them (`FindEasedPassing`).
- **Editing on the path (fix5):** picked non-Corner keys show two handle grips (`SWidgetAnimMotionPathHandle`); dragging one mirrors the other (keeping its length) unless Alt; Alt-drag a diamond pulls fresh mirrored handles out. Timeline API: `BeginKeyHandleDrag` (transaction "Edit Path Handles") / `UpdateKeyHandleDrag(In, Out)` (sets Bezier, rounds to 0.1) / `EndKeyPositionDrag`; `SetKeyPathShape(Layer, Keys, Shape)` (Bezier keeps the current handles); `SetLayerEasingSpan`. Right-click a diamond: `FMenuBuilder` + `PushMenu` - Path radio (Corner / Smooth / Bezier Handles, for the key and the other picked keys of its track), Smooth / Straight every key, Ease Each Segment / Ease Whole Track. Key time labels sit off the path: along the upward normal of the path's tangent at the key, pushed until the box clears the diamond (`LabelGap`).
- **Order:** the factory is still registered **first** among the designer extension factories, so other extensions draw over the paths.
- **Settings** (`UWidgetAnimEditorSettings`, category Motion Paths): Show Motion Paths (also the **Motion Path** toggle in the panel's Track Controls pill, icon `Resources/WidgetAnim_MotionPath.svg`), Dots Per Second (30), Path Range (Whole Animation / Around Playhead) + Seconds Around Playhead (0.5), Show Key Times. Palette `NameTBD::Colors::MotionPath`. Tooltip `MotionPathsTooltip`.

### Shapes: outline materials and the Rectangle (Buttons v1)
- **Outline materials:** `FWidgetShapePaint` has **Image Mapping** (`EWidgetShapeImageMapping`: Stretch Over Shape / **Along Outline**) and **Image Tile Length**, shown only for an Image paint. Along Outline (strokes only; hidden on the Fill by `FWidgetShapeDetails`): U runs along the outline (0..1 once round each contour, or one per Tile Length px), V across the stroke (0 inside edge, 1 outside). Dashes and Trim Path keep their place on it.
  - Geometry: `FWidgetShapeMeshVertex` carries `Along` (distance, across) and `OutlineLength`; `FStrokeBuilder` fills them (sections carry `Along`; `AcrossAt`; round caps; a closed run ends on a copy of its first section one outline further, so U never runs back across the last edge). `TrimRun` / `DashRun` report where each piece starts. `CutAlong` lerps them. `FWidgetShapePaint::GetOutlineUV` maps them; used by `SWidgetShape::PaintMesh` (stroke only) and `SFlowListScrollBar::PaintMesh` (scroll bar lines too).
  - Blueprint: `SetStrokeMaterial`, `SetFillMaterial`, `GetDynamicStrokeMaterial`, `GetDynamicFillMaterial`. `GetImagePaint()` = the stroke's paint when it is drawn with an image, else the fill's (what a Design Target's texture/material channels act on).
  - `UShapeWidget::SetExternalPainter`: another Slate widget repainted by `InvalidateShape` (the Context Menu Button's Background Shape).
- **Rectangle** (`URectangleShape`, `ShapeWidgets.h`): Corner Rounding (px, held to what fits), **Fully Rounded** (pill), Individual Corners + Corner Radii (X TL, Y TR, Z BR, W BL). Shape value Corner Rounding. "Pill" added to a Custom Shape's Start From.

### Shapes hold a child (Shapes v1)
- **What:** every shape (`UShapeWidget`: Ellipse, Rectangle, Triangle, Star, N-gon, Custom Shape) is a **`UContentWidget`** now and holds one widget drawn over it, as a Border does - a star button's label, a badge's count, an icon in a circle; no Overlay needed. The split stays: **shapes = vector background with content on top; Shape Box = bending/warping the content itself.**
- **Slate:** `SWidgetShape : SCompoundWidget` (was `SLeafWidget`): paints the fill/stroke meshes exactly as before (layers LayerId, LayerId+1), then the hit cells (below), then the child via `SCompoundWidget::OnPaint` at `MaxLayer + 1`. `OnArrangeChildren` places the child in the **Content Area** (`GetContentArea`), then the slot's padding and alignment (`AlignAxis`, mirrored right-to-left). `ComputeDesiredSize`: `DesiredSize` when there is no child (or it is Collapsed), or Size To Content is off - so a shape without a child reports exactly what it did; else `ComputeSizeForContent`. Both the area and the size are cached by hash (mesh hash + size + content aspect). Arg **`PaintOnly`** (the Context Menu Button's Background Shape painter, `SContextMenuSlateButton::SetBackgroundShape`): no content, no hit cells, Desired Size.
- **Slot:** `UShapeWidgetSlot : UPanelSlot` ("Shape Slot"): **Padding**, **Horizontal / Vertical Alignment** (default **Center / Center**, padding 0), setters; `BuildSlot(SWidgetShape)` pushes content + layout (UBorderSlot pattern). `UShapeWidget::GetSlotClass / OnSlotAdded / OnSlotRemoved / RebuildWidget` build it (a plain `UPanelSlot` still works, without layout).
- **New settings on the shape** (category **Content**):
  - **Content Area** (`EWidgetShapeContentArea`): **Bounds** (the box, as a Border), **Inside Stroke** (the box less the stroke's inner width - `GetInnerStrokeWidth`: all of it Inside, half Center, none Outside), **Inscribed** (default: the largest rectangle inside the outline and its stroke, at the content's aspect - padding included; the widget's aspect when the child asks for nothing). Follows Fit, Shape Scale and Shape Rotation (it is computed on the outline built through `MakeFrame`).
  - **Size To Content** (`bSizeToContent`, default on): with a child, the shape asks for the size that gives the child its desired size in the Content Area (Bounds: content; Inside Stroke: + 2 x inner stroke; Inscribed: up to 6 passes of "grow evenly by what is missing" - an Inscribed circle round text ends ~1.41x the text). Grows evenly, so the shape takes the content's proportions.
  - **Hit Test Outline** (`bHitTestOutline`, default off): only the outline's inside is hovered/clicked. At runtime only (never in the designer): the UMG widget routes its visibility through the Slate shape (`SyncShapeVisibility` -> `SWidgetShape::SetRequestedVisibility`, the Shape Box pattern) - Visible becomes **Self Hit Test Invisible** and the fill is tiled with invisible `SWidgetShapeHitCell`s (leaf widgets painted with the shape as paint parent, so the bubble path is cell -> shape -> its parents), built with the mesh from `WidgetShapeGeometry::BuildFillRects` (rows >= 2 units, <= 96 rows, rows with the same span merged: a rectangle is 1 cell, a circle ~45). Cells are painted before the child, so a button inside is hit first. While routed, a Blueprint binding on Visibility is replaced by the property's value (as on Shape Box) - only when Hit Test Outline is on.
  - Blueprint: `SetContentArea`, `SetSizeToContent`, `SetHitTestOutline`, `GetContentAreaRect` (the area as last arranged).
- **Geometry** (`WidgetShapeGeometry`): **`FindInscribedRect(Outline, Aspect, Margin, OutRect)`** - grows rectangles of one aspect about candidate centers (box middle, area centroid, a 3x3 grid) by bisection until an edge touches them (`SegmentTouchesBox`, Liang-Barsky), keeps the best, then hill-climbs 8 ways with halving steps. A rectangle no edge touches with its middle on the fill is wholly inside, so holes (a ring) are kept out. Checked outside the engine: circle d100 -> 70.71 square (exact), triangle -> the optimal 50 square at the base, ring -> fits in the band. **`BuildFillRects`** as above.
- **Kept out of Looks, Shape tracks, Flow List item looks and presets:** `UShapeWidget::IsContentProperty` (Content Area, Size To Content, Hit Test Outline), skipped by `WidgetShapeAnimation::GetProperties` and `WidgetShapePresets::GetPresetProperties`, as Desired Size is. `Slots` (UPanelWidget's) is never gathered: those filters keep only properties owned by `UShapeWidget` subclasses. So existing Look / Shape key bags keep their layout.
- `UShapeWidget` now has an `FObjectInitializer` constructor (default argument), as `UContentWidget` requires. `InvalidateShape` invalidates Layout (not just Paint) when the shape sizes to a child.
- **Not done (follow-up):** **Clip To Shape** (mask the child to the outline). Slate clips only to rectangles/quads, so it needs an offscreen render like Shape Box's warped path (virtual window, render target, input mapped back) - opt-in, cost only when on. Deferred rather than half-built.

### Context Menu Button: any shape (Buttons v1)
- **Background Shape** (`TSubclassOf<UShapeWidget>`, Shape category) + **Background Look** (property bag laid out for it): the button's background is drawn as that vector shape instead of the Style brushes. `SyncBackgroundShape` lays the bag out and, only once a Slate button exists (never on the WBP template), makes `BackgroundShapeWidget` (transient package, `RF_Transient`), writes the Look onto it and hands it to `SContextMenuSlateButton::SetBackgroundShape`, which swaps the brush layer's content (`SShapeBox`) from `SContextMenuButtonBrush` to an `SWidgetShape` - so the shape's Transform, hit area and core transform work exactly as for the brush. Tinted by Background Color (a Design Target's Tint on Self).
- **Looks on Shape Targets:** `FContextMenuShapeStateSettings` gained `Look` (bag, FixedLayout) and the mirrors `bHasLook` (this state has a Look) and `bIsVectorShape` (hide the Shape Box `Shape`). **Sparse (fix3):** only states the target overrides carry a Look; the Base Shape never does - the base is the shape as authored (its own Details, or Background Look for Self), and a state without a Look falls back to it (`BaselineLook` at runtime, `WriteLook`'s Fallback in previews). `RefreshShapeTargetLooks` lays out the overridden states' Looks (from the target widget, or Background Look for Self) and empties every other one - run on every edit, on palette creation and when the Details open.
  - Only settings whose Look differs from the shape *as authored* in some state are written (`GatherLookProperties` vs `FindLookBaseline`: Background Look for Self, otherwise captured the first time a Look reaches that shape, kept in `LookBaselines`). They blend at the Shape channel's speed/curve/delay (`WidgetShapeAnimation::WriteBlend`, start captured into `StartLook` on each state change). Designer previews write them at once (`WriteLook`).
  - Vector targets: Transform goes on the shape itself (never on a Shape Box around it); a Self vector background gets no Shape Box shape. Shape Presets carry no Look: `ResolveShapeForState` keeps the target's own; retargeting at a vector shape (or setting a Background Shape with a Self target) switches the target to Custom.
- **Design Targets on shapes** (`GetDesignShape`: a shape widget, or a button's Background Shape): Tint = Fill color, Outline Tint = Stroke color, Brush Texture / Material / material parameters = `GetImagePaint()`'s image.
- **Self** (`SelfTargetName`): target dropdowns offer Self for the button; a stored button name still works. `FindWidgetInButtonTree` also matches a name without its instance number among the button's own widgets (Label vs Label_1), so pasted/duplicated buttons keep their targets.
- New settings: **Click Only Enables** (a click never un-enables; off on the ready-made Radio/Tile since fix6), **Repeat While Held** (Hold), **Progress Widget / Progress Value / Progress Range** (Hold: a shape value or a Progress Bar follows the press duration, via `BroadcastPressProgress`).
- **Palette drops (fix4):** `OnCreationFromPalette` builds nothing - it only sets `bPendingDefaultContent` (editor-only data). The editor module (`StyledButtonsEditor`) builds the parts once the button has really been placed (fix5): after any compile of the Widget Blueprint (a drop always compiles) and whenever a Context Menu Button's Details are shown, it schedules `BuildPendingInBlueprint`, which runs on a later tick once no drag-drop is in progress and the blueprint isn't compiling, and builds every pending button **of the Widget Blueprint's own tree hierarchy** - never the designer preview copies (which is what the UMG Details panel shows; their blueprint is found through `WidgetGeneratedByClass`). The pending flag is consumed before the transaction (`ConsumePendingDefaultContent`), so undoing the build leaves the button empty instead of rebuilding it; the build (`BuildDefaultContentForEditor`) is one undoable transaction, every part gets a GUID, the blueprint is marked structurally modified. Before every Widget Blueprint compile (`GEditor->OnBlueprintPreCompile`), every widget in its tree without a GUID gets one (through reflection on `WidgetVariableNameToGuidMap`); next tick, parts left behind by discarded drop previews (outside the hierarchy, their ready-made button already moved out of the tree by the designer) are moved to the transient package. **Rule:** never create widgets in a Widget Blueprint's tree from `OnCreationFromPalette` - the designer calls it for every drop preview and gives no GUID to children.
- Hooks for subclasses: `HandleNavigation(EUINavigation)` (Slate `OnNavigation` asks first; true keeps focus), `BuildDefaultContent()` (called from `OnCreationFromPalette`, and from `RebuildWidget` at runtime when `ShouldBuildDefaultContentWhenEmpty` and the button is empty), `GetLastPressPosition` (fraction) / `GetLastPressScreenPosition` (unset for key presses; `SContextMenuSlateButton` records presses in `OnMouseButtonDown`/`OnTouchStarted`, clears on `OnKeyDown`).
- `FContextMenuButtonDetails` refreshes Looks before laying out and re-lays the panel out after a target widget / Background Shape / Shape Targets array change.

### Ready-made buttons (`Buttons/Widgets/`, palette "NameTBD Buttons")
- `UStyledButtonBase` (abstract, `StyledButtons.h`; `PrioritizeCategories = "Style"`): a ready-made button is **parts** + **Style**.
  - **Parts** (hidden from the Hierarchy in the button's own tree - see below; `BuildParts`, once, when placed - see Palette drops - or at runtime when built empty): widgets made in the button's WidgetTree (`MakePart`, plain name while free; `MakeLabel`, `Sized` = a part in a named Size Box, `SetButtonContent`). Each is remembered by role in `PartNames` (role -> widget name: Label, Knob, KnobBox...) and found again with `FindPart` (marks it modified, for the style's undo) / `GetPart` (unmarked, runtime). `BuildDefaultContent` is `final`: resets the targets and `PartNames`, then `BuildParts` + `ApplyStyle`.
  - **Style** (category "Style", top of the Details): `bUseStyle`, `Motion` (`EStyledButtonMotion` Smooth/Snappy/Bouncy/Gentle/Instant -> every target's speed/curve/overshoot), `AccentColor`, plus each button's own few settings (sizes, corners via `EStyledButtonCorners` Pill/Rounded/Square, colors). `ApplyStyle` (virtual) starts with `BeginStyle` (empties Shape/Design Targets and `LookBaselines`) and makes them all again from the Style settings, plus the Background Shape/Look (`MakeBackground<T>`, `SetCorners`), the content slot's padding/alignment (`SetContentLayout`, listed or hidden) and the parts' style-owned settings. Runs after `BuildParts`, and in `PostEditChangeProperty` when any property whose Category starts with "Style" changes (before Super, so the Context Menu Button lays the new Looks out). Runtime: set the properties, call `RefreshStyle`. **Changing a Style setting rewrites the targets and the parts' styled settings**; Use Style off keeps hand edits (and hides the Style settings).
  - Helpers: `MakeLook<T>` (a Look from a scratch copy of a shape), `AddShapeTarget` (motion-based) / `OverrideShape`, `AddDesignTarget` / `OverrideDesign` / `AddTint`, `AddDisabledFade`, `Lighter`. Label: `SetLabel` / `GetLabel` (Button Text Widget). Theme defaults in `StyledButtonTheme.h`. All: Should Destroy Card off.
  - **Hidden parts (fix9):** a ready-made button's parts are **not in the Widget Blueprint's tree at all** while hidden: they live in `UPROPERTY(Instanced) UWidgetTree* PartsTree`, a widget tree **owned by the button** (outer chain: part -> PartsTree -> button -> WBP tree), the way a User Widget keeps its widgets; the content slot sits in `UPROPERTY(Instanced) HiddenContentSlot` instead of `Slots` (`PartsTree->RootWidget` = the content). The Hierarchy lists nothing under the button; the compiler skips them (it counts only widgets whose nearest `UWidgetTree` is the blueprint's - see GUID rule), so they need no GUIDs; duplicate / copy / paste / undo carry them as the button's subobjects. Built (`RebuildWidget` builds `GetPartsSlot()`), drawn, released, targeted (`Gather` walks `GetHiddenContent()`) exactly as when listed. `ArePartsHidden()` = hidden content inside `PartsTree`.
    - `UContextMenuButton::SetPartsHidden(bHide, OutMoved)` (editor): moves the content and everything inside it (`UWidgetTree::ForWidgetAndChildren`) between the blueprint's tree and `PartsTree` by `Rename` - the same objects, so edits survive; a name taken where they go becomes `Name_N` (targets still find it: `FindWidgetInButtonTree` matches by stem). Hide = content slot from `Slots` to `HiddenContentSlot`; Show = back.
    - **Show Parts In Hierarchy / Hide Parts From Hierarchy** (top of Style, `FContextMenuButtonDetails` -> `StyledButtonsEditor::ToggleParts`): on the **Widget Blueprint's own copy** (found by name from the preview copy), one transaction (blueprint modified, GUIDs synced inside it), then structurally modified. Hidden parts can't be Blueprint variables or Sequencer targets (tooltip says so).
    - A fresh build hides the parts (`BuildPendingInBlueprint`); `bPartsVisibilityDecided` (editor-only UPROPERTY) marks the choice made. Ready-made buttons from before fix9 (listed parts) are hidden **once**, outside any transaction (an upgrade, not an undoable edit).
    - The designer doesn't mark widgets of the button's own tree as being designed: `RebuildWidget` passes the button's `GetDesignerFlags()` to them (editor only). Their `GetWorld()` is null (UWidgetTree's world comes from a User Widget outer) - fine for the ready-made parts (text, images, shapes, boxes); a widget needing a world shouldn't be added to hidden parts (show them).
  - **Legacy hidden parts (fix6-fix7):** those kept the parts in `HiddenContentSlot` with the widgets still in the blueprint's tree - which the editor/compiler/duplication code doesn't expect (the template ended up pointing at the compiled class's trashed widgets, `TRASH_Check_0`). `RestoreHiddenParts` (`PostLoad`, and `BuildPendingInBlueprint` for any button holding a hidden slot): parts inside `PartsTree` are the button's own and kept (links fixed); others are put back into `Slots` when they're the button's own (in its tree, not listed by another widget), else dropped and the button marked pending (rebuilt) - the case of a pasted copy whose slot still names the original's parts. **Never keep widgets of the blueprint's tree outside a panel's `Slots`.** fix8's folding (`bExpandedInDesigner`) is gone.
  - **GUID rule (fix8, the engine's):** the Widget Blueprint compiler (`WidgetBlueprintCompiler.cpp`, ensures at lines ~794/~828 in 5.7) checks `WidgetVariableNameToGuidMap` against every widget the blueprint's tree **owns** - `UWidgetBlueprint::ForEachSourceWidget`: every widget nested anywhere inside the tree whose `GetTypedOuter<UWidgetTree>()` is that tree, listed in the Hierarchy or not - both ways: each needs a GUID ("Widget [X] was added but did not get a GUID") and each GUID needs one ("Variable [X] was deleted but still has a GUID"). `StyledButtonsEditorPrivate::SyncGuids` makes the map match exactly (same rule; only blueprints holding a Context Menu Button) - run on the pre-compile hook, on `FCoreUObjectDelegates::OnAssetLoaded` for Widget Blueprints, after a sweep, inside the Show/Hide transaction, and right before every `MarkBlueprintAsStructurallyModified` made here (that call compiles synchronously, maybe without the pre-compile hook). Orphan parts must be swept out of the tree (renamed to the transient package), not left to garbage collection, or their GUIDs go stale between compiles: `SweepLeftoverParts` removes parts whose ready-made button left the tree, and parts a button that holds other content no longer holds (an empty button - an undone build - keeps everything for redo).
  - **Button preview toolbar (fix9, `NameTBDEditor/Private/ButtonPreviewToolbar.h/.cpp`):** in the Widget Blueprint editor's toolbar (`AssetEditor.WidgetBlueprintEditor.ToolBar`, own section, dynamic entry per editor - same pattern as the Widget Animation controls in `WidgetAnimBuilderToolbar.cpp`), collapsed unless one Context Menu Button is selected: `Name | Idle Hover Press On Disabled | < value > | Replay` (~400 px, ~520 with the stepper; On only when Button Can Be Enabled; the stepper only for a button with values). Acts on the selected **designer preview copy** only (nothing authored or saved). `FSelectedButtonPreview` remembers per button name what was picked and shows it again, at once, whenever the designer remakes its preview (`FWidgetBlueprintEditor::GetPreview()` changed); Replay plays the button's last change again from where it started.
    - States: `UContextMenuButton::PlayDesignerPreviewState(State, bAnimate)` (editor) runs the **runtime blend** on the preview: `ClearDesignPreviews`, `InitializeRuntimeShapeTargets/DesignTargets` (baselines = as authored), current state set to the one shown and applied instantly, then the new state and an active timer (`TickDesignerPreviewState`, serial-guarded) calling `ApplyTransitions(dt)` - speeds, curves, overshoot, delays, Looks, design channels exactly as in game. A pick mid-blend redirects it. Arrival (`FinishDesignerPreviewState`): design channels restored to their baselines, runtime targets reset, `ApplyDesignPreviews` paints the state as before. While a blend runs `ApplyDesignPreviews` does nothing; afterwards it paints `GetEffectiveDesignPreview()` = the toolbar's state when it drives (`bDesignerPreviewDriven`), else Show Preview/the placement's. `StopDesignerPreviewState` hands back.
    - Values: virtuals on `UContextMenuButton` (editor) `GetDesignerPreviewValueCount/Index/Text`, `SetDesignerPreviewValueIndex(Index, bAnimate)` - Choice Button (options or number steps; the value kicks in: `bAnimateInDesigner` lets `UpdateDisplay` kick at design time) and Segmented Choice (the highlight slides: `TickSlide` animates at design time while `bAnimateInDesigner`). Preview copy only.
    - Show Preview is hidden in the Details (`HideProperty`); it remains the state shown until the toolbar picks one, and the Instance Overrides' Show Preview still works for placements.
- `StyledButtons.h/.cpp`:
  - **Generic Button** (`UGenericButton`, was "Pill Button"): label; Corners, Corner Rounding, Content Padding, Fill/Outline/Text Color, Press Scale.
  - **Pill Button** (`UPillSwitchButton`, was "Toggle Switch"): on/off switch, no text - the background is the track, the knob (Ellipse in `KnobBox`) slides `TrackSize.X - 2*Inset - Knob` on Enabled; Track Size, Knob Inset, Track/Outline/Knob/Knob On Color; Left/Right set it.
  - **Checkbox**: no text - the background is the box; Custom Shape check drawn on by Trim End; Box Size, Corner Rounding, Outline Width/Color, Check Color/Width.
  - **Radio Button**: no text - the background is the ring; dot pops (Shape Scale); Size, Ring Width/Color, Dot Scale. Exclusive (`bUnenableAllOtherGroupButtonsOnEnabled`), **Click Only Enables off** (fix6: clicking the chosen one clears it - it used to be stuck on).
  - **Menu Item**: accent bar grows, label steps aside; Font Size, Text/Hover Text/Hover Fill Color, Bar Size, Hover Shift.
  - **Hold Button**: ring Trim End = press progress, fires once; Corners, Fill/Outline/Text Color, Ring Size/Width.
  - **Tile Button**: icon, name, rarity stripe, framed when chosen; exclusive, Click Only Enables off (fix6); Tile Size, Corner Rounding, Icon Size, Fill/Outline/Rarity Color, Show Label, Show Rarity, Hover Scale.
  - Removed in fix6: Tab Button, Icon Button. CoreRedirects (`Config/DefaultNameTBD.ini`): `PillButton -> GenericButton`, `ToggleSwitchButton -> PillSwitchButton`, `TabButton`/`IconButton -> GenericButton`.
- `ChoiceButtons.h/.cpp`:
  - **Choice Button** (label + "< value >"; Options or Number mode with Min/Max/Step/Decimals/Suffix; Wrap; click left of the value = back; Left/Right step; arrows dim at ends; value slides in; `OnChoiceChanged(Index, Value)`). Style: Corners, Corner Rounding, Content Padding, Selector Width, Font Size, Show Label, Fill/Outline/Label/Value/Arrow Color, Arrow Size. Parts by role (Label, Value, PreviousArrow, NextArrow, PreviousBox, NextBox, SelectorBox) - the old `ValueTextWidgetName`/arrow name properties are gone.
  - **Segmented Choice** (options side by side, a highlight slides under the chosen one; segment Text Blocks made at runtime/preview from Options, transient, HitTestInvisible; click picks by position over the Segments box; Left/Right step; slide speed from Motion). Style: Fully Rounded, Corner Rounding, Inset, Fill/Outline Color, Segment Padding, Font, Text/Selected Text Color (the last four apply even with Use Style off - the segments are always made from them). **Highlight (fix6):** a Rectangle in `HighlightBox` (Size Box, overlay slot HAlign Left / VAlign Fill, both HitTestInvisible): `PlaceHighlight` sets the box's Width Override to one segment (track width / count) and translates the box by `Position * segment width` - the highlight's widget is exactly where it is drawn (fix5 stretched it over the whole track and shifted it, so its box hung off the button and caught clicks). Its corners are concentric with the track: Fully Rounded inside a pill, else `CornerRounding - Inset`, so the end segments follow the track's edge. Parts are cached once per build (`CacheSlideParts`), never looked up per frame. Restyling segments only happens on a shown button (never adds transient texts to the Widget Blueprint's own tree).

### Niagara particles in UI (NiagaraUI v1, module `NameTBDNiagara`)
- **What:** `UNiagaraUIWidget` ("Niagara Particles", palette NameTBD, `NiagaraUIWidget.h`) plays a Niagara System in UMG and draws its **CPU** emitters' **Sprite and Ribbon** renderers as Slate custom-vertex meshes - sorted, clipped, tinted and transformed like any widget. No render target, no scene capture. GPU emitters, meshes, lights and lightweight (stateless) emitters are not drawn: one-time `LogNiagaraUI` warning at runtime, and notes/warnings in the Widget Blueprint compile results (`ValidateCompiledDefaults`).
- **Simulation:** per widget an ownerless, transient `UNiagaraComponent` outered to the widget's world (in the designer, the editor world), made on the **first paint** (nothing exists for a widget never drawn): `SetAutoActivate(false)`, `SetAutoDestroy(false)`, **`SetAllowScalability(false)`** (never culled by distance/visibility/budget - it sits near the world origin), **`SetRenderingEnabled(false)`** (no scene proxy, no render data; the widget reads the particles itself), hidden, no shadows/navigation. **Play While Paused** (default on) = `SetTickableWhenPaused` + `SetForceSolo`. Released (`DestroyComponent`) in `ReleaseSlateResources` (skipped during GC - `IsUnreachable`) and when its world is cleaned up (`FWorldDelegates::OnWorldCleanup`, so no world is kept alive).
- **Space:** the system's origin sits at **Origin** (alignment in the widget); **Particle Scale** = Slate units per Niagara unit; **Plane** (`ENiagaraUIPlane`: XZ front - default, XY top with Y down, YZ side) picks which plane is the screen; depth = Right x Down (toward the viewer). Local-space emitters map through the widget's whole accumulated render transform (DPI, layout, Render Transform, Widget Animations). World-space emitters map through the widget's scale only - the component is placed each paint at the widget's origin in "screen world" units (absolute / (Geometry.Scale x Particle Scale)), turned about the depth axis by the widget's angle and scaled by its extra render scale - so world-space particles stay behind as the widget moves (trails). **Perspective Distance** (0 off) scales particles by depth.
- **Drawing** (`Private/NiagaraUIMesh.*`, `FNiagaraUIMesh`): per paint `Gather` walks the simulations' emitters (`WaitForConcurrentTickAndFinalize` first - Niagara can run async to the end of the frame), hashes every current data buffer pointer + count + the widget's place/tint/cull rect, and **rebuilds only when that hash changed** (a paused effect or a second paint in a frame costs nothing). Draw entries sorted by renderer Sort Order Hint, then emitter, renderer, simulation - so the same renderer of the main system and its bursts share one batch; consecutive entries with the same brush merge (**one draw element per brush run**). Per-particle cull against the culling rect. All arrays persistent (grow only).
  - Sprites: Position, Color, Sprite Size (default 10), Sprite Rotation (degrees, clockwise), Alignment (Unaligned / Velocity / Custom - texture top along the facing), Pivot In UV Space, Sub UV frames (SubImageSize, SubImageIndex), Sort Mode (View Depth/Distance = by plane depth, Custom Ascending/Decending = CustomSorting), Dynamic Parameter xy -> TexCoord1.
  - Ribbons: grouped by Ribbon ID (FNiagaraID or int), ordered by Ribbon Link Order (else UniqueID newest first, else NormalizedAge), mitred strip (max 2x), UV0 -> TexCoord0 and UV1 -> TexCoord1 by their Distribution Mode (Scaled Uniformly / Segment Length / Tiled with Tiling Length), **Smooth Ribbons** = Catmull-Rom subdivision where segments are > 6 px on screen (max 8 steps).
  - Brushes (`Private/NiagaraUIRuntime.*`): a renderer's material is drawn when it is a **User Interface** material (shared `FSlateBrush` per material, kept by an `FGCObject`, let go 10 s after nothing holds it); else **Material Remap** (surface -> UI material) is looked up; else **Particle Image** (the widget's brush, its tint and UV region), else a built-in **soft dot** (64x64 transient texture). Vertex color = particle color x widget tint (sRGB FColor).
- **Cost control:** **Pause When Hidden** (default on): a module `FTSTicker` (`NiagaraUIRuntime`, one for all widgets, only those with a simulation) pauses a widget's simulations when it wasn't painted for 2 frames and the next paint resumes them. The Slate widget is **volatile only while a simulation is active and not user-paused** (so inside invalidation panels it costs nothing at rest). Hit-test invisible by default. `stat NiagaraUI` (Build Meshes, Paint, Particles Drawn, Draw Elements, Meshes Rebuilt), `NiagaraUI.Enable 0` (stop all), `NiagaraUI.Log 1` (made/released/paused/resumed).
- **Bursts:** `PlayAt(LocalPosition, BurstSystem)` / `PlayAtAbsolutePosition` play a one-shot at a point, up to **Max Bursts** (8) pooled simulations - an idle one of the same system first, then any idle one (asset swapped), then a new one, then the oldest (cleared first). Placed and started on the next paint so world-space particles are born at the point. Prewarm only for the main system.
- **Details order (fix3):** `PrioritizeCategories = "Particles Parameters Playback Layout Appearance"` - Parameters sits right under Particles, above UWidget's own categories.
- **User Parameters (fix2):** `FInstancedPropertyBag UserParameters` (EditAnywhere, `FixedLayout`, Category Parameters) is laid out **from the system's exposed User parameters** (`GetExposedParameters().GetUserParameters`), names without `User.`.
  - **Shared code (fix3):** `Public/NiagaraUIParameters.h` / `Private/NiagaraUIParameters.cpp`, namespace `NiagaraUIParameters` (exported): `GetShortName`, `LayOut(System, Values, SystemValues, PositionNames, bOutChanged)` (returns "rows changed"), `IsChanged`, `HasChanges`, `Apply`, `ApplyChanged`, `RestoreChanged`, `ApplyObject`. The widget and the Particles track both use it; the widget's `ApplyParameter` / `IsParameterChanged` are gone (`ApplyParameters` and `LayOutUserParameters` delegate). `FNiagaraUIParameterOverrides` (USTRUCT: Values, SystemValues, PositionNames) carries a track's values to a simulation.
  - **Types:** float, int, bool, enum, Linear Color, Vector 2D, Vector (Vec3 and **Position** - positions listed in hidden `PositionParameters` and set with `SetVariablePosition`), Vector 4, Quat, and objects (textures, materials, meshes...).
  - **Data interfaces (fix5):** told apart by class name (`EDataInterfaceKind`, no Niagara DI headers or exports needed for that), read and written by reflection:
    - **Curves:** Curve for Floats -> `FRuntimeFloatCurve`, Color Curve -> `FRuntimeCurveLinearColor`, Vector Curve and Vector 2D Curve -> `FRuntimeVectorCurve` (the 2D one uses X/Y). An External Curve in the struct is used when set. Applied by **copying the component's override DI, writing the curves, `UpdateTimeRanges` + `UpdateLUT`, `SetDataInterface` into the override store, then `ReinitializeSystem`** - only when a curve actually differs (`CompareScriptStruct`), so a playing effect restarts only on a real change. Never written in place: the running simulation may be reading it on another thread.
    - **Arrays:** Float, Int32, Bool, Vector 2D (`ArrayFloat2`), Vector (`ArrayFloat3`), Position, Vector 4 (`ArrayFloat4`), Color, Quat -> a bag **array** of the element type (`EPropertyBagContainerType::Array`). Defaults read from the DI's one `TArray` UPROPERTY by reflection (floats widened). Applied with `UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArray*` (full `User.` name), which is live and thread-safe.
    - **Not listed:** Texture Sample (samples on the GPU only; UI draws CPU emitters), and scene DIs (meshes, grids, render targets, collision, camera).
    - `Apply` recognizes a DI parameter by its bag desc (an array, or one of the three curve structs) and looks up the variable in the component's own system by name (`FindUserVariable`), so the same bag works for bursts of other systems.
  - **Starting values:** each starts at the system's own value, read from the store (`GetParameterValue<T>` / `GetUObject`).
  - **Override tracking:** hidden `UserParameterDefaults` keeps the system's values as last laid out. A parameter is **changed** when it differs from them (`NiagaraUIParameters::IsChanged`, `FProperty::Identical`). Only changed ones are handed to simulations (`ApplyParameters` -> `ApplyChanged` -> `Apply` by type; objects as material / texture / object). Unchanged ones follow the system, even when its value changes later.
  - **Re-layout** (`LayOutUserParameters`, no-op when the system's parameters and values are as laid out): changed values are kept when the parameter still exists with the same type. It runs on:
    - `PostLoad` (editor, or when migrating v1 maps),
    - `PreSave` (editor),
    - `PostEditChangeProperty` of Niagara System,
    - `SynchronizeProperties` at design time (the designer's preview, which is what the Details panel shows),
    - `SetNiagaraSystem`.
  - **Details refresh:** a changed row layout refreshes every Details panel next tick (`FPropertyEditorModule::NotifyCustomizationModuleChanged`; editor-only `PropertyEditor` dependency under `bBuildEditor`).
  - **v1 migration:** v1's Float/Color/Vector Parameters maps are hidden `UPROPERTY()`s now, folded into the bag once on load (`MigrateLegacyParameters`) and emptied.
- **Blueprint:**
  - Play(bRestart), Stop Emitting, Clear.
  - **Burst At Origin** (fix2: a one-shot where the widget is - its Origin worked out at paint, `FNiagaraUISimulation::bAtOrigin`), Play At, Play At Absolute Position.
  - Set Niagara System(bPlay) (re-lays out User Parameters).
  - Set Paused / Is Paused, Is Playing, Get Live Particle Count, Get Niagara Component.
  - Set Float / Int / Bool / Color / Vector 2D / Vector / Vector 4 / Object Parameter: kept in User Parameters when listed there, always handed to what is playing. **Reset Parameters**.
  - Set Color And Opacity, Set Particle Scale, Set Desired Size.
  - Event **On Finished** (main system only; not fired when Set Niagara System swaps it).
  - Auto Play starts once per simulation on first paint; Play before the first paint waits for it.
- **Using it (answer given to the user):**
  - Engine templates such as Directional Burst / Omnidirectional Burst (Engine > Plugins > Niagara Content > DefaultAssets > Templates, "Show Engine/Plugin Content") play as they are if their emitters are CPU-sim (check Sim Target; the Widget Blueprint compile says otherwise). But their sprite material is a surface material, so they draw as soft dots tinted and sized by the particles until a UI material is set or remapped.
  - The recommended workflow is: New Niagara System from the template, check CPUSim and Local Space, set Loop Behavior Once for bursts, and give the sprite renderer a UI material (Domain User Interface, Translucent or Additive; Texture Sample x Vertex Color, opacity = texture alpha x vertex alpha), or map the surface material to it in Material Remap. Then use the Niagara Particles widget (palette NameTBD) and set its Niagara System.
  - Size is 1 Niagara unit = 1 Slate unit x Particle Scale.
  - Not based on the free Niagara UI Renderer plugin's code: same idea (CPU particle data -> Slate meshes), written for this plugin; its public source was only read to check Niagara API names.
- **Particles cue** (`WidgetAnimParticleCue.h`, registered with `FWidgetAnimEvaluatorRegistry` by the module - listed under Cues), unified in fix2:
  - **Niagara System** on the track (was Burst System; CoreRedirect in `Config/DefaultNameTBD.ini`): what Burst plays, and what Play plays as the widget's system; None = the widget's own. The widget and the track no longer have to agree; one Particles widget serves every track.
  - **Use Widget's Niagara System** (fix3, `bUseWidgetSystem`, default off; shown for Burst / Play): each Particles widget plays its own system with its own parameters; Niagara System and User Parameters are **greyed out** (fix4; hidden in fix3) and hidden for Stop Emitting / Clear. **Act On** (`EWidgetAnimParticleCueScope` First Found / All Found; shown when Use Widget's Niagara System is on, and for Stop Emitting / Clear, which always act on the widget's own): All Found = every one named Particles Widget in the User Widgets around; unnamed, every one at or inside the track's widget, else every one in the nearest User Widget around it that has any (`FindParticlesWidgets(..., bAll, Out)`; `FindParticlesWidget` = first). Helpers `PlaysTrackSystem()`, `ActsOnAll()`.
  - **Track User Parameters** (fix3): `UserParameters` (EditAnywhere, FixedLayout, no Blueprint access) + hidden `UserParameterDefaults` / `PositionParameters`, laid out from the track's Niagara System by the evaluator's `RefreshParams` on each edit (also while hidden, so turning Use Widget's Niagara System off finds them as left). Only changed values go out, and only when the track plays its own system: `Fire` builds an `FNiagaraUIParameterOverrides` and calls `UNiagaraUIWidget::StartBurst(System, Point, bAtOrigin, Overrides)` / `PlayWithOverrides(System, Overrides)`.
  - **On the widget** (fix3): each `FNiagaraUISimulation` keeps `Overrides` (to put on at its next start) and `AppliedOverrides` (on its component now). `StartSimulation` -> `ApplyOverrides`: the applied track values back to the system's (`RestoreChanged`), the widget's own changed values again (`ApplyParameters`), then the new track's. `DropOverrides` before any `SetAsset` (Set Niagara System, a designer system swap, a pooled burst taking another system). Main keeps a track's values across a plain Blueprint `Play` until another system is set or a cue plays it without them. Blueprint `Set ... Parameter` still writes to every running simulation.
  - **Action:** Burst (enum value still `BurstAtTarget`) / Play / Stop Emitting / Clear.
  - **Burst At:** `EWidgetAnimParticleCuePlace` **At Particles Widget** (default - `BurstAtOrigin`, where the Particles widget is placed) or At Track's Widget (Burst Point on the track's widget, mapped through cached geometries; staggered = one burst per item).
  - **Particles widget:** the one named by **Particles Widget** (User Widgets around the track's widget, then the host's), else the track's widget itself (aim the Layer Group at the Particles widget - simplest), else the first inside it, else the nearest around it.
  - Fires at design time (previews). Summary "Burst NS_x on Name" / "Play NS_x". `WidgetAnimCues.h`'s note about particles updated (v1).

### Flow List architecture (short)
- **What it is**: `UFlowList` (palette: NameTBD > Flow List) is a panel that lays its children along a **path** by each child's signed **distance from the focus**, measured in items, and styles each child from that distance. It covers scroll lists, carousels, cover flows, picker wheels, Persona-style dials and slanted menus, tapering push/pop lists, rings and stacks. Files are in `Source/NameTBD/Widgets/FlowList/`.
- **Data** (`FlowListTypes.h`): four groups on the widget, all `BlueprintReadOnly` with setters, plus a `StylePreset` dropdown that fills them.
  - `FFlowListLayout`: path `Type` (Line, Arc, Wheel, Ring, Spline), Orientation, Angle, Focus Anchor/Offset, Item Pivot, Align To Path, per-path settings, Spacing, Item Size, Pitch Override, Desired Size.
  - `FFlowListEffects`: `FFlowListChannel`s for Scale, Squash, Opacity, Rotation, Cross Offset and Spread, plus Tint (`FFlowListColorChannel`), Spacing Follows Scale, Edge Fade, Z Order and Cull Distance. A channel is Off, Falloff (At Focus → At Edge over Distance items, with an easing and a Same/Opposite/Separate mirror for the items before the focus), or Curve (an `FRuntimeFloatCurve` over the signed distance).
  - `FFlowListScrolling`: Snap To Items, Loop, Stop At Content End, drag (sensitivity, Fling Coast, Max Items Per Fling, Elastic/Clamp overscroll), `ScrollSpring` (`FWidgetSpringParams`), wheel, navigation, Focus Follows Selection, **Stick Scrolls** (analog stick scrolling with speed, max speed, acceleration time, dead zone, response, invert; fix9), Select While Scrolling, Click To Focus.
  - `FFlowListTransitions`: Animate Changes, **Use Basic Animations** (on: Enter/Exit `FFlowListTransition` - duration, delay, easing, opacity, scale, offset along/across the path, rotation; off: **Anim Enter Item / Anim Exit Item**, `TArray<FWidgetAnimEntryBase>` played on each item), `ReorderSpring`, **Intro** (`FFlowListIntro`: Play Intro, Stagger), Keep Focused Item. Legacy `bPlayIntro` / `Stagger` are hidden UPROPERTYs migrated into Intro by `PostSerialize`.
  - `FlowListTypes::ApplyPreset` holds the presets.
- **Slate** (`SFlowList.h/.cpp`, an `SPanel`):
  - Scroll position is in items. A slot's `Place` springs to its `ItemIndex`, which is how items slide aside on insert, remove and move.
  - `ComputeLayout` sweeps outward from the focus, integrating each pitch scaled by Scale × Squash (Simpson's rule per item) when Spacing Follows Scale is on. That's what makes a taper pack into a curve.
  - Uniform scale goes in the **layout** transform (text stays crisp). Turn and squash go in a **render** transform about the pivot, composed after the child's own render transform, so Widget Animations on items still play.
  - Opacity and tint go through the child's `FWidgetStyle` in `OnPaint`. Each item paints at `MaxLayer + 1`.
  - Motion states: Idle, Drag, Coast, Spring, Stick (fix9: the stick's speed, from `UpdateStick`; read by the `FFlowListStickInput` input preprocessor).
    - Drag uses a UIScrollView rubber band past the ends.
    - Release: with snapping, it springs to round(position + velocity × Fling Coast), starting at the release velocity. Without snapping, it coasts with exponential decay.
    - Wheel and keys accumulate on the spring's target. Springs use `UWidgetAnimSpringLibrary::AdvanceSpring`.
  - Ticking is an active timer that stops when idle.
  - Pointer handling:
    - The press is seen in `OnPreviewMouseButtonDown`.
    - If a child (a button) holds the mouse, the list polls the cursor. Once past the drag threshold, along the list's axis, it calls `ReleaseAllPointerCapture` to take the drag, so grabbing a button never clicks it.
    - A drag mostly across the list is left alone (nested scrolling).
    - A press on a moving list catches it.
  - Removed children stay in the panel as leaving ghosts: hit-test-invisible, playing Exit, not items. `RestoreAnchor` keeps the focused item in place across structure changes (`ShiftFrame`). Removing the focused item lets the next one slide in (push/pop).
  - Navigation along the list's orientation moves one item, returns `Explicit` focus to the item's first focusable widget, and returns `Escape` at the ends. `OnFocusChanging` scrolls to an item that gains non-mouse focus.
  - The designer draws the path and focus point (`Show Path In Designer`).
  - Item Widget Animations (Use Basic Animations off): Slate keeps Enter/Exit timing (`SetItemAnimationLengths`, from the entries' stack durations) but draws nothing of them; `OnItemTransitionStarted(Widget, bEnter)` fires once each entrance/exit passes its stagger, and `UFlowList` plays the entries on the item through an `FWidgetAnimPlayer` + baselines per item (`ItemAnimations`, ticked on an `FTSTicker`). Baselines are restored when the item leaves for good or a virtual entry goes back to the pool. An entering item isn't drawn until its animation starts.
  - The item's own pose from its entrance/exit animation (opacity, scale, move, turn, shear of the item itself) is taken out of the player's result (`UFlowList::PoseItemAnimation`: `EvaluateWidgetAnimPlayer`, then `ApplyComposedResult` for the rest) and drawn by the Slate list (`SFlowList::SetItemAnimationPose`, about the item's Render Transform Pivot, over its own render transform, under the list's turn and squash). The item's own Render Transform / Opacity are never written, and its entry is dropped from the baselines.
  - `FlowList.LogItemAnimations` (console): 1 logs previews, the computed entrance/exit lengths, each Add/Remove (asked to animate, Animate Changes, whether the entrance/exit started), each entrance/exit start and how many entries started; 2 also each frame's pose. Always-on one-time warnings: item not found; no usable Anim Enter/Exit Item entry; an item removed with nothing to play as it leaves.
  - Cues (`EFlowListItemCue`): Transition (Add/Remove/shown/intro/previews), View (**Play As Items Scroll In And Out**: `SFlowList::UpdateViewCues` each animated frame compares each slot's in-view flag - its box at the list's scale overlaps the list, worked out in `ComputeLayout` before any fade, kept in `CachedInView` - with the last; what is in view the first time is silent) and Selection (**Play On Selection**: queued in `SetSelected`). View and selection cues are queued (`PendingCues`) and told in `FlushCues` after the frame's step. For those two the pose is held in the designer too (`bHoldPose`). A direction with nothing to play (no usable Anim Exit Item, say) leaves the item exactly as it is - no exit is made up from the entrance.
  - Selecting an item of a Flow List in the designer puts Enter / Exit above its top right corner, previewing that one item (`UFlowList::PreviewItem`, `SFlowList::PlayItemPreview`).
  - Items are matched to the Slate widgets the list holds by `UFlowListSlot::SlateContent` (what `BuildSlot` handed the Slate list) or a virtual entry's `Slate`, through `UFlowList::GetItemSlateWidget` - never by `GetCachedWidget` alone, which in the designer can be the inner widget of a design-time wrapper. A start that finds no item logs one warning (`LogFlowList`).
  - Designer preview: item entrances/exits also play at design time; a finished entrance restores the item's baselines there. Editing anything under Transitions on the template plays the entrances in the designer's preview next frame (`PreviewInDesigner`, previews found by `FindDesignerPreviews`: same name, preview owner's class generated by the template's Blueprint).
  - Spline curve sampling is shared: `FlowListTypes::BuildSplineCurve` (Catmull-Rom, `SplineStepsPerSpan` = 16), used by `SFlowList::BuildSpline` and the designer's path editor.
- **UMG** (`FlowList.h/.cpp`):
  - `UFlowList : UPanelWidget` with `UFlowListSlot` (Padding, Size Override).
  - `IFlowListItem` (BlueprintNativeEvent `OnFlowListSelected`, and `OnFlowListDistanceChanged` when Report Item Distance is on).
  - Events: OnSelectionChanged, OnSettled, OnItemClicked, OnScrolled, OnItemExited, OnEdgeReached.
  - Sounds: Select tick (rate-limited in real time), Settle, Edge, through `FUISoundPlayer`.
  - Item API: AddItem / PushFront / InsertItem / RemoveItem(At) / PopFront / PopBack / MoveItem / ClearItems. Scrolling API: ScrollToIndex / ScrollToItem / ScrollBy / Set/GetScrollPosition / Get/SetScrollFraction / GetViewFraction (for a custom scroll bar).
  - Leaving UMG widgets are held in `LeavingItems` (a strong UPROPERTY) until their exit ends, because Image and Border hand Slate raw brush pointers.
- **Performance** (v2):
  - `SFlowList::GetLayout` works the layout out once per frame for a geometry, keyed on frame, `LayoutVersion`, scroll, size and render transform. Arrange, paint and every hit test share it.
  - **Virtual list:** `UFlowList::EntryWidgetClass` plus `SetListItems`.
    - The Slate side holds `VirtualCount` items by index. `LiveOrder` has one entry per item, `INDEX_NONE` where no entry is shown.
    - `UpdateRealized` realizes the items within the view plus 2 (given back past +1.5) through `OnRealizeEntry` / `OnReleaseEntry`.
    - The UMG side pools entries (`FFlowListEntryWidget`, kept with their Slate widget so reuse doesn't rebuild) and tells them their item through `IFlowListEntry`.
    - Items are even: Item Size, or the first entry's size.
    - `NotifyVirtualInserted/Removed` slide and keep focus like the child list. Entries leave through Exit before going back to the pool.
- **Hover Magnify** (`FFlowListEffects::Magnify`):
  - The pointer is projected onto the path's direction at the focus (`MagnifyCenter`, in items), and `MagnifyAt(d)` multiplies Scale and, through `SpacingFactor`, the spacing.
  - `MagnifyAmount` eases with `FadeTime`. The active timer keeps running while the pointer is over the list.
- **Finding lists:**
  - `UFlowList::FindFlowListOfItem` walks up through panels, out of Widget Blueprints, and through the virtual entry registry (`FlowListRegistry::EntryLists`).
  - `FindNearestFlowList` searches the item's list, then itself, then inside, then widens outward, or finds a list by name.
  - `OnListUpdated` (native multicast) fires every animated frame and on item or selection changes.
- **State machine plumbing:**
  - `FWidgetAnimInput_FlowList` (`FlowListInput.h`) is a Value Input, offered in every Driven By and value track. Measures: Selected Index / Value, Scroll Position / Progress / Speed, Is Scrolling, Item Count, and, on an item, Item Index / Distance / Focus / Is Selected.
  - `UFlowListSlot::ItemValue` gives an item a stable number for Selected Value.
  - The built-in Selected input also reads `WidgetAnimMachineWidgetStates::IsSelectedByContainer`. The Flow List registers `UFlowList::IsInSelectedItem` with it in `FNameTBDModule::StartupModule`, so a widget inside the selected item is Selected in every machine.
  - Best practice adopted: the list publishes, and the reacting widget owns the machine. A character portrait's Widget Animation Component has an Integer input Driven By Flow List (Selected Index or Value) and a state per character. The list knows nothing of it.
- **Item looks:** `UFlowListItemStyle` (`FlowListItemStyle.h`) is a UI Component on an item or anything inside it. It's in the designer's Add Component.
  - Edits of the template in Details are pushed to the designer's preview copies at once (`PushEditsToPreviewCopies`), since component edits don't rebuild the preview.
  - Details (`FFlowListItemStyleDetails`) show only what applies: empty look bags, Material Parameters on a widget with no material slot (while empty), and the Transition under Nearness To Focus are hidden. Trigger, a spring's Mode, Is Color and Material Parameters array changes force a Details refresh next tick (rows a component edit hides/shows weren't being re-laid out in the designer).
  - Trigger: Selected, Nearness To Focus, Hovered, or Selected Or Hovered. A spring `Transition` moves between the looks (Nearness follows the list directly).
  - Looks are property bags, reusing the Design track's machinery: Selected/Unselected design settings (brush, outline, colors, font, padding; `WidgetDesignAnimation`) and shape settings (`WidgetShapeAnimation`, and Shape Box through `ApplyShapeToBox`).
  - Also: material parameters, render opacity and transform, and Anim IDs played each way.
  - Only settings that differ between the two looks are written.
  - `FFlowListItemStyleDetails` (editor) lays the bags out for the owner widget as Details are shown. It finds the owner through a live preview copy or the authored tree, as the Widget Animation Component does.
- **Scroll bar:** `UFlowListScrollBar` / `SFlowListScrollBar` (`FlowListScrollBar.h`), v3 rewrite.
  - Style Preset (Custom, Minimal, Classic, Segmented, Arc, Dial, Page Dots) fills the settings, as the list's presets do.
  - Path: Straight, Curve (a circular arc set in Details by Curve Bow, Curve Length and Curve Rotation, with the bar's Orientation) or Spline (Spline Points in fractions of the bar, drawn on the canvas with the Flow List's spline editor); End Padding for Straight/Curve. Everything - track, thumb, fill, ticks, dots - lies along it (`FBarPath`: flattened points + distances, `At` / `Project` / `Slice`). `RefreshBar()` redraws after a direct edit.
  - Track / Thumb / Fill are `FFlowListScrollBarLine`s: `FWidgetShapePaint` (color, gradient, image), thickness and hover thickness, hover and pressed tints, cap, dashes, outline. Drawn with the shape geometry (`WidgetShapeGeometry::BuildStroke`, `CutForGradient`) and custom verts.
  - Thumb Kind: Bar or Knob (any brush, size and hover size, tints, Align Knob To Path). Smooth Thumb follows the list on `ThumbSpring`.
  - Ticks: one per item (Max Ticks, every N beyond), lit and lengthened at the current item, Tick Offset across.
  - Dots lie along the path too (Align Dots To Path), hit-tested as circles.
  - It follows its list by name or the nearest one, through `OnListUpdated`. Dragging the thumb (projected onto the path) calls `SetScrollFraction`, and letting go calls `SnapToNearestItem`.
- **Spline path editor** (editor, `FlowListPathEditor.h/.cpp`): a UMG designer extension like the Custom Shape's (`WidgetShapePathEditor`), for a Flow List (Layout Type Spline) and a Flow List Scroll Bar (Path Spline) alike - `SplineTarget` adapts the two (a scroll bar has no focus marker). A selected Flow List's toolbar also always has **Enter** / **Exit**, which preview the items' entrances and exits (`UFlowList::PreviewEntrances` / `PreviewExits`, `SFlowList::PlayExitPreview`). Selecting one with a Spline path shows Edit Path above it; while editing: drag points (snapping, Shift 45 degrees, Ctrl no snap), box-select, drag the focus marker along the curve (Spline Focus; Shift snaps to quarters), double-click the curve to insert, Pen adds at the ends, Delete (two stay), Reverse, arrows nudge, Enter/Escape finish. Writes the template and the preview (`SetListLayout`), one transaction per gesture; state kept by list name.

## State machine (as it is now, after StateMachine v8)

The state machine additions agreed earlier (items 1-6) are done in `NameTBD_StateMachine_v1.patch`, except item 5 Option B (true sub-machines), which wasn't wanted until Option A proves not enough. The "Still open" items of v2-v7 are done in `NameTBD_StateMachine_v8.patch` (see "StateMachine v8 additions"); only Option B is left.

### Where the state machine lives
- **Asset:** `Source/NameTBD/WidgetAnimation/WidgetAnimStateMachine.h/.cpp`.
  - `UWidgetAnimStateMachineAsset` (WASM_): `Inputs` (`FWidgetAnimMachineInput`), `Layers` (`FWidgetAnimMachineLayer`).
  - **Layer:** Name, `BlendMode` (`EWidgetAnimMachineLayerBlend` Override / Additive), Weight, WeightInput, **`MaskMode` / `MaskTargets`** (Affects / Targets, new), `States` (`FWidgetAnimMachineState`, node kinds State / Entry / Exit / Any State), `Transitions` (`FWidgetAnimMachineTransition`, in priority order per source node, reordered in the graph by `MoveTransitionPriority`), editor Comments, **editor `EditorGroups`** (new).
  - **State:** **`BlendSpace`** (shown as Plays: Single Animation / Blend 1D / Blend 2D), Animation, **`RandomAnimations`** (Random Variations), **`BlendInput`, `BlendSamples`**, Playback (Once / Loop / Ping Pong / Hold End Pose), PlaybackSpeed, **`SpeedInput`**, ProgressInput, **`ParameterInputs`** (Parameters From Inputs), **`OnEnter` / `OnExit`** (actions), comment, node color, **editor `EditorGroup`**.
  - **Transition:** bEnabled, `Conditions` (`FWidgetAnimMachineCondition`, all must hold; a second arrow is an OR), Blend (Tween / Spring / Play Transition Animation / Cut), Duration / Easing / Spring, ExitTime (Immediately / AtProgress / WhenFinished), Interruptible, **`bRandom` / `RandomWeight`**, **`OnTaken`** (actions).
  - **Condition:** **`Source`** (shown as Test: Input / Another Layer's State). Input: tests one `Input` (machine input or built-in Hovered, Pressed, Focused, Selected, Disabled, Closing, Clicked) with `EWidgetAnimMachineCompare`, optionally against another input (`EWidgetAnimMachineCompareTo`). Layer state: **`Layer`, `LayerTest` (Is In / Is Not In / Entered / Exited), `State`**.
  - **Inputs:** Bool, Float, Trigger, Press And Hold, Integer, Choice, Vector 2D; Driven By (`FWidgetAnimInputSource`), Measured On.
  - **Events:** `FWidgetAnimStateEventInfo` (Machine, Layer, State, OtherState, bEntered, **EventName** (new: a Fire Event action's), Host), broadcast by `UWidgetAnimationComponent` (BlueprintAssignable). A Fire Event action also calls the Custom Event `On <Machine> <Event Name>` on the User Widget, if it has one (`WidgetAnimMachineEvents::GetActionEventFunctionName`, next to `GetStateEventFunctionName`).
  - Option functions (GetOptions, on the asset and on the editor's proxies): `GetInputNameOptions`, `GetNumberInputOptions`, `GetStateParameterOptions`, `GetConditionLayerOptions`, `GetConditionStateOptions`, `GetMaskTargetOptions` ("(Widget)" = `MaskHostTarget`, then the targets), `GetBlendInputOptions`, `GetSettableInputOptions`.
  - `RenameLayerStateReferences(LayerName, Old, New)`: called by `WidgetAnimMachineEdits::RenameState` / `RenameLayer`, so layer-state conditions follow renames.
- **Runtime:** `WidgetAnimStateMachineRuntime.h/.cpp` - `FWidgetAnimMachineInstance` / `FWidgetAnimMachineLayerInstance`, `FWidgetAnimMachinePose`, spring channels, `FWidgetAnimMachineInputState`, `FWidgetAnimMachineContext` (has `OnStateEvent`), `Tick(Instance, Context, DeltaTime)` / `TickLayer`, `FWidgetAnimMachineLayerDebug`, `ComputeBlendWeights`, `GetBlendWeights`.
- **Blueprint:** `WidgetAnimStateMachineLibrary.h` - Set/Get per input type, Fire Trigger, Is Widget In State, Get Current State Of Layer. On the component: **Get State Machine Blend Weights** (new).
- **Played by:** `UWidgetAnimationComponent` (State Machines entries `FWidgetAnimStateMachineEntry`: Target Widgets, input overrides, Start On Construct).
- **Editor:** `SWidgetAnimStateMachineEditor.*` (layers as graph tabs, inputs panel, preview Plays On / group rows, PIE debugging), `WidgetAnimStateMachineGraph.cpp` (graph nodes, transition priority, groups, blend plot), `WidgetAnimMachineConditionCustomization` etc.

### Press And Hold: Is Let Go is a pulse
- `FWidgetAnimMachineInputState::bLetGo`: set in `UpdateHolds` the frame a press that had started (held, done, or any elapsed time) is let go; cleared at the end of `Tick` with `bFired`. A Hold condition **Is False** (shown "Is Let Go") reads `bLetGo` - once, as a Trigger fires - instead of "not held", which held true forever.
- Jump To State (`WidgetAnimMachineJump`): `FInputPlan::Tap` - Hold inputs to press and let go a moment later. The editor presses them, then `TickPendingLetGo` lets go 0.06 s later (`PendingLetGo`, `PendingLetGoTime`).

### Inputs panel (design like My Blueprint's variables)
- Each row: name (fills) | type pill column (`WidgetAnimTypePill::TypeColumnWidth` 92) | value column (`ValueColumnWidth` 124), right-aligned; built-in rows use the same columns. No X button: right-click a row for Rename / Delete (`OpenInputMenu`, `RenameInputInPanel`, `RemoveInputFromPanel`).
- `NameTBDEditor/Private/WidgetAnimTypePill.h/.cpp`: `EKind` {Bool, Float, Integer, Choice, Vector2D, Color, Trigger, Hold}; `GetColor` reads the engine's pin colors through `UEdGraphSchema_K2::GetPinTypeColor` (Bool, Float = Real/Double, Integer, Choice = Byte/enum, Vector2D and Color = their structs); Trigger and Hold keep the plugin's own (`NameTBD::Colors::StateMachineEditor::InputTrigger` / `InputHold`). `MakePill` = the `Kismet.VariableList.TypeIcon` pill tinted, plus the type name. Animation parameters use the same pill. The `Input*` constants in `NameTBDColors.h` were set to the engine's defaults, for anything else still reading them.

### Entry and Exit can be jumped to
- Double-click (or the node menu's **Jump To Entry / Jump To Exit**). Exit: planned like any state (the BFS accepts Exit as a target). Entry: `RestartFromEntry` - every preview input back to its default, built-ins false, `PreviewSignature.Reset()` so the designer preview starts again; in PIE only a notification (the game owns the inputs).

### 1. Conditions on another layer's state - done
- Condition **Test: Another Layer's State**, with Layer (any but the transition's own - the customization's own combo, from `FindOwnLayerIndex`), Layer Test (Is In / Is Not In / Entered / Exited), State (the picked layer's states and Exit). The input rows hide. Reads "Press is Down", "Badge entered Shown", "X isn't Y", "X left Y".
- **Runtime:** `Tick` snapshots every layer's `Current` into `LayerStatesNow` before any layer moves, keeping the previous frame's in `LayerStatesBefore`. `EvaluateLayerStateCondition` reads those, so layer order doesn't matter. A layer mid-transition counts as in its target (its `Current` is already the target). Entered / Exited hold for the one frame the snapshot changed. A layer that moved sets `bInputsChanged`, so settled layers look again. A missing layer or state is false.
- Jump To State assumes layer-state conditions hold (it can't plan another layer), never makes or fails them.
- (v8) A condition naming a missing layer or state is a Widget Blueprint compile warning (it is simply false); while a widget is watched, transition nodes mark them held / not (see "StateMachine v8 additions").

### 2. Per-layer widget masks - done
- Layer **Affects**: All (default) / Only These / All But These, and **Targets** (multi: "(Widget)" = the host, then the machine's targets).
- **Runtime:** `BuildLayerMask` resolves the targets through the instance's `TargetPaths` (and the Shape Box each is or sits in, for Shape targets); `ApplyLayer` drops transforms, material parameters, shape targets and authoritative writes on widgets left out, before the Override / Additive blend - so the layers under it keep them.
- **Editor:** the layer tab shows a small badge "only N" / "all but N".
- (v8) Per-channel filter: the layer's **Channels** (see "StateMachine v8 additions").

### 3. Blend states (1D / 2D) - done
- State **Plays: Blend 1D / Blend 2D**, **Blend Input** (1D: Float, Integer, Press And Hold, or a Vector 2D's length; 2D: a Vector 2D), **Blend Samples** {Animation, Position, Position 2D}.
- **Runtime:** one `FWidgetAnimPlayer` per sample (`FWidgetAnimMachineLayerInstance::BlendPlayers`), all started with the state (same playback and speed); each evaluated every frame and mixed by `BlendWeights` (a running weighted `BlendPoses`). Weights recomputed when inputs change. `ComputeBlendWeights`: 1D linear between neighbours (held at the ends); 2D on a sample = that one, inside a triangle = barycentric over the smallest triangle around the point (all triangles of the first 16 samples tried), outside = the nearest three by inverse distance squared. Played = every sample finished or the longest sample's pass. Pose playback settles every sample.
- **Editor:** a small plot under the node's subtitle (`SWidgetAnimMachineBlendPlot`): a line with ticks (1D) or a box with dots (2D), dots growing with their weight and the live input dot, while watched. Subtitle "Blend 2D by Stick, 4 samples". Tooltip row "Blends".
- **Blueprint:** Get State Machine Blend Weights (component), in Blend Samples order; empty when not in a blend state.
- Not done: cues (Sound, Event, Particles...) on samples are fired by each sample's own player (all of them, unweighted) - worth checking; a dedicated sample editor (it's an array in Details).

### 4. Actions fired from inside the machine - done
- `FWidgetAnimMachineAction` {Kind Set Input / Fire Trigger / Fire Event, Input, Bool Value, Number Value, Option, Vector Value, Event Name}: state **On Enter / On Exit**, transition **On Taken**.
- **Runtime:** `StartTransition` queues the left state's On Exit, the transition's On Taken, the entered state's On Enter (`PendingActions`); `RunPendingActions` runs them at the end of `Tick`, after the frame's pulses are cleared - so what they set or fire is seen next frame and a machine can't loop within one. `Start` runs the first states' On Enter. Fire Event goes through `OnStateEvent` with `EventName` set (not at design time). The layer keeps its last 4 actions in words (`LastActions`) for the debugger.
- **Editor:** a lightning badge on nodes with actions (`FNameTBDStyle::StateMachineActionsIcon`, `Resources/WidgetAnim_StateMachineActions.svg`); tooltip rows "Actions" and, while watched, "Last Actions".
- (v8) The component Details list each Fire Event action's event with a "+", as state events (see "StateMachine v8 additions").

### 5. Collapsed groups (Option A) - done; Option B not started
- Editor only: `FWidgetAnimMachineStateGroup` {Id, Name, Position, bCollapsed} in `Layer.EditorGroups`, `State.EditorGroup`. Runtime unchanged.
- State menu **Collapse To Group** (the selected states and this one; on a state of an expanded group, folds the group back). A group box's menu: **Open Group** (or double-click), **Expand In Place**, **Ungroup**, **Rename**. While a group is open, a "< Layer / Group" button over the graph goes back. States added while a group is open join it.
- `UWidgetAnimMachineGraph::OpenGroup`, `OpenGroupView`, `GetShownNodeId` (a state's shown node: itself, its collapsed group's box, or none). Transitions are drawn between shown nodes; within one box they aren't. The box lights when the watched state is one of its states (`ShowsState`). Wires can't be joined to a box (open or expand it).
- `Normalize` drops a state's group that no longer exists, and groups left with no state.
- Not done: an open group shows only its states - transitions to states outside it aren't drawn there (see them at the layer level).

### 6. Smaller items - done
- **Random transitions:** `bRandom` + `RandomWeight`. When a Random transition could be taken, one of the Random transitions from the same node (Any State's included) whose conditions and exit time hold is picked by weight (`PickRandom` in `FindTransitionToTake`).
- **Random Variations:** a state's `RandomAnimations`; each start plays Animation or one of them, at random.
- **Speed Input:** a Float (any number input) multiplying the state's speed, held at 0 or more. Sets the state track's `Speed` (every sample's for a blend state) and scales `TimeInState`; `PassSeconds` is now the unscaled pass length.

### Cross-cutting
- New fields default to the old behavior; new enum types are new (no values appended to old ones). No redirects.
- `FWidgetAnimMachineLayerDebug` gained `BlendWeights`, `BlendPoint`, `LastActions`.
- Tooltips in `NameTBDTooltips.h` (`StateMachineEditor`: `JumpToEntryTooltip`, `JumpToExitTooltip`, `LayerMaskBadgeTooltip`, `ActionsBadgeTooltip`, `CollapseToGroupTooltip`, `GroupNodeHint`) and on the new UPROPERTYs / UENUMs.

### StateMachine v2 additions
- **Inputs panel:** "+" opens a Blueprint-style type picker (search, then each type's pill and name - `WidgetAnimTypePill::MakeTypePickerButton`). Inputs have a **Category** (`FWidgetAnimMachineInput::Category`): uncategorized first, then each category under a heading - fold with its arrow, double-click its name to rename it (`WidgetAnimMachineEdits::RenameInputCategory`); right-click an input: Move To Category ((none), each category, New Category - `SetInputCategory`). In each group, the inputs the machine reads come first, the unread after them, dimmed. "Widget" is now **Widget Inputs**, the read ones first too.
- **Press And Hold button** (`SWidgetAnimHoldInputButton`): its own widget, not an SButton - a dark button washed with the hold color and an empty track along its bottom while untouched (unlike Fire), and a fill clipped exactly to its rounded shape as it is held.
- **Input renames** now follow into Speed Input, Blend Input, Parameters From Inputs and actions too (`ReplaceInputReferences`).
- **Random Variations** are `FWidgetAnimMachineRandomVariation` {Animation, Chance 0 - 1}: kept to 1 at most (scaled down together in `Normalize`); what is left over plays the state's own Animation (`FWidgetAnimMachineState::PickRandomAnimation`). v1's `RandomAnimations` (no chances) load into it, an equal share each with the state's own. The node subtitle reads "Idle 40% + 2 random, Loop".
- **Graph:** a state node that changes size keeps its middle in place, so wires lined up with Q stay straight (`SWidgetAnimMachineStateNode::Tick`, `UWidgetAnimMachineGraph::KnownNodeSizes` / `KnownNodeZoom`, `WidgetAnimMachineEdits::SetNodePositionQuietly` - no undo step of its own). Zoom changes don't move anything.
- **Collapsed groups:** the box's inside is the collapsed graph node's / collapsed track's body (`SWidgetAnimMachineCollapsedBody`, colors `NameTBD::Colors::WidgetAnimEditor::CollapsedNode*`). An open group darkens the graph's edges inside a thin frame (`SWidgetAnimMachineScopeVignette`, `ScopeVignette` / `ScopeFrame`) and shows a breadcrumb trail over the graph - "< Layer > [group icon] Group" - in place of the back button.
- **Wire menu:** "Transitions On This Wire" comes after the State section; each transition is its route with a check box (ticked = can be taken) instead of Enable / Disable words.
- **Node comments:** no hint text in the menu's box; the bubble is a Blueprint node's exactly - the editor's comment color (`UGraphEditorSettings::DefaultCommentNodeTitleColor`), its pin and its show / hide toggle (hiding keeps the note).
- **Layer tabs:** right-click Rename / Delete.
- **Data validation** (`UWidgetAnimStateMachineAsset::IsDataValid`): conditions on a layer or state that is gone; Parameters From Inputs naming a parameter none of the state's animations has, or an input the machine doesn't have.
- **Editor layout and modes** - see "Reconciled with the Widget Animations panel" in the latest change set.

### StateMachine v3 / v4 additions
- **Straight Wire** (`FWidgetAnimMachineTransition::bEditorStraight`, editor only): a plain wire drawn as an exact straight line from state to state, at any angle. Set by Q on states that lie on a slant (they are no longer moved onto 45 degrees), or from the transition node's menu.
- Q works on collapsed groups (ends mapped through `GetShownNodeId`), and is one undo step.
- Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z in the editor (`OnPreviewKeyDown`).
- Play / pause floats at the graph's bottom left (`MakePlayPauseButton`); the bar: mode and asset at the left, debug picker, Dock, lock and Save at the right.
- Lock (`bMachineLocked`): the machine read-only while previewing; not saved.
- (v5) "Collapsed groups" are called **Collapsed States** in the UI. Node sizes are kept with positions (`EditorSize` / `EditorSizeZoom`) so undo / redo keep Q's straight lines. The Widget Blueprint's own machine has a name (`UWidgetAnimTimelinesExtension::StateMachineName`), renamed on the bar or in the Details; timelines are renamed by double-clicking their name.
- (v6) Names renamed by double-click use `SWidgetAnimNameField` (`WidgetAnimNameField.h`), not `SInlineEditableTextBlock`. The collapsed states' trail is in the layer row. Collapse States straightens the new box. Several states selected: a selection-only node menu.

### StateMachine v8 additions
- **Widget Blueprint compile warnings** (`NameTBDEditor/Private/WidgetAnimCompileChecks.h/.cpp`): `UWidgetAnimCompilerExtension : UBlueprintCompilerExtension`, registered for `UWidgetBlueprint` with `FBlueprintCompilationManager::RegisterCompilerExtension` (rooted, never unregistered) by `WidgetAnimCompileChecks::Register` in the editor module's startup. `ProcessBlueprintCompiled` writes `CompilationContext.MessageLog.Warning` lines (`@@` defused). `WidgetAnimCompileChecks::Gather` walks every object inside the Widget Blueprint (`ForEachObjectWithOuter`, nested, not transient): Widget Animation Components (their State Machines' machines and those machines' animations, their Anim In / Anim Out animations), the Widget Blueprint's own machine (named "State Machine '<name>'" / "This Widget Blueprint's State Machine") and timelines ("Timeline '<name>'"). Each machine / animation once per compile.
  - The lines are the data validation's, now shared: `UWidgetAnimStateMachineAsset::GatherProblems(OutProblems, MachineName)` (missing condition layer / state, **new: a condition on an input the machine doesn't have**, Parameters From Inputs naming a parameter no animation of the state has, or a missing input) and `UWidgetAnimPresetAsset::GatherParameterProblems(OutProblems, AnimationName)` (bindings / Plays When on missing parameters). `IsDataValid` on both calls them.
- **Debug view, conditions on other layers:** `FWidgetAnimMachineLayerDebug::LayerConditions` (`FLayerCondition` {Transition, Condition, bHolds}, `FindLayerCondition`) filled by `GetDebug` with `EvaluateLayerStateCondition`. The graph: `UWidgetAnimMachineGraph::GetLayerConditionHeld` / `GetLayerConditionsHeld` / `ReadLayerConditionHeld` (a condition that held within `LayerConditionLinger` 0.5 s still counts - so a one-frame Entered / Exited is seen; `LayerConditionHeldTimes`). A watched transition node with such conditions has a dot at its top right (`LayerConditionHeld` green / `LayerConditionNotHeld` red, `LayerConditionDot` 7 graph units - `NameTBDColors.h`); its tooltip lists each as "Holds" / "Doesn't hold" with the condition in the same colors.
- **Per-channel layer masks:** `FWidgetAnimMachineLayer::MaskChannels` (int32 bitmask, `EWidgetAnimMachineLayerChannel` Bitflags: Opacity, Scale, Translation "Position", Shear, Angle "Rotation" - the same bits as `WidgetAnimMachineChannels` - Material "Material Parameters", Outline "Shape Box Outline", Other "Texts, Designs And Others"), shown as **Channels** in the layer's Details; default all (`AllMaskChannels` 0xFF), `AffectsChannel`, `HasChannelMask`. `ApplyLayer` takes the mask: transform channels filtered per channel (Additive too), material parameters, shape targets and authoritative writes dropped when left out - on the targets Affects lets through. The layer tab's badge reads e.g. "only 2, Scale" / "3 channels" (`MakeLayerMaskBadgeText`).
  - **Fixed on the way:** the layer Details proxy (`UWidgetAnimMachineLayerProxy::PostEditChangeProperty`) never wrote Affects / Targets back to the machine (only Name, Blend Mode, Weight, Weight Input); it now writes `MaskMode`, `MaskTargets` and `MaskChannels` too.
- **Component Details "+ add event" for Fire Event actions:** `WidgetAnimComponentEvents::GatherActionEvents` (`FActionEventRow`) lists every Fire Event action of the machines the component plays (states' On Enter / On Exit, transitions' On Taken), once per Custom Event name (`GetActionEventFunctionName`), after the state events; the "+" makes the Custom Event with an Info pin of `FWidgetAnimStateEventInfo` (what it is called with). Tooltip `ActionEventRowTooltip`.

### Still open (state machine)
- Option B: true sub-machines.

## Animation Parameters (v1 + v2 + v8 done: `NameTBD_StateMachine_v1.patch`, `_v2.patch`, `_v8.patch` + `_v8_fix1`, `_v8_fix2`, `_v8_fix3`, `_v8_fix4`, `_v8_fix5`, `_v8_fix6`)

The user asked whether widget animations should have parameters and how other tools do it, then asked for it to be planned in full with the state machine work, which it ties into (machine inputs drive parameters). Everything planned is built (v8 finished the open items).

### Why
Without parameters every variation is a copy of the asset: slide from the left / from the right, 40 px / 200 px, flash red / gold, pulse 1.1x / 1.3x, the same reveal slower for one item. Copies drift and multiply. Reusable libraries (and Fab buyers) expect one asset with knobs.

### How others do it (for reference)
- **Rive:** state machine inputs pick and blend animations, but don't change them. **Data binding** (View Models) binds runtime values to element properties (colors, text, numbers, positions), and animations play relative to them.
- **Lottie:** animations are fixed files. **Dynamic properties** override chosen values at runtime by key path.
- **CSS:** custom properties used inside `@keyframes` - one animation, many results. The cleanest model of what's wanted here.
- **Framer Motion:** variants as functions of a `custom` value (per-item delay or offset).
- **After Effects:** Essential Graphics / MOGRTs expose chosen properties of a template.
- **Unity Animator:** parameters drive the state machine, not keyframe values. Tween libraries are code, so everything is an argument.
- **UMG Sequencer:** no parameters.

### As built (v1)
- **Runtime file:** `Source/NameTBD/WidgetAnimation/WidgetAnimParameters.h/.cpp` (new).
  - `EWidgetAnimParameterType` {Float, Integer, Bool, Color, Vector2D, Choice}.
  - `FWidgetAnimParameter` (declaration): Name, Type, Description, a default per type (`DefaultFloat`, `DefaultInteger`, `bDefaultBool`, `DefaultColor`, `DefaultVector2D`, `Options` + `DefaultOption`), `bUseRange` / Min / Max.
  - `FWidgetAnimParameterValue`: Name, Type, a value per type, `bOverride` (shown "Set Here"); `FromDefault`, `AsVector` / `SetFromVector` (everything as up to four numbers; a Choice as its option's index), `SameValueAs`, `ToDisplayString`.
  - `FWidgetAnimParameterBinding` (on a track): `FieldPath` (dotted, through structs, arrays `[i]`, `FInstancedStruct` and `FInstancedPropertyBag`), `Parameter`, `Scale`, `Offset` ("Distance x -1" for the mirrored side).
  - `FWidgetAnimParameterCondition` (a track's **Plays When**): Parameter, Options, IsSet - a Choice switching a whole track on or off.
  - `FWidgetAnimParameterScope` / `FWidgetAnimParameterScopeStack`: thread-local stack of values pushed by whoever starts an animation (component, host, machine), filtered by Animation / Anim ID.
  - `FWidgetAnimParameterResolver`: `Resolve(Animation, Entry, Outer, AnimId)` (precedence below), `Apply(Layers, OutLayers, OutMuted)` writes bound fields on the played copy and mutes tracks whose Plays When fails; `GetCurrent` / `FCurrentScope` hand the values down to inserted (Play Animation cue) animations.
  - `namespace WidgetAnimParameters`: `WriteField`, `FindField`, `ReadField`, `GetNaturalType`, `CanBind` (Float: numbers and vectors; Integer: numbers, vectors, enums; Bool: bools and numbers; Color: FLinearColor / FColor; Vector 2D: vectors; Choice: enums, names, strings), `IsNameFree`, `MakeUniqueName`.
  - `UWidgetAnimParameterLibrary`: Make Animation Parameter (Float / Integer / Bool / Color / Vector 2D / Choice), Get Animation Parameter Names, Get Animation Parameter Default.
- **Storage:** `UWidgetAnimPresetAsset::Parameters` (+ `FindParameter`); `FWidgetAnimLayer::ParameterBindings` (+ `FindParameterBinding`) and `PlaysWhen` - plain `UPROPERTY()`s, so the fields keep their types.
- **Resolved once per play:** `FWidgetAnimEntryBase::BuildSettings` (entries) and `UWidgetAnimLibrary::BuildLayerSettings` (inserted animations, with the outer values) resolve and apply on the copy that plays.
- **Where values come from, highest wins:** play-time (`PlayWidgetAnimationWithParameters`, `SetWidgetAnimationParameter` on host or component) > the component entry's **Parameters** (`FWidgetAnimEntryBase::Parameters`, rows with "Set Here" ticked; kept in step with the animation by `SyncParameters`) / a machine state's **Parameters From Inputs** > the component's own **Parameters** (`UWidgetAnimationComponent::ComponentParameters`, since v8_fix3) > the parameter's default.
- **Blueprint** (Widget Animation library, `WidgetAnimHost.h`, Target = any widget, searching parents for the component): `Play Widget Animation With Parameters`, `Set Widget Animation Parameter`, `Clear Widget Animation Parameters`, `Get Widget Animation Parameter`; the values are kept on the `UWidgetAnimationComponent` (`Set/Clear/GetWidgetAnimationParameter`). The component wraps every place it starts or ticks animations and machines in `PushParameterScopes` (`FWidgetAnimSetParameterGroup`, `SetParameterGroups`).
- **State machine:** `FWidgetAnimMachineParameterBinding` {Parameter, Input} in a state's `ParameterInputs`, read when the state starts (`GatherParameterInputs` -> `BuildAnimationSettings(..., ParameterValues)`); also per blend sample and random variation.
- **Editor (Widget Animations panel, `SWidgetAnimTimeline.cpp`):**
  - **Animation Parameters** category (its header is custom: title + "+") under whatever is selected (`FWidgetAnimSelectionDetails::CustomizeParameters`): "+" with a type menu; a group per parameter - inline name, type pill combo (the Inputs panel's pill), default; right-click Rename / Delete; children Description, Options, Use Range, Min, Max.
  - **Bind chip** after the value of each bindable Selection-panel row (since fix2: `SWidgetAnimTimeline::MakeParameterBindChip`, put there by `FWidgetAnimSelectionDetails::AddBindChip` - the row is made a custom row from its own default widgets, children kept - on the track rows Track Properties / Track Values add generically and on flat children (`AddChildren`); not on rows laid out by hand (Duration, Delay on percent time, material parameter, registered types' Type Params, key rows). Icons `ParameterBindIcon` / `ParameterBoundIcon`, SVGs `WidgetAnim_ParameterBind.svg` / `WidgetAnim_ParameterBound.svg`): compatible parameters as radio items, **New Parameter From This Field** (named after the field, its value as default), **Mirrored (x -1)**, **Unbind**.
  - **Plays When** row on a track (`AddPlaysWhenRow`).
  - Deleting a parameter removes its bindings; changing its type drops bindings that can't take it. Details refresh on the next tick (`RefreshDetailsNextTick`).
  - Entry rows: `WidgetAnimParameterValueCustomization.h/.cpp` - Set Here box, name, pill, value (a combo for a Choice); editing a value ticks Set Here.
  - `WidgetAnimPanelSource.cpp` `CopyTimelineInto` copies Parameters (saving a timeline as an asset keeps them).

### Done in StateMachine v2
- **Live:** `UWidgetAnimationComponent::RefreshPlayingParameters` - Set / Clear Widget Animation Parameter work out the settings of the entries' animations already playing again and lay them over their tracks one for one (`FWidgetAnimTrack::Settings`), where each part is in its play kept. Deferred past a frame being applied (`bParameterRefreshPending`). A state machine's state takes new values the next time it starts.
- **Bound rows:** while bound, the row's value reads "Distance = 40" (x -1 when mirrored; "(missing)" for a parameter that is gone), greyed, in place of the field (the wrapped value widget in `MakeParameterBindChip`). The Track Editor used to name a track's parameters after its title ("Slide 0.3s  <Distance, Side>"); since v8_fix5 the sections carry no parameter text at all: the Animation Tracks list shows a type-colored pill per bound field after the track's name, and ", N tracks parametrized" after a group's name (see "Done in v8_fix3"); the section tooltip says which field takes which parameter.
- **Chip:** an empty pill in the field's own type's color when unbound, filled in the parameter's type's color when bound, split in two when mirrored (`ParameterMirroredIcon`, `WidgetAnim_ParameterMirrored.svg`), red when bound to a missing parameter.
- **Preview:** each parameter's **Preview** row (tick + the type's editor): the preview, the designer preview and motion paths play that value instead of the default; never saved (`IWidgetAnimPanelSource::SetPreviewParameters`, `SWidgetAnimTimeline::Get/SetPreviewParameter`; `FWidgetAnimAssetPanelSource::BuildFlattened` puts them on its preview entry).
- **View Model:** the Widget Data Binder's trigger action **Set Animation Parameter** (`EWidgetDataTriggerAction::SetParameter`, `ParameterName`): every change of the field sets the parameter (number, bool, Vector 2D, color, or a Name / String / Text / enum as a Choice's option) on the Animation Widget, for the Anim IDs or every animation - live.
- **Missing parameters:** data validation (`UWidgetAnimPresetAsset::IsDataValid`): a binding or Plays When naming a parameter the animation doesn't have is a warning on save / Validate.
- **Type picker:** "+" in Parameters is the same Blueprint-style type picker as the State Machine editor's (`WidgetAnimTypePill::MakeTypePickerButton`).

### Done in StateMachine v8
1. **Curves and tooltips draw parameter values:** `SWidgetAnimTimeline::GetDisplayLayer(LayerIndex)` - the layer with every bound field holding its parameter's value (Preview value when tried, else default; through Scale / Offset), worked out once a frame (`DisplayLayers`, `DisplayLayersFrame`, `DisplayLayersOf`; resolver built from `GetParameters()` + `Source->GetPreviewParameters()`, `FWidgetAnimParameterResolver::Apply`), the layer itself when nothing is bound. Read by the curve editor (`SWidgetAnimGraphCanvas::GetLayer`, `RebuildModels`; edits still go to `GetMutableLayer`) and the track / key tooltips (`MakeLayerToolTip`, `MakeKeyToolTip`). The Track Editor draws no value labels on sections, so nothing else needed it.
2. **Builder mode:** the WidgetAnimationBuilder's working copy had no parameters at all. Now `UWidgetAnimBuilder::Parameters` (EditDefaultsOnly, saved by Create into the asset, copied to the scratch `PreviewAsset`, copied across to live previews by `CopyAuthoredStateFrom`, in `bAffectsPreview`); `SWidgetAnimTimeline::GetParameters` returns it in Animation Timeline mode. Preview values: `UWidgetAnimBuilder::SetPreviewParameterValues` / `GetPreviewParameterValues` (transient `PreviewParameterValues`, on the Class Defaults and every live preview, no transaction) handed to the preview's use as its Parameters in `RebuildPreviewAnimations`; `FWidgetAnimBuilderPanelSource::SetPreviewParameters` / `GetPreviewParameters` call them (both modes). Edit Existing Asset mode already worked (the asset's own Parameters).
3. **Paste:** `CopyLayers` also puts the declarations of the parameters the copied tracks are bound to (or Plays When) on the clipboard - lines `P<tab><Base64 of the exported FWidgetAnimParameter>` after the tracks (two fields: older readers skip them). `PasteLayers`, when tracks name parameters the animation doesn't have, asks once before anything changes (`FMessageDialog` Yes / No / Cancel): Yes adds them (as declared where copied from, else made from the first bound field - `WidgetAnimParameterDeclare::FromField`, shared with New Parameter From This Field); No pastes with those bindings and Plays When removed; Cancel doesn't paste. Only where the panel has parameters (`GetParameters()`).
4. **Compile warning:** see "StateMachine v8 additions" - the Widget Blueprint compile reports the animations' and machines' problems.
5. **Script language** (Python, the Script area / `widget_anim_scripting`): `anim.add_parameter("Distance", ParamType.FLOAT, 40)` (alias `anim.param`; "param Distance: Float = 40"), `anim.parameters` (name -> default), `parameter_type` / `parameter_default` / `parameter_options`, `remove_parameter`; `track.set(SlideDistance="$Distance")` ("Slide.Distance = $Distance"; `"-$Distance"` mirrored), `track.bind(field, parameter, scale=1, offset=0, mirrored=False)`, `unbind`, `binding`, `plays_when(parameter, options)`; `ParamType` names. C++ (`UWidgetAnimScriptAnimation`): `SupportsParameters`, `GetParameterNames`, `GetParameterType`, `GetParameterDefault`, `GetParameterOptions`, `SetParameter` (adds or changes; a type change drops bindings that can't take it, as the panel does), `RemoveParameter`, `BindTrackField` (stores the exact path - `WidgetAnimScriptFields::FResolved::ExactPath`, "TypeParams.<Name>" for a registered type's setting found by fallback), `GetTrackFieldBinding`, `SetTrackPlaysWhen`; `SetTrackProperty` with a value "$Name" / "-$Name" binds (a text / name field keeps a literal "$5" unless the animation has a parameter of that name). Parameters live on the asset, a timeline or the builder's working copy (`GetParametersArray`); a plain list of tracks has none. API reference and the code editor's hints updated.
6. **Particles track:** a bind chip at the end of the **User Parameters** row (any property bag of a registered type's settings): the bag lays its rows out itself, so `FTypeParamsDetails` makes the bag's row a custom row (children kept) with `SWidgetAnimTimeline::MakeBagBindChip(TypeParamsHandle, BagField)` - filled while any of its fields is bound, opening a submenu per bag field (`FillBindMenu`, split out of `OpenBindMenu`), paths `TypeParams.UserParameters.<Name>`. At runtime the bound value is written into the played copy's bag and goes out as a changed value. **Set Particle Track Parameter** / **Clear Particle Track Parameters** (`UWidgetAnimParticleTrackLibrary`, NameTBDNiagara, `DefaultToSelf = Widget`): a User Parameter value (an `FWidgetAnimParameterValue` from Make Animation Parameter, name without "User.") for the Particles tracks the widget plays, by Anim ID or all; kept per widget (`WidgetAnimParticleTrackOverrides`, weak keys pruned on set); `Fire` applies them (`ApplyTrackParameters`: the track's widget, the host and each User Widget around it, nearest last-written) to a copy of the params, only for tracks playing their own system.
7. **Live values in a playing state:** `WidgetAnimStateMachines::RefreshParameters(Instance, Context, bOnlyIfInputsChanged)` rebuilds the playing state's settings (`PlayedAnimation` - the Random Variation picked - and each blend sample) with the values now, and lays them over its tracks one for one (`CompareScriptStruct`, place in play kept; `bSettled` / `bPoseSettled` reset so a pose state takes its new end pose). Called by `Tick` whenever inputs changed (only states with Parameters From Inputs whose values differ from `PlayedParameterInputs`), and by `UWidgetAnimationComponent::RefreshPlayingParameters` (Set / Clear Widget Animation Parameter) for every machine.

### Done in v8_fix3
- **Track list (as of fix5; the section labels of fix3 / fix4 are gone):** in the Animation Tracks list (left), each track row shows after its name a small pill per bound field, in its parameter's type's pin color (`SWidgetAnimBindingPills`, an `SLeafWidget` made by `SWidgetAnimTimeline::MakeListBindingPills`): same types side by side (`ListBindingPillGap` 2 px), a wider gap between types (`ListBindingPillTypeGap` 5 px), types in `EWidgetAnimParameterType` order, red (`ListBindingPillMissing`) for a parameter that is gone, faded on a locked / hidden / read-only track; 10 x 6 px rounded (`ListBindingPillWidth` / `Height`). Read every paint, so it follows bindings and type changes without a list rebuild; collapsed while nothing is bound. No tooltip of its own (since fix6): the row's tooltip is the track's, as in the Track Editor. Each group row says ", N tracks parametrized" after its name (`CountParametrizedTracks`, tracks of the group with any binding) in the name's font in italics (`GetListParametrizedFont`: NormalFontBold with the Italic face), foreground at `ListParametrizedTextOpacity` (0.65); the name and the note share the old name slot, clipped at its end (an overlong name is cut rather than ellipsized). Plays When alone doesn't count (it isn't a value).
- **Track tooltip:** `MakeLayerToolTip` rows **Parametrized** (an `SGridPanel` since fix4, so parameter names start in one column and the "=" signs line up; one line per binding: the field in words - `WidgetAnimParameterEditing::DescribeFieldPath`, e.g. "Value (key 2)", "Translation (key 3, Transform)", "Color (User Parameters)" - then "<-" and the parameter's name in its type's pill color, red "Name (missing)" when gone, then " = value" as the preview plays it (Preview value, else default) and "(x scale + offset)" when not 1 / 0) and **Plays When** ("Side is Left, Right", "Flag is true / false", "Name (missing) - always"). Read off the authored layer.
- **Component Parameters:** `UWidgetAnimationComponent::ComponentParameters` (shown "Parameters", Animation category right after Animations With State Machines, fixed size, hidden while empty via `bHasComponentParameters`): one `FWidgetAnimParameterValue` row per parameter of every animation the widget plays (Anim In / Anim Out entries and every animation its state machines play, `GatherParameterDeclarations`, first declaration of a name wins). Rows kept in step by `SyncComponentParameters` (PostLoad, non-interactive PostEditChangeProperty, and each time the Details are built): a ticked row of the same name and type keeps its value, any other follows the default. Rows use the entry row customization (Set Here box, name, pill, value, Choice combo), the declaration found by `FindComponentDeclaration` -> `FindParameterDeclaration`.
  - Runtime: `PushParameterScopes` pushes them first as an **under-use** scope (`FWidgetAnimParameterScope::bUnderUse`, new arg on the constructor and `FWidgetAnimParameterScopeStack::Push`): `Resolve` applies under-use scopes (only rows with bOverride) after the defaults / outer values and before the use's own values, play-time scopes after. So: play-time > entry Parameters / Parameters From Inputs > component Parameters > default. Applies to every animation the component plays, state machine states included (they are ticked inside `PushParameterScopes`).
  - Editing a row refreshes what is playing (`RefreshPlayingParameters(nullptr, NAME_None)`). `GetWidgetAnimationParameter` reports the component row under the entry's own value, and for an animation no entry plays (or none given) looks the name up among all the component's animations.

### Still open (Animation Parameters)
- Nothing planned. Possible later: the builder's working copy isn't covered by the compile warnings (its tracks live on the builder's Class Defaults, not an animation object); a bound field of the curve editor can be dragged (it changes the constant, which the binding hides).

### Runtime notes
- Resolve on the copy that plays, never the asset: two widgets playing one animation with different values must not interfere.
- Unbound fields cost nothing. Bound ones cost one lookup per play.
- A missing parameter (renamed or removed) falls back to the field's own constant.
- Choice parameters can switch a whole track on or off (Plays When). This is cheaper than authoring two animations, and covers most "variant" needs.

## Data rules and Data Preview (DataBinding v1 - done: `NameTBD_DataBinding_v1.patch`)

Was "Next after the state machine and Animation Parameters - scripting widget behavior from bound data" (goal: rules driven by data bound to the widget - "when Rank changes, play Promote / Demote", "when Score > Best, highlight", "when an entry is added or removed, animate it in / out"; a preview window to edit the bound data or step through sample data and watch the widget react in the designer without PIE; target use case leaderboards).

### How production tools do it (researched for DataBinding v1)
- **Rive Data Binding** (the model followed): View Models (properties: number, string, boolean, color, enum, trigger, **list**, image, nested view model) bound to element properties; **state machine conditions read view model properties directly** (no separate inputs); **converters** chained in groups (Convert to String, Pad, Round, Calculate, **Range Map**, **Interpolator** - smooths a number's change over time, Color Interpolator, Formula, Toggle, Number to List, List to Length; scripted converters with convert / reverseConvert); **lists** repeat a component per item, each item its own view model instance; in the editor, values are edited live and the artboard reacts. (rive.app itself is blocked from this environment; taken from search results and Rive's blog.)
- **XAML / NoesisGUI:** `DataTrigger` (when a binding equals X: setters / storyboards, Enter and Exit actions); `ItemsControl` + item templates; `FluidMoveBehavior` animates items to new layout slots.
- **React / Framer Motion:** keyed lists - identity by key across renders; `AnimatePresence` for enter/exit; `layout` (FLIP) animations for reorders.
- **ProtoPie:** a "Detect" trigger - when a variable/property changes (with conditions), run responses.
- **UMG MVVM:** View Models + conversion functions; List View entries get list item objects.
- **Taken from them:** conditions on data (with direction and field-vs-field), a Rive-Interpolator-like count, keyed list reconciliation (React keys / FluidMove) driving the Flow List's own enter/exit/slide, per-entry data (Rive list items), and in-editor live data (Rive / design tools' variable panels).

### Answers to the open questions
- **Where the rules live:** on the **Widget Data Binder** component (the existing data -> animation component) - Triggers table (rules) + List Bindings table (lists). No new asset: rules sit beside the bindings they read, per widget, and an entry widget's own binder holds its own rules (Rank Went Down -> Promote on the row, not the screen).
- **Lists -> list items:** **List Bindings**: a ViewModel array -> one entry widget per element in a **Flow List** (animated: Enter / Exit / slide) or any panel, matched by **Key Field**.
- **Sample data:** **Sample Sets** on the binder (saved with the Widget Blueprint): values + list rows as text, stepped through in the **Data Preview**.

### As built
- **Triggers** (`FWidgetFieldTrigger`, `WidgetDataBindingTypes.h`) gained:
  - Conditions **Went Up / Went Down** (`EWidgetDataTriggerCondition::WentUp/WentDown`, appended): fire on each change to a larger / smaller number (bool 0/1); no "while" (Play While True acts as Play, like Changed); a rebind isn't a change. `FTriggerState::LastNumber`.
  - **Compare To** (`EWidgetDataCompareTo` Threshold "A Value" / Field "Another Field") + **Compare Field**: Score > Best Score, read live in `EvaluateCondition`, Hysteresis applies; the trigger is evaluated again when either field changes (both subscribed; `HandleViewModelFieldChanged` matches either).
  - Action **Count Into Field** (`CountTo`, appended): counts **Count Field** to the field's number over **Count Duration** (0.6 s) with **Count Easing** (`EWidgetAnimEasing`, EaseOut) - Rive's Interpolator. Number targets take the number (integers round); Text / String / Name targets get `FText::AsNumber` with **Count Decimals** and **Count Group Digits**. At bind: snapped (Evaluate On Bind). A change mid-count carries on from the shown number. Ticked by the binder's ticker (`TickCounts`, `bAnyCounting`). `WriteCount`.
  - Edit conditions updated so Count Into Field shows only its rows; Animation Widget hidden for it.
- **List Bindings** (`FWidgetListBinding`: ViewModel Field (array of structs or objects), List Widget (None = the binder's widget), Entry Widget Class, **Key Field**, **Position Field**, **Animate Changes**). `ReconcileList`, on bind (`bInitial`: nothing animates) and on every broadcast of the field:
  - keys from Key Field (`MakeKey`: a struct member exported as text, or a path on an object; no Key Field = index for structs, the object for objects), made unique (`#2`);
  - first fill removes the panel's designer-placed children (layout placeholders);
  - gone keys: `UFlowList::RemoveItem(bAnimate)` (Exit plays) / `RemoveChild`; kept keys reuse their entry; new keys: `CreateWidget` (outer = the binder's User Widget; in the designer given its designer flags), element pushed **before** insertion;
  - order: Flow List `InsertItem(Entry, i, bAnimate)` / `MoveItem` (slides - the list's springs); other panels: rebuilt in order when it differs;
  - each entry's element -> its **entry binder** (`FindEntryBinder`: on the entry or the first in its tree): struct -> `SetItemData` (copied into the entry's ViewModel by member name via `WidgetDataValues::Convert`, + Position Field = position from 1); object that is Field Notify -> the entry's `SetViewModel` (a list of ViewModels, Rive-style); other object -> `SetSource`.
  - A virtual Flow List (its own Entry Widget Class) is refused with a warning. Entries kept alive by `ListEntryWidgets` (UPROPERTY Transient); `GetListEntries(Index)`.
- **Item data** (`SetItemData(Struct, Data, PositionField, Position)` / `SetItemPosition`, C++): kept in `ItemData` (`FInstancedStruct`), applied in `BindSource` (suppressed, before the source's values; mock values skipped for entries) and at once when bound - so a changed element plays the entry's triggers (Rank Went Down -> Promote).
- **Sample Sets** (`FWidgetDataSampleSet` {Name, Values (field = text), Lists ({ViewModel Field, Rows})}): a row is the element's members as text, `Name=Ann, Score=1200, Id=3` (a struct imported as `(...)`; objects: pairs parsed - `ParsePairs` - and written with `WriteField`; an object with the same key is reused, so its entry sees a change rather than a new ViewModel). **`ApplySampleSet(Index)`** (Blueprint): Mock Values (except fields the set writes - `GetSampleSetFields`) then the set. `SetViewModelFieldFromText`, `GetViewModelFieldAsText` (arrays as "N rows").
- **Designer** (no PIE): **Run Triggers In Designer** (`bRunTriggersInDesigner`, default on): `OnPreConstruct(bIsDesignTime)` -> **`BindForDesigner`**: attach the ViewModel (view's, else created), write Mock Values + the preview's sample set (or the entry's element / source) suppressed, then evaluate triggers on bind and reconcile lists. `bDesignBound`. Off: the old behavior (Mock Values written, nothing runs).
  - **Widget Animation Component:** new **`BeginDesignTimePlayback()`** - at design time, lets play-by-name (Play / Stop By Id, Set Value / Progress) run on the runtime clock (`bDesignTimePlayback`, `RuntimeTick` and `SetWidgetAnimationValues` accept it; visibility reactions stay runtime-only) from the designer's captured authored pose; stops the toolbar preview if running; ended (tracks stopped) by `NotifyOwnerDesignTimeRefresh`. The binder calls it before it plays in the designer; `KeepDesignerAwake` registers an active timer on the binder's widget so the designer repaints while things play.
  - State machine inputs (Set State Machine Input) are not previewed in the designer (machines aren't initialized there; the State Machine editor's preview covers them).
- **Data Preview** (editor, `NameTBDEditor/Private/WidgetDataBinderDetails.h/.cpp`, `FWidgetDataBinderDetails`, registered for `UWidgetDataBinderComponent`; the editor module now depends on `NameTBDMVVM`): a **Data Preview** category at the top of the binder's Details:
  - **Sample Set** row: `<` `>` **Play / Stop** (steps every **Sample Set Interval**, 2 s, round and round), **Mock Values**, **Rebind**, and "Set 2 of 5: Overtake".
  - A row per ViewModel field (`GetViewModelFieldOptions`): its value in the preview now (counts tick in it), editable (Enter writes it with `SetViewModelFieldFromText`); a list field: "N rows" + **Add Row** (copy of the last, new key - numeric max+1 or a suffix - at a random place), **Remove Row** (random), **Overtake** (one row up 1-3 places), **Shuffle**, **Reverse** (`EditListForPreview`, `EWidgetDataListEdit`).
  - Acts on the binder's designer copies: `FindDesignerPreviews()` (archetype chain, as the Flow List Item Style does), cached 0.5 s; edits of the binder's own settings are taken into them next frame (`RefreshDesignerPreviewFrom(Template)`: copies every editable property, then `BindForDesigner`) - the designer doesn't copy a component's edits until it compiles. Nothing is saved.
  - Tooltips: `NameTBD::Tooltips::DataPreview` in `NameTBDTooltips.h`.
- **Leaderboard recipe** (answer to give): ViewModel with `TArray<FLeaderboardRow> Rows` (Field Notify) - row struct {PlayerId, Name, Score, bIsLocalPlayer}; screen binder: List Binding Rows -> Flow List (Snap off, a Line path), Entry Widget Class = WBP_Row, Key Field = PlayerId, Position Field = Rank. WBP_Row has its own binder (ViewModel Class with Rank, Name, Score, DisplayScore, bIsLocalPlayer) and triggers: Rank **Went Down** -> Play Promote; Rank **Went Up** -> Play Demote; Score **Count Into Field** DisplayScore; bIsLocalPlayer **Is True** -> Play While True Highlight. Sample Sets with shuffled rows to try it in the designer. A new row enters (the Flow List's Enter), a gone row leaves (Exit), moved rows slide.

### Still open (data rules)
- Rive-style converters on Field Bindings (Range Map, Formula, Round...) - only Count Into Field so far.
- A list's **sample rows** are text; a struct-aware row editor would be nicer.
- `SetItemData` is C++ only (a generic struct pin needs a CustomThunk node).
- Virtual Flow Lists (Entry Widget Class + Set List Items) aren't filled by List Bindings; a List Binding fills a Flow List with children (fine for leaderboards of tens of rows, not thousands).
- Changes inside an element of a **struct** array only show when the array is broadcast again (as MVVM itself); object ViewModel elements update their entries directly.

## Shapes that hold a child (Shapes v1 - done: `NameTBD_Shapes_v1.patch`)

Built as recommended - see "Shapes hold a child (Shapes v1)" near the top. Decision taken without asking: **Hit Test Outline ships now; Clip To Shape is the follow-up** (it needs Shape Box's offscreen render + input mapping; listed under Open items).

## Planned feature (agreed, not started) — AI Assistant: in-editor chat + MCP server

### What the user wants (added after DataBinding v1 / Shapes v1 - to be built in its own chat)
**Full AI integration that creates polished widgets from a prompt, start to finish in the UE editor, for rapid iteration.** Not a Q&A helper first: the headline is "describe a widget, get a finished, good-looking Widget Blueprint you can keep tweaking by prompt or by hand".

- **Acceptance test (use it verbatim):** *"Create a greeting card that says "Hello User" at the top left with a soft aurora glow at the bottom and faint circle particle effects that bubble from the bottom and fade upward."* Expected result, in one undoable step behind an Apply / Discard preview:
  1. `WBP_GreetingCard` (new Widget Blueprint, unique name), Overlay root, fixed size (~600x360), a Rectangle shape as the card (rounded ~24, dark fill).
  2. Text "Hello User", slot top left, padding ~32, large white font, on top.
  3. Aurora: a Rectangle along the bottom with a linear gradient fill (teal / green / violet fading to transparent upward) - or a shipped UI aurora material on the shape's fill - hit-test invisible; optional looping Widget Animation shifting its colors slowly (ping-pong).
  4. Bubbles: a Niagara UI system made from a shipped "Rising Bubbles" UI template (CPU, local space, sprite, upward velocity + drift, fade by age, soft-dot material), User Parameters set (rate, size, opacity ~0.25, rise speed, color); a Niagara Particles widget at the bottom center, hit-test invisible, between the aurora and the text.
  5. Compiled, saved, and summarized back (widget tree + key settings; optionally a preview capture for a multimodal check).
- **Rapid iteration is the point:** follow-ups ("make the glow purpler", "more bubbles", "move the text to the center", "add a pill button that says Continue", "make it bounce in") are each one or a few tool calls on the widget just made - the AI keeps the target in context (`describe_selected` / the last-built asset) and edits rather than rebuilds. Every edit is one undoable transaction.
- **"Polished" by default:** the tools apply house defaults so a first result already looks finished - spacing scale, corner rounding, font sizes, color palettes (light / dark), Motion presets (the ready-made buttons' Smooth / Snappy / Bouncy, the spring presets), entrance animations from track presets. The model chooses intent ("card", "title", "accent", "soft glow"); the tools turn intent into consistent values. A style/theme asset (palette + type scale + spacing + motion) the user can edit, read by every tool.
- **Can create, in scope:** Widget Blueprints and their widget trees (any UMG widget + the plugin's shapes - which can now hold children, Shape Box, Flow List, ready-made buttons, animated text, Niagara Particles), slot layout, properties; Widget Animation assets / timelines (tracks, keys, easing, springs, parameters); state machines; Widget Data Binder setups (triggers, List Bindings, Sample Sets - so "a leaderboard" works with sample data in the designer's Data Preview); Context Menu Button targets and Looks; presets.
- **Niagara for UI:** from **shipped UI-ready templates** (bubbles, sparkles, confetti, embers, glow ring, trail, rising sparkle loop), duplicated into a new `NS_` asset and tuned through User Parameters and a few safe emitter settings (CPU, local space, loop once, UI material / Material Remap). Validate with the Niagara Particles widget's compile checks. No free-form module-stack authoring in v1 (editor-internal, version-fragile); narrow module tools later only if real prompts need them.
- **Materials:** pick or instance shipped UI materials (aurora, soft glow, shimmer, gradient sweep) and set parameters; no material-graph authoring.
- **Out of scope / suggest only:** Blueprint graph logic beyond narrow tools (bind an event, add a Custom Event); C++ (copy-paste suggestions only); textures (no image generation).
- **Shipped content is part of the feature:** the templates, UI materials and the default theme must ship with the plugin, or the AI has nothing good to pick from.
- **Seeing the result:** an opt-in tool that renders the designer preview to an image for a multimodal model to critique layout/contrast ("text overlaps the glow") and fix it - off by default for cost.

**Suggested v1 tool list (wrapping existing code, chiefly `WidgetAnimScripting` which already adds widgets, tracks, keys and parameters):** `create_widget_blueprint`, `add_widget` (with slot settings), `set_widget_property`, `set_shape_paint` (fill / gradient / stroke), `create_widget_animation` / `add_track` / `set_keys` / `apply_track_preset`, `create_niagara_ui_system(template, params)` / `set_niagara_parameters`, `add_trigger` / `add_list_binding` / `add_sample_set`, `apply_theme`, `describe_selected`, `list_options`, `capture_preview` (opt-in). Expose through the **MCP server first** (drive and test from Claude Code / Claude Desktop immediately), then the in-editor chat panel. Deliver as `NameTBD_AIAssistant_v1.patch`; the greeting card prompt above is its acceptance test.

### Goals (the user's requirements)
- **Free for the plugin creator:** no hosted servers, relays, proxies or shipped API keys - nothing that bills the creator per use. Every token is paid by the plugin's user, through their own provider account (or free, with a local model).
- **As token-efficient as possible for the user:** no "research period" at the start of a chat. The model gets a small cached map, fetches details only on demand, and reads compact facts from tools - never whole assets or source.
- **Full expected functionality:** answer questions about the plugin, generate and edit Widget Animations, shapes, Context Menu Buttons (ready-made included), Flow Lists and presets, and diagnose problems on the selected widget - in the editor and from external assistants.
- Both surfaces: an **in-editor chat panel** and an **MCP server**, sharing one tool layer.

### Architecture
- **New editor-only module `NameTBDAI`** (never in packaged games; Fab: disclosed, opt-in, editor-only). Everything below lives there, except the generated docs (plugin `Resources/AI/`).
- **1. Tool layer (core, shared by both surfaces).** Each tool is a C++ function with a JSON schema; input is validated against the plugin's own structs, then written through the existing functions (`WidgetShapeAnimation`, Shape/Design Target APIs, Flow List presets, Widget Animation assets...). Not code generation - data.
  - Read tools (cheap, compact output):
    - `describe_selected()` - the selected widget(s): class, and only **non-default** settings as terse JSON (filtered, no full struct dumps).
    - `list_options(kind)` - valid enum values, presets, shape kinds, track types, spring presets, so the model never needs docs to know what's allowed.
    - `search_docs(query)` / `get_docs(topic)` - see Docs.
    - `get_asset(path, fields?)` - a Widget Animation / preset asset, optionally only some fields.
  - Write tools: `create_widget_animation`, `add_track` / `set_keys`, `set_shape_look` (a Shape Target state's Look), `add_shape_target` / `add_design_target`, `set_background_shape`, `apply_flow_list_preset` / `set_flow_list_settings`, `create_ready_made_button(kind)`, `save_preset`. Grow the list from real usage; each mostly wraps existing functions.
  - **Safety:** every write is one `FScopedTransaction` (Ctrl+Z reverts it), touches only the selected or named assets/widgets, and the chat shows a preview ("adds 3 tracks to WA_Hover") with Apply / Discard before writing. Validation errors return short, actionable messages the model can correct from.
  - Tool definitions (names, descriptions, field descriptions) are generated from the same reflection data as the docs, so the schemas themselves teach the model most of what it needs.
- **2. Docs, generated, shipped, searched locally (free).**
  - A build/editor step extracts tooltip strings (`NameTBDTooltips.h`), UPROPERTY / UCLASS / UENUM comments and enum tooltips into **small topic files** (a few hundred tokens each, e.g. "Flow List: Play On Selection", "Shape Target Looks", "Hold Button"), plus a hand-written **map** (see below). Regenerated on every release, so docs never drift from code.
  - Local search: a keyword (BM25-style) index built with the docs; optional small precomputed embeddings. No service, works offline.
- **3. In-editor chat panel** (dockable tab, `Window > NameTBD AI`).
  - **Bring your own key:** provider (Anthropic, OpenAI, Gemini) or any **OpenAI-compatible endpoint** (covers Ollama, LM Studio, most local servers and gateways) + model + key. Stored per user (Editor Per-Project/User settings, `config = EditorPerProjectUserSettings`), never in `Default*.ini`, never committed; the plugin ships no key.
  - Thin adapter per provider over `HttpModule` + `Json`: streaming replies, tool calls, prompt caching flags. Unreal's HTTP is enough; no SDKs.
  - Two model choices: a cheap default for Q&A and a stronger one for generation (user picks both).
  - UI: chat, "context" chips (selected widget/asset auto-attached as `describe_selected` output only on request), preview/apply cards for writes, per-request token/cost estimate, clear errors (invalid key, rate limited, model has no tool support).
- **4. MCP server** (same tools, same docs).
  - Runs locally, started by the plugin from the editor (stdio launcher or localhost HTTP/SSE bound to 127.0.0.1 only, with a per-session token). Talks to the running editor, so tools act on the open project.
  - Exposes: **tools** (the tool layer), **resources** (doc topics), **prompts** (the map, plus a few task prompts: "make a hover animation", "debug this Flow List").
  - The AI is whatever the user already runs (Claude Desktop, Claude Code, Cursor...) - their subscription/key. A "Copy MCP config" button in settings writes the client JSON for them.

### Token efficiency (the core design)
- **Map, cached:** a fixed system prompt of ~1-2k tokens: what each system is (Widget Animations, Flow List, Shapes, Context Menu Button, ready-made buttons), key terms, and which tool answers what. **Byte-for-byte stable** across chats and versions-of-a-session (anything per-chat goes after it) so provider prompt caching applies: Anthropic (explicit `cache_control` breakpoint after the map and tool list), OpenAI (automatic prefix caching), Gemini (context caching). Cached tokens cost ~10% after the first request. Tool definitions are also stable and sit inside the cached prefix.
- **Details on demand:** no docs in the prompt beyond the map; the model calls `search_docs` / `get_docs` for the one or two topics a question needs (a few hundred tokens).
- **Facts from tools, not reading:** `describe_selected` returns only non-default values; `list_options` returns enums/presets; outputs are compact JSON, no prose, no full structs, no source files.
- **Short conversations:** new chat per task is cheap (cached map); long histories are trimmed or summarised by the panel; tool results older than a few turns are collapsed to one-line summaries.
- **Lean writes:** write tools accept partial data (only the fields to change); defaults fill the rest.
- Target: ~1-2k cached tokens of fixed prefix + a few hundred tokens per question → cents or less per chat, no warm-up.

### Cost to the creator: zero per use
- No relay, proxy, hosted search or analytics; docs are searched locally; MCP runs on the user's machine.
- Remaining creator costs: one-off API usage while developing/testing; support (mitigated by clear in-editor errors and setup docs).
- No commercial agreement with providers needed: users call public APIs with their own credentials under their own provider terms.

### Disclosure (Fab listing + settings page)
- AI features are optional, editor-only, and need the user's own provider account (or a local model); usage is billed by that provider; the rest of the plugin works without them.
- Exactly what is sent: the map, tool definitions, the user's messages, and tool outputs (selected widget settings, requested doc topics) - never the whole project. Show provider/model and a per-request token estimate.

### Build order
1. Tool layer + generated docs + local search (useful alone, testable without any model).
2. MCP server on top (small; lets the tools be exercised from existing assistants immediately).
3. In-editor chat panel + provider adapters (Anthropic, OpenAI, Gemini, OpenAI-compatible) + caching + previews.
4. Grow tools from real requests; keep the map stable.

### Risks / notes
- Local/small models handle tool calls worse: strict validation, short error messages, and a "model lacks tool support" check.
- Provider APIs change: keep adapters thin; the OpenAI-compatible adapter is the fallback.
- Blueprint graph generation is fragile; keep to narrow, well-tested tools (e.g. bind an event) rather than free-form graphs. C++ generation stays copy-paste only.
- Never write outside the selected/named targets; every write undoable.

## Previous change sets
- `NameTBD_FlowList_v1_to_v2.patch` (+ `_fix1`): virtual lists, layout cache, Hover Magnify, Scroll Bar v1, state machine plumbing (Value Input, Item Value, built-in Selected), Flow List Item Style, Jump To State, disabling transitions from a wire's menu. Committed.
  - Rule from `_fix1`: never put `BlueprintReadOnly` / `BlueprintReadWrite` on an `FInstancedPropertyBag` UPROPERTY.
- `NameTBD_TrackEditorPolish_v1.patch` (+ `_fix1`, `_fix2`): Scale X/Y, Sound curves in the curve editor, Design track, Frames display, save naming, Add Track menu groups, track editor polish. Committed.
- `NameTBD_TrackEditorPolish_v2.patch` (+ `_fix1` … `_fix10`). Committed.
  - Stagger only on text tracks. Duration shown on every track. Design track blends member by member and cross-fades images and fonts.
  - Selection panel split into Track Properties / Track Values. Plugin-registered types laid out as native. "User-defined Track" naming. Read-only Type Params. Blueprint type Settings follow their struct.
  - Hold easing. Frames preview with inline FPS.
  - Text targets joined by `&&`. Canvas Text Editor (`WidgetAnimTextCanvasEditor.*`, Edit Text chip, own undo history).
  - Ctrl+Z / Ctrl+Y in the track and graph editors.
  - Start/end markers as an SVG triangle (`WidgetAnim_RangeMarker.svg`). Pixel-aligned markers and playhead (`SnapColumn`).
- `NameTBD_FlowList_v1.patch`: the Flow List (`UFlowList`): paths, effects by distance, spring physics, push/pop transitions, and 9 presets.

## Open items carried forward
- **Shapes: Clip To Shape** (Shapes v1 follow-up) - mask a shape's child to its outline: render the child offscreen (as Shape Box's warped path: `SVirtualWindow` + `FWidgetRenderer` + `ICustomHitTestPath`) and draw the fill mesh with that texture (UV = local / size), opt-in.
- **Data rules** (DataBinding v1 follow-ups): see "Still open (data rules)".
- **Niagara User Parameters - what production users will still expect** (from the discussion; not done):
  - Reset to default goes to the **system's value** (today the bag's reset arrow goes to the type's zero).
  - The Niagara variable metadata carried over: descriptions as tooltips, min/max and slider ranges, editor sort order and advanced display.
  - A "changed" marker per row.
  - Getters (`Get ... Parameter`).
  - Blueprint setters for curves and arrays (today only through Get Niagara Component and Niagara's own nodes).
  - A track that keys a Float / Color User parameter over an animation (like the Material Scalar / Vector tracks).
- **DEVELOPER ONLY settings** (Editor Preferences > Plugins > Widget Animations Editor): before publishing, remove the category and settle its values back into `NameTBDColors.h`.
- A literal "&&" can't be targeted by a text track's Part of the Text.
- **State machine** Option B (true sub-machines) - not wanted until Option A proves not enough. Animation Parameters: nothing planned left.

## Latest change sets — `NameTBD_DataBinding_v1.patch` and `NameTBD_Shapes_v1.patch` (each applies on its own on top of `NameTBD_StateMachine_v8_fix12.patch`, i.e. HEAD `3c0886a`; no file in common, so either order; code only, 2 new files in DataBinding; untested in editor)

Checked with `git apply --check` on `3c0886a` (separately and together).

### Asked
> Build items 1 (scripting widget behavior from bound data) and 2 (shapes that hold a child). For 1, study how production-grade applications do this, like Rive for example.

### Done
- **DataBinding v1:** see "Data rules and Data Preview (DataBinding v1)" - research summary, the three open questions answered, Went Up / Went Down, Compare To another field, Count Into Field, List Bindings (keyed add / remove / move into a Flow List), item data for entries, Sample Sets, design-time binding + component design-time playback, the Data Preview panel.
- **Shapes v1:** see "Shapes hold a child (Shapes v1)" - shapes are content widgets with a Shape Slot, Content Area (Bounds / Inside Stroke / Inscribed), Size To Content, Hit Test Outline.

### Files
- DataBinding: `NameTBDMVVM/Public/WidgetDataBindingTypes.h`, `NameTBDMVVM/Public/WidgetDataBinderComponent.h`, `NameTBDMVVM/Private/WidgetDataBinderComponent.cpp`, `NameTBD/WidgetAnimation/WidgetAnimationComponent.h` (BOM kept) / `.cpp`, `NameTBDEditor/NameTBDEditor.Build.cs`, `NameTBDEditor/Private/NameTBDEditor.cpp`, `NameTBDEditor/Public/NameTBDTooltips.h` (BOM kept), new `NameTBDEditor/Private/WidgetDataBinderDetails.h/.cpp`.
- Shapes: `NameTBD/Widgets/Shapes/ShapeWidget.h/.cpp`, `WidgetShapeGeometry.h/.cpp`, `WidgetShapeAnimation.cpp`, `NameTBD/Buttons/ContextMenuButton.cpp` (PaintOnly painter), `NameTBDEditor/Private/WidgetShapePresets.cpp`.

### Decisions made without asking
- Rules live on the Widget Data Binder (Triggers + List Bindings), not a new asset; entries carry their own rules in their own binder.
- New enum values appended (`WentUp`, `WentDown`, `CountTo`) rather than new enums - enums serialize by name, so old assets are unaffected; `EWidgetDataCompareTo` is new.
- Run Triggers In Designer defaults **on** (the point of the preview); Off restores the old "mock values only" designer behavior.
- A List Binding's first fill removes children placed in the designer (they are layout placeholders, as an items control's are).
- Flow List is the animated target (its Enter / Exit / slide); other panels are re-ordered at once.
- Sample rows are text (`Name=Ann, Score=1200`), the way Unreal writes structs, rather than a new row editor.
- Shape Slot defaults Center / Center, padding 0; Content Area defaults Inscribed; Size To Content defaults on (a shape without a child is unchanged either way).
- Hit Test Outline routes visibility only while on and only at runtime, so Visibility bindings and the designer are untouched otherwise.
- Clip To Shape deferred (see Open items).

### Unverified (no engine here)
- `UWidget::GetDesignerFlags` / `SetDesignerFlags` on created entries (editor-only API), and that UI components of entries created in the designer get `OnPreConstruct(true)` (they should: the entry gets the designer's flags, including Execute PreConstruct).
- Hit cells: painted outside `GetChildren` with the shape as paint parent - the same technique as Shape Box's hit cells; with global invalidation the cells' grid entries persist between repaints.
- `AlignAxis` uses the `EHorizontalAlignment` / `EVerticalAlignment` numbering (Fill 0, Left/Top 1, Center 2, Right/Bottom 3).
- `UContentWidget`'s constructor taking `FObjectInitializer` (the shape's constructor now passes it).
- `FText::AsNumber` / `FNumberFormattingOptions` setters, `FScriptArrayHelper::SwapValues/RemoveValues/AddValue/EmptyValues`.

### Check by hand
- **Shapes:** an Ellipse with a Text Block inside: Size To Content on -> the circle grows round the text, the text stays inside (Inscribed); Content Area Bounds -> text uses the box; a Star with a stroke Inside -> text clear of it; Shape Slot padding / alignment work; a shape with no child is the size it was. Old Widget Blueprints with shapes load and look the same; Shape tracks / Context Menu Button Looks / presets unchanged (no Content rows in them). Hit Test Outline on a Visible circle over a button: the corners click the button under it, the circle's inside doesn't; a button inside the circle still clicks.
- **Data rules:** a binder with a trigger Score **Went Up** -> Play; Compare To **Another Field** (Score > Best Score) -> Play While True; **Count Into Field** Score -> DisplayScore (Text): the number ticks. In the designer (no PIE): the binder's Details show **Data Preview** at the top; typing a value plays the triggers on the preview; `<` / `>` / Play step Sample Sets.
- **Leaderboard:** the recipe above with a Flow List: Sample Sets reorder rows -> rows slide; Add Row -> enters; Remove Row -> exits; Overtake -> a row climbs and its Rank Went Down plays Promote; the local player's row highlights. At runtime, broadcasting the Rows field does the same.

## Change set before — `NameTBD_StateMachine_v8_fix12.patch` (applies on top of `NameTBD_StateMachine_v8_fix11.patch`; code only; untested in editor)

Applies in order: v8, v8_fix1 … v8_fix12 (checked with `git apply` on `f64a092`).

### Asked
> Add a small little blurb anchored at the top left of the widget when Paths Only is on "Paths Only On: Widget cannot be clicked on" with the same border style as the key's time on the motion path.

### Done (`FMotionPathLayer::PaintPaths`, `WidgetAnimMotionPath.cpp`)
- While Motion Path and Paths Only are on and paths are drawn (`HasPaths` - exactly when Paths Only blocks clicks), the designer shows "Paths Only On: Widget cannot be clicked on" (`LOCTEXT PathsOnlyNote`) in a key time label's box (`DrawLabel(..., LabelBackground, bOutline)` - dark, rounded, dim white outline), its bottom left 4 px above the **top left corner of the designed widget** (the designer's preview `UUserWidget`, mapped into the designer's space as the paths are - through its tick-space render transform, so it follows zoom and pan), clamped 4 px inside the designer. If the preview isn't found: under the designer's toolbar (`StatusAnchor`).
- `GetPreview()`: the preview found by `FindWidgetAnimDesignerPreview`, kept weak, searched again at most once a second while unknown (it is remade on each compile).

### Decisions made without asking
- "The widget" read as the widget being designed (the preview's root); the note sits just above its corner rather than over it, so it covers none of it - pushed inside when the corner is off screen.
- Text exactly as asked.

### Files
- `NameTBDEditor/Private/WidgetAnimMotionPath.cpp`.

### Check by hand
- Paths Only on (with an animation open that moves something): the note above the widget's top left, same box as a key's time; zoom / pan the designer - it follows the corner; scroll the corner off screen - it stays inside. Paths Only off, or Motion Path off: gone.

## Change set before — `NameTBD_StateMachine_v8_fix11.patch` (applies on top of `NameTBD_StateMachine_v8_fix10.patch`; code only; untested in editor)

Applies in order: v8, v8_fix1 … v8_fix11 (checked with `git apply` on `f64a092`).

### Asked
> The grab hand should go back to a normal cursor when not over a keyframe/handle

### Cause and fix (`FMotionPathInput`, `WidgetAnimMotionPath.cpp`)
- Leaving a diamond only cleared the hover; the cursor set on the platform stayed until Slate asked the designer again, which it doesn't on every move. Now FMotionPathInput remembers when the cursor showing is its own (`bCursorOwned`, set wherever it sets a hand / grab) and gives it back as `EMouseCursor::Default` once (`GiveBackCursor`) when the mouse is off every diamond and handle: on the move that leaves one, in `Tick` when the hover ends without a move (the designer changed under a still mouse), and on the release of a drag let go elsewhere. A drag let go over a diamond or handle lights it and shows its open hand again (hover updated on release).

### Files
- `NameTBDEditor/Private/WidgetAnimMotionPath.cpp`.

### Check by hand
- Hover a key: open hand; move off it: normal arrow at once. Drag a key and let go off it: arrow; let go on it: open hand. Over a designer toolbar control after a key: that control's own cursor on the next move.

## Change set before — `NameTBD_StateMachine_v8_fix10.patch` (applies on top of `NameTBD_StateMachine_v8_fix9.patch`; code only)

Applies in order: v8, v8_fix1 … v8_fix10 (checked with `git apply` on `f64a092`).

### Reported (build of fix9)
- `WidgetAnimMotionPath.cpp(927): error C2248: 'SWidget::GetCursor': cannot access protected member declared in class 'SWidget'`.

### Fixed
- `FMotionPathLayer::ClaimDesignerCursor` no longer asks the designer for its own cursor; it sets the `Cursor` attribute unconditionally (the UMG designer sets none) and still unsets it when the layer goes or the designer changes.
- **Rule for later:** `SWidget::GetCursor()` is protected (5.7); `SetCursor` is public.

### Files
- `NameTBDEditor/Private/WidgetAnimMotionPath.cpp`.

### Check by hand
- Build; the rest of the build log after `WidgetAnimMotionPath.cpp` (the reported log stopped at action 12/17) - send any further errors.

## Change set before — `NameTBD_StateMachine_v8_fix9.patch` (applies on top of `NameTBD_StateMachine_v8_fix8.patch`; code + 1 new SVG; untested in editor)

Applies in order: v8, v8_fix1 … v8_fix9 (checked with `git apply` on `f64a092`).

### Asked
> The cursor is flicking back and forth between the open grab hand and normal when hovering the keyframe. It should be the open grab hand. Also make the hitboxes for everything on the motion path that can be interacted with a bit bigger. Add another button, beside motion paths. This button will disallow widgets to be selected when motion paths are on. This is because widgets right now can easily take the hitbox of keyframes and the handles.

### Done
- **Cursor flicker** (`WidgetAnimMotionPath.cpp`): FMotionPathInput set the cursor only on the real mouse moves it sees; Slate also asks the widgets under the mouse for a cursor on its own (after synthesized moves, which input preprocessors don't get), and the designer answered with its arrow. Now:
  - `FMotionPathLayer::GetDesignerCursor()` - grab hand over a draggable diamond or a handle, pointing hand over a pick-only one, closed grab while pressed / dragging, unset elsewhere;
  - `ClaimDesignerCursor()` (each paint, after `FindDesignerWidget`) sets it as the designer widget's `Cursor` attribute (`SWidget::SetCursor`, a `CreateSP` getter) - unconditionally since fix10 (`SWidget::GetCursor` is protected); `ReleaseDesignerCursor()` unsets it when the layer goes or the designer changes. So the designer's own answer to Slate is the motion path cursor while over a key.
  - `FMotionPathInput::Tick` also keeps the cursor every frame: while pressed, `GetDesignerCursor()`; while something is hovered, a fresh `FindHit` at the cursor (clears hover when off it).
- **Bigger hit areas** (`NameTBDColors.h` MotionPath): `KeyHitSize` 15 -> 21 (diamonds and a track's start / end markers), `HandleHitSize` 13 -> 19. The key labels keep their place: `Reach` now from `KeySize` with `LabelGap` 6 -> 9.
- **Paths Only toggle** - right beside Motion Path in the Track Controls pill (`WidgetAnim_PathsOnly.svg`, new: a key diamond and a pointer; fallbacks `Icons.SelectInViewport` / `LevelEditor.SelectMode` / `Icons.Lock`), enabled only while Motion Path is on. Setting `UWidgetAnimEditorSettings::bMotionPathPathsOnly` (config, "Paths Only (No Widget Selection)" in Editor Preferences > Motion Paths; `SetMotionPathPathsOnly`), tooltip `WidgetAnimTips::MotionPathsOnlyTooltip`.
  - While on and a designer is drawing paths (`HasPaths`), a left press on the designer that isn't on a key / handle is consumed by FMotionPathInput (`IsPathsOnlyPress`), and its release too (`bSwallowedLeft`): no widget selection, no marquee. Presses on the designer's own controls pass (`IsOnDesignerControl`: anything between the mouse and the designer that takes keyboard focus - its toolbar buttons, combos, boxes). Right / middle buttons (pan, zoom) untouched; the Hierarchy still selects.
  - **Double clicks** (`HandleMouseButtonDoubleClickEvent`, new, always on): the second press of a double click comes as a double click event, which FMotionPathInput didn't see - on a key it went to the designer and selected / opened the widget under the key. Now it is handled as a press (on a key or handle, or anywhere with Paths Only).

### Files
- `NameTBDEditor/Private/WidgetAnimMotionPath.cpp`, `WidgetAnimEditorSettings.h/.cpp`, `SWidgetAnimTimeline.cpp` (the toggle); `NameTBDEditor/Public/NameTBDColors.h`, `NameTBDTooltips.h` (BOM kept); `Resources/WidgetAnim_PathsOnly.svg` (new).

### Decisions made without asking
- Named "Paths Only"; off by default.
- Paths Only blocks only the left button (select, marquee, double click); designer toolbar controls stay usable; widgets can still be selected from the Hierarchy.
- The cursor attribute is put on the designer widget (and taken off again when the layer goes).

### Unverified (no engine here)
- Not compiled.
- That the UMG designer widget falls back to `SWidget::OnCursorQuery` (its `Cursor` attribute) when not panning; if it doesn't, the per-frame `Tick` keeps the cursor instead (a flicker could then remain at most for a frame now and then) - say if it still flickers.
- That the designer's toolbar controls report `SupportsKeyboardFocus` (buttons and combos do by default).

### Check by hand
- Hover a Transform key's diamond and hold still: open grab hand, steady; press: closed grab; a pick-only key: pointing hand.
- Keys and handles are easier to hit, a few px further out.
- Paths Only on: clicking a widget on the designer (off a key) selects nothing, no marquee; keys and handles still work; the designer toolbar still works; right-drag still pans. Off: as before. Disabled while Motion Path is off.
- Double-click a key: no widget gets selected under it.

## Change set before — `NameTBD_StateMachine_v8_fix8.patch` (applies on top of `NameTBD_StateMachine_v8_fix7.patch`; code only; untested in editor)

Applies in order: v8, v8_fix1 … v8_fix8 (checked with `git apply` on `f64a092`).

### Asked
> Add a little bit of padding to the icons to the left, so it fits nicer. Also, I like how the motion paths fill in their opacity as the track runs. But when the track isn't running, it should be full opacity. When the track isn't running it should be fully opacity, then when the track runs, the motion path fills in its opacity from start to finish (currently its doing the opposite, dimming the opacity start to finish).

### Done
- **Cue track icons** (Event, Sound, Particles) sit `CueTrackIconLeftPadding` (4 px, `NameTBDColors.h` WidgetAnimEditor) further in from the section's start; the name follows the icon as before (`SWidgetAnimTimelineTracks::OnPaint`).
- **Motion path fill** (`FMotionPathLayer::PaintPaths`): stopped or paused, the whole path (line, underlay, jump dashes, frame dots) is at full opacity. While the preview plays (`IWidgetAnimPanelSource::IsPlaying`, read each frame into `bPreviewPlaying`), the stretch not yet played is drawn at `PathUnplayedOpacity` (0.4) and what the playhead has passed at full - so the path fills in from its start. Was the reverse (played stretch dimmed, always). `PathPastOpacity` renamed `PathUnplayedOpacity`.

### Files
- `NameTBDEditor/Private/SWidgetAnimTimeline.cpp`, `NameTBDEditor/Private/WidgetAnimMotionPath.cpp`, `NameTBDEditor/Public/NameTBDColors.h`.

### Decisions made without asking
- Scrubbing or paused mid-way counts as "not running": full opacity everywhere.
- The padding applies to the cue icon only (the hidden / locked / pinned glyphs before it are unchanged).

### Check by hand
- Event / Sound / Particles sections: a little room between the section's left end and the icon.
- Motion path: full while stopped; press play - it starts faint and fills in to full behind the playhead; pause - full again.

## Change set before — `NameTBD_StateMachine_v8_fix7.patch` (applies on top of `NameTBD_StateMachine_v8_fix6.patch`; code only; untested in editor)

Applies in order: v8, v8_fix1 … v8_fix7 (checked with `git apply` on `f64a092`).

### Reported
> They are appearing as white text on white borders (screenshot: the motion path key's time and position boxes solid white)

### Cause and fix
- fix6 drew each label box twice: the fill, then an outline brush (`FSlateRoundedBoxBrush(Transparent, 3, LabelOutline, 1)`) with a white tint. `FSlateDrawElement::MakeBox` tints only a rounded box's **fill** (the brush's own fill color is ignored) and leaves its outline in the brush's own color - so the "outline" pass painted a solid white box over the fill, under the text.
- Now one draw per box: `LabelOutlinedBrush()` = white fill (tinted to `LabelBackground` / `LabelValueBackground` by MakeBox) with the 1 px `LabelOutline` baked in; the text one layer above. Layers per label 2 (was 3).
- **Rule for later:** with MakeBox, a rounded box's fill color is the tint; its outline color is the brush's. Never draw an outline-only rounded brush with an opaque tint.

### Files
- `NameTBDEditor/Private/WidgetAnimMotionPath.cpp`.

### Check by hand
- Motion path key labels: dark time box, orange-tinted dark position box, both with a thin dim white edge; light text readable.

## Change set before — `NameTBD_StateMachine_v8_fix6.patch` (applies on top of `NameTBD_StateMachine_v8_fix5.patch`; code only; untested in editor)

Applies in order: v8, v8_fix1 … v8_fix6 (checked with `git apply` on `f64a092`).

### Asked
> Hovering a track in the track viewer should give the exact same tooltip as it does in the editor

Then, before this patch was installed (so folded into it):
> 1. The Particle track needss an icon like event and sound have. 2. In the motion paths, the keyframe's value sometimes overrides with its time value. 3. Give the border for the keyframe value in the motion path a slight orange tint to distinguish it visually from the time, and give both a dim white outline.

### Done
- Animation Tracks list rows now show the Track Editor's tooltip for that track: `SWidgetAnimTimeline::MakeListTrackToolTip(LayerIndex, bCollapsedHead)` builds the same `SWidgetAnimTrackToolTip` the Track Editor uses, its content made fresh on each open by `MakeLayerToolTip` (a track row, including tracks of a collapsed track opened in the list) or `MakeCollapsedToolTip(Head)` (a collapsed track's own row). Replaces `ListLayerRowTooltip` / `ListCollapsedRowTooltip` there (the two tips stay in `NameTBDTooltips.h`, now unused).
- The binding pills lost their own tooltip (it would have covered the row's); `DescribeBindings` and the forward declaration of `DescribeFieldPath` removed.
- **Particles track icon:** the Track Editor draws a Niagara system's icon before a Particles track's name, as it does the event icon for Event and the sound icon for Sound (`SWidgetAnimTimelineTracks::OnPaint`, same size / tint settings `CueTrackIconSize` / `CueTrackIconTint`). The editor module doesn't link NameTBDNiagara, so the track is recognized by its settings struct's name (`WidgetAnimParticleCueParams`); the icon is `ClassIcon.NiagaraSystem` found in whichever style holds it (Niagara's editor style, via `FSlateIconFinder::FindIcon` + `GetOptionalBrush(..., nullptr, nullptr)`), else `ClassIcon.ParticleSystem` / `ParticleSystemComponent` / `Emitter` from the editor style; looked up until found.
- **Motion path key labels** (`FMotionPathLayer::PaintPaths`, `WidgetAnimMotionPath.cpp`):
  - The value covered by a time: every label was drawn on one layer, so with Show Key Times a neighboring key's time label drawn later lay over the hovered / dragged key's position. Labels are now drawn in two passes, the hot keys' (hovered or dragged) last on a higher layer (`HotLabelLayer`); a non-hot key at the same spot as a hot one (another track's key there) draws no label of its own.
  - The time and the position are two boxes now, `LabelPartGap` (3 px) apart: the time on `LabelBackground`, the position on `LabelValueBackground` (dark with an orange tint); both with a 1 px dim white outline (`LabelOutline`, baked into `LabelOutlinedBrush` since fix7). Status lines (WidgetAnim.LogMotionPaths) unchanged.

### Files
- `NameTBDEditor/Private/SWidgetAnimTimeline.cpp`, `SWidgetAnimTimeline.h` (`MakeListTrackToolTip` declared, private); Particles icon; include `Styling/SlateIconFinder.h`.
- `NameTBDEditor/Private/WidgetAnimMotionPath.cpp` - key labels.
- `NameTBDEditor/Public/NameTBDColors.h` - `MotionPath::LabelValueBackground`, `LabelOutline`, `LabelPartGap`.

### Decisions made without asking
- "Track viewer" = the Animation Tracks list, "editor" = the Track Editor (as in fix5). The collapsed track's list row got the collapsed bar's tooltip too, for the same reason.
- The pin / eye / lock buttons on a row keep their own tooltips.
- Rows of a nested animation track's tracks (opened under an animation track) are unchanged.
- "Value overrides with its time" read as the overlap above (the label text itself was one string, so it couldn't swap); if it still happens with Show Key Times off, it is something else - say when.
- "Border ... orange tint" read as the value box's background tinted; the outline is the dim white on both.

### Unverified (no engine here)
- Not compiled. `FSlateIconFinder::FindIcon(FName)` / `FSlateIcon::GetStyleSet()` written to the UE 5 API.
- That Niagara's style really names its system icon `ClassIcon.NiagaraSystem` in 5.7 (else the fallbacks draw, or none).

### Check by hand
- Hover a track row in the list and the same track's section: identical tooltips (title, rows, Parametrized grid, Curve). Hover the pills: the same tooltip. A collapsed track's row: the collapsed bar's tooltip.
- A Particles track: a Niagara icon before its name, like Event / Sound tracks.
- Motion paths with Show Key Times on, keys close together: hover / drag one - its time and position boxes sit over the neighbors' labels; the position box is orange-tinted, both outlined.

## Change set before — `NameTBD_StateMachine_v8_fix5.patch` (applies on top of `NameTBD_StateMachine_v8_fix4.patch`; code only; untested in editor)

Applies in order: v8, v8_fix1 … v8_fix5 (checked with `git apply` on `f64a092`).

### Asked
> I dont really like the text on the track. Remove it. The better solution is to have the type color pill on the track viewer on the left, so its easy to see at a glance, for example, the scale track has 2 floats and one vector 2d binded. Make sure it fits on the track viewier nicely and it formatted well. Also, after the layer group name in the track viewer, add a comma and in italicized slightly dimmed text say N tracks parametrized.

### Done
- Section text removed: the Track Editor's sections are as before fix3 (title, length, easing), no parameter text.
- Animation Tracks list: a pill per bound field after each track's name, in its parameter's type color, grouped by type; tooltip names each binding.
- Group rows in the list: ", N tracks parametrized" after the name, italic, dimmed (65%).
- The section tooltip's Parametrized grid (fix4) stays.

### Files
- `NameTBDEditor/Private/SWidgetAnimTimeline.cpp` - section label reverted; `SWidgetAnimBindingPills` (before `SWidgetAnimListRow`, with a forward declaration of `WidgetAnimParameterEditing::DescribeFieldPath`); pills slot in `MakeListLayerRow`; group row name slot; `MakeListBindingPills`, `CountParametrizedTracks`, `GetListParametrizedFont`.
- `NameTBDEditor/Private/SWidgetAnimTimeline.h` - those three declared (private).
- `NameTBDEditor/Public/NameTBDColors.h` - `SectionParametrizedTextOpacity` removed; `ListBindingPill*`, `ListParametrizedTextOpacity` added.

### Decisions made without asking
- "Track viewer on the left" read as the Animation Tracks list (the left part of the Track Editor), both for the pills and for the group note; the group label drawn beside each group's bar in the timeline is unchanged.
- One pill per bound field (two fields bound to one Float are two Float pills), right after the track's name, before the italic "– Type / inserted from" note.
- Drawn pills (rounded boxes in the pin colors, sized by `NameTBDColors.h`) rather than the `Kismet.VariableList.TypeIcon` brush, so their size and spacing are exact in the 24 px row.
- Plays When gets no pill and doesn't count toward "N tracks parametrized".
- A collapsed track's own row and the rows of a nested animation track get no pills (tracks of a collapsed track opened in the list do - they use `MakeListLayerRow`).

### Unverified (no engine here)
- Not compiled.
- How a very long group name looks: it is clipped at the slot's end (no ellipsis) so the target pill keeps its place.

### Check by hand
- Scale track with 2 Float and 1 Vector 2D bindings: after "Scale", two green-ish Float pills close together, then a gap, then a Vector 2D pill; hover them for the list. Bind / unbind in the Selection panel: the pills follow at once. Change a parameter's type: their color follows. Lock or hide the track: they fade.
- Group row: "Layer Group 1, 2 tracks parametrized" with the note in italics, a little dimmer; none when nothing is bound.
- Sections: no "values parametrized" text anywhere.

## Change set before — `NameTBD_StateMachine_v8_fix4.patch` (applies on top of `NameTBD_StateMachine_v8_fix3.patch`; code only; untested in editor)

Applies in order: v8, v8_fix1, v8_fix2, v8_fix3, v8_fix4 (checked with `git apply` on `f64a092`).

### Asked
> remove the curly braces. The N values parametrized should come after the track name, with a comma and space. Also in the hover tooltip align the variable name beginnings and the equal signs on all the variables so its formatted nicely. The text is too dim as well, brighten it slightly
> I also noticed there is a stray 's' after the track duration field. That should be inside the field box

### Done
- Track label: no braces; ", N values parametrized" right after the name, before the length: "Scale, 2 values parametrized  0.30s". Opacity 0.5 -> 0.65 (`SectionParametrizedTextOpacity`; `SectionParametrizedGap` removed - the comma is the gap).
- Tooltip **Parametrized** row: an `SGridPanel` (field | arrow | parameter | "= value"), so names and "=" signs line up.
- Duration row: the unit is inside the number box ("0.3 s", "25 %" on a percentage timeline) through `WidgetAnimTimeline::FUnitNumericInterface` (a `TDefaultNumericTypeInterface<float>` that appends the unit and strips it when typed; the unit is read each time, so it follows the time mode). The percentage Duration / Delay row (`AddPercentTimeRow`) had the same stray "%" outside its box and got the same fix.

### Files
- `NameTBDEditor/Private/SWidgetAnimTimeline.cpp` - track title in three pieces, tooltip grid, `FUnitNumericInterface`, Duration and percent rows; include `Widgets/Input/NumericTypeInterface.h`.
- `NameTBDEditor/Public/NameTBDColors.h` - opacity 0.65, gap constant removed.

### Decisions made without asking
- With a target ("Slide → Button") the count goes after the target, before the length: "Slide → Button, 2 values parametrized  0.30s".
- The comma is dimmed and italic with the rest.
- The percent rows got the same fix as Duration so all three look alike.

### Unverified (no engine here)
- Not compiled. `TDefaultNumericTypeInterface<float>` overrides (`ToString`, `FromString`, `IsCharacterValid`) written to the UE 5 signatures.

### Check by hand
- A track with 2 bound fields: "Scale, 2 values parametrized  0.30s", the middle part dim italic but easier to read than before; narrow the section: the count drops and the plain title stays.
- Hover it: parameter names start in one column, "=" signs line up.
- Duration: "0.3 s" inside the box; type "0.5" or "0.5 s" - both take; switch the timeline to percent - "%" inside; a percent timeline's Delay row the same.

## Change set before — `NameTBD_StateMachine_v8_fix3.patch` (applies on top of `NameTBD_StateMachine_v8_fix2.patch`; code only; untested in editor)

Applies in order: v8, v8_fix1, v8_fix2, v8_fix3 (checked with `git apply` on `f64a092`).

### Asked
> On the track, instead of saying the parameter names in brackets <>, just say "{N values parametrized}" (when there are parameters binded) in dimmed and italicized text. To see what values map to which parameters, this will be in the hover tooltip. Also, parameters should show up in the widget animation component when they exist. Like it should be an array of parameters.

### Done
- Track label "{N values parametrized}" (dimmed, italic) replaces "‹Distance, Side›"; tooltip rows **Parametrized** (field <- parameter = value) and **Plays When**.
- Widget Animation Component: a **Parameters** array listing the parameters of every animation it plays, shown only when there are some, set per row with Set Here; applied under each use's own values. Details in "Animation Parameters > Done in v8_fix3".

### Files
- `NameTBD/WidgetAnimation/WidgetAnimParameters.h/.cpp` - `bUnderUse` scopes, Resolve order, precedence comment.
- `NameTBD/WidgetAnimation/WidgetAnimationComponent.h/.cpp` - `ComponentParameters`, `bHasComponentParameters`, `SyncComponentParameters`, `GatherParameterDeclarations`, `FindParameterDeclaration`; PostLoad / PostEditChangeProperty / PushParameterScopes / GetWidgetAnimationParameter.
- `NameTBDEditor/Private/WidgetAnimationComponentDetails.cpp` - syncs and adds the Parameters row.
- `NameTBDEditor/Private/WidgetAnimParameterValueCustomization.cpp` - component rows find their declaration; own tooltip.
- `NameTBDEditor/Private/SWidgetAnimTimeline.cpp` - track label, `DescribeFieldPath`, tooltip rows.
- `NameTBDEditor/Public/NameTBDColors.h` - `SectionParametrizedTextOpacity`, `SectionParametrizedGap`.

### Decisions made without asking
- N counts bound fields (bindings), not distinct parameters: "Slide Distance" and "Key 2 Value" bound to one parameter is "2 values". Plays When alone shows no label (it switches the track, it isn't a value) but is in the tooltip.
- The braces stay in the text, as asked: "{2 values parametrized}"; singular "{1 value parametrized}".
- The label shows only beside a full-size title and only where it fits, like the easing; dimmed = 50% of the title color.
- The component's array is named `ComponentParameters` in C++ (shown "Parameters") so it can't clash with the `Parameters` members/params elsewhere (UHT and C4458 rules).
- Component Parameters sit **under** an entry's own Parameters and a state's Parameters From Inputs (more specific wins), and under play-time values. Unticked rows do nothing (each animation keeps its own default; animations that share a name can have different defaults).
- A parameter name declared by two animations with different types: the first found wins the row; another animation with that name gets the row through `FWidgetAnimParameterResolver::Set`, which takes it as is when the types match, converts number to number (Float / Integer / Bool), and ignores it otherwise.

### Unverified (no engine here)
- Nothing compiled. Watch for C4456/C4458 in the new code (names checked against members by grep).
- Whether `EditCondition` on a `UPROPERTY()` bool hides the array cleanly in the customized category (`EditConditionHides`, `HideEditConditionToggle`).

### Check by hand
- Bind two fields of a track; the section reads "{2 values parametrized}" in dim italics; hover: two lines with the fields, parameter names in their type colors and values; change a Preview value and hover again.
- A track with Plays When: tooltip row "Plays When".
- Widget Animation Component with an Anim In animation that has parameters: **Parameters** appears under Animations With State Machines, one row each; tick one and change it - the playing preview takes it; an entry's own ticked value still wins. Remove the animation: the array hides. A state machine whose states play animations with parameters: their parameters are listed too.

## Change set before — `NameTBD_StateMachine_v8_fix2.patch` (applies on top of `NameTBD_StateMachine_v8_fix1.patch`; code only; untested in editor)

Applies in order: v8, v8_fix1, v8_fix2 (checked with `git apply` on `f64a092`).

### Reported
- The editor crashed (EXCEPTION_ACCESS_VIOLATION, stack Slate -> UnrealEd, no symbols) on binding a Particles track's User Parameter to an Animation Parameter.
- Asked in the same patch: every parameter bind chip right-aligned, beside the row's Reset to Default button.

### Cause (most likely - the stack had no function names)
- The Selection panel's stand-in for the selected track (`UWidgetAnimPresetSelectionShadow`) is caught up with the track every frame (`SyncSelectionShadow`), and the multi-selection proxies too (`SyncLayerProxies`). Both replaced the whole layer whenever anything differed. A bind changes the track (`ParameterBindings`), so the next frame the stand-in's Type Params - a new instanced struct, a new property bag - were allocated afresh and the old memory freed, while the Details still showed the old rows until their refresh on the next tick. A row that points straight at its value - a curve's preview (`SCurveEditor`, UnrealEd), for a Curve for Floats user parameter - then painted freed memory: Slate paint -> UnrealEd -> access violation. The same could happen on any other edit of the selected track made outside the Details (moving it in the Track Editor) while such a row is shown.

### Fixed
- `WidgetAnimDetailsCopy::CopyInPlace` / `CopyLayer`: copies a layer over another field by field - equal fields left alone, an instanced struct or a property bag of the same type stepped into rather than replaced (other fields `CopySingleValue`, so a struct keeps its address). Used by `SyncSelectionShadow`, `SyncLayerProxies`, and `UpdateSelectionShadow` when it is the same track again. Rule: never replace what a Details panel is showing - copy into it in place.
- The bag bind chip's menu no longer holds the bag's `FProperty` pointers: each field is found by its path on the live track when its submenu opens, and `AddParameterFromField` finds its field by path when it runs (a bag's struct is made again whenever its layout changes).
- `GetDisplayLayer`'s per-frame copies live in a transient `UWidgetAnimPresetAsset` held by `DisplayLayersHolder` (TStrongObjectPtr), so the garbage collector sees the property bag structs they hold (a layout change could leave one held by nothing else).
- **Chips right-aligned:** `WidgetAnimTimeline::LayOutRowWithChip(Row, Name, Value, Chip, DefaultRow)` - the value column filled (`HAlign_Fill`), the value at its default width on the left (an `SBox` with the row's own min / max), the chip last, at the far right beside Reset to Default. Used by `AddBindChip` (every track field's chip) and the User Parameters row's chip (which now takes the row's default widths too).

### Files
- `SWidgetAnimTimeline.h/.cpp`.

### Unverified (no engine here)
- That the crash is the one above (no symbols); if it still crashes, the full call stack with function names (Launcher > Unreal Engine 5.7 > Options > Editor symbols for debugging, or run from Visual Studio) says where.
- `FDetailWidgetDecl::HAlign` on ValueContent filling the value column up to the Reset to Default button.

### Check by hand
- A Particles track whose system has a Curve for Floats user parameter: bind any User Parameter with the chip at the end of User Parameters (and New Parameter From This Field) - no crash; the curve row still shows its curve. Move that track in the Track Editor with it selected - no crash.
- Every bind chip in the Selection panel (Start / End Scale, Slide Distance, Overshoot, User Parameters...) at the far right of its row, in one column, next to the yellow Reset to Default arrow; the values keep their usual widths; a bound row's "Distance = 40" still reads.

## Change set before — `NameTBD_StateMachine_v8_fix1.patch` (applies on top of `NameTBD_StateMachine_v8.patch`; code only)

Applies in order: v8, v8_fix1 (checked with `git apply` on `f64a092`).

### Reported (first build of v8)
- `SWidgetAnimStateMachineEditor.cpp` (`MakeLayerMaskBadgeText`): C4334 (an error in this build) - `static_cast<int64>(1 << Bit)`: a 32-bit shift widened to 64 bits.

### Fixed
- `static_cast<int64>(1) << Bit`. Rule: shift in the width the result is used at - cast the 1, not the shifted result.
- Everything else in the build log compiled (SWidgetAnimTimeline.cpp, WidgetAnimCompileChecks.cpp, WidgetAnimScripting.cpp, the Niagara module...); the build stopped on this one error.

### Files
- `SWidgetAnimStateMachineEditor.cpp`.

### Check by hand
- The build finishes and links; then everything under v8's "Check by hand".

## Change set before — `NameTBD_StateMachine_v8.patch` (applies on top of `NameTBD_StateMachine_v7_fix3.patch`, i.e. HEAD `f64a092`; code + Python; 2 new files; untested in editor)

Checked with `git apply --check` on `f64a092` (the repo with v1 ... v7_fix3 applied and committed).

### Asked
- Every item of "Still open (state machine)" except Option B, and every item of "Still open (Animation Parameters)".

### Done
- See "StateMachine v8 additions" (state machine) and "Done in StateMachine v8" (Animation Parameters).

### Files
- New: `NameTBDEditor/Private/WidgetAnimCompileChecks.h/.cpp`.
- Runtime: `WidgetAnimStateMachine.h/.cpp`, `WidgetAnimStateMachineRuntime.h/.cpp`, `WidgetAnimPreset.h/.cpp`, `WidgetAnimBuilder.h/.cpp`, `WidgetAnimationComponent.h/.cpp`.
- Niagara: `WidgetAnimParticleCue.h/.cpp`.
- Editor: `NameTBDEditor.cpp`, `SWidgetAnimTimeline.h/.cpp`, `SWidgetAnimGraphEditor.cpp`, `SWidgetAnimStateMachineEditor.cpp`, `WidgetAnimStateMachineGraph.h/.cpp`, `WidgetAnimationComponentDetails.cpp`, `WidgetAnimPanelSource.h/.cpp`, `WidgetAnimScripting.h/.cpp`; `NameTBDColors.h`, `NameTBDTooltips.h`.
- Python: `Content/Python/widget_anim_scripting.py`.

### Decisions made without asking
- Compile warnings come from a Blueprint compiler extension on every Widget Blueprint compile (not the timelines' Widget Blueprint extension, which only Widget Blueprints with timelines or an own machine have). Warnings, never errors. Also new: a condition on an input the machine doesn't have.
- The layer-condition marks keep a held condition lit for 0.5 s, so a one-frame Entered / Exited is visible; Entered / Exited are otherwise shown as the machine reads them.
- Channels are 8 groups (5 widget channels, material parameters, Shape Box outlines, everything else) rather than one per registered type; "everything else" covers texts, designs, shapes and registered types.
- The layer Details' Affects / Targets not being written back was fixed (an existing bug found on the way).
- Fire Event rows use `FWidgetAnimStateEventInfo` as their Info pin (what the runtime calls them with).
- Paste asks Yes (add) / No (unbind) / Cancel (don't paste); the clipboard now carries the declarations.
- The script language is the existing Python API ("param" = `add_parameter`, "$Name" = a binding through `set`); no new syntax.
- Particle User Parameters get one chip on the bag's row (a submenu per field), since the bag's own rows can't carry chips. Set Particle Track Parameter keeps values per widget until cleared, AnimId filters; nearer widgets win.
- A playing state takes new parameter values live (inputs and Set Widget Animation Parameter alike), laid over its tracks one for one as entries' tracks are.

### Unverified (no engine here)
- `UBlueprintCompilerExtension::ProcessBlueprintCompiled(const FKismetCompilerContext&, const FBlueprintCompiledData&)` and `FBlueprintCompilationManager::RegisterCompilerExtension(TSubclassOf<UBlueprint>, UBlueprintCompilerExtension*)` (headers `BlueprintCompilerExtension.h`, `BlueprintCompilationManager.h`, KismetCompiler), `FKismetCompilerContext::MessageLog` / `Blueprint` access, `FCompilerResultsLog::Warning(const TCHAR*)`; whether the extension also runs on skeleton compiles (warnings would then repeat).
- UHT: a Bitflags `UENUM` with `1 << n` values and an int32 `Bitmask` UPROPERTY whose initializer names a `static constexpr` member declared after it.
- `FDetailWidgetRow` via `CustomWidget(true)` keeping a property bag's children; `FMenuBuilder::AddSubMenu` with `FNewMenuDelegate::CreateLambda`; `FMessageDialog::Open(EAppMsgType::YesNoCancel, FText)`; `TObjectKey::ResolveObjectPtr`.
- That `WidgetAnimParameters::WriteField` steps into the Particles track's bag through `TypeParams.UserParameters.<Name>` (the handoff says it does).

### Check by hand
- **Build** all modules (Editor, Niagara); UHT on `EWidgetAnimMachineLayerChannel`, the new UFUNCTIONs (`UWidgetAnimScriptAnimation`, `UWidgetAnimParticleTrackLibrary`).
- **Compile warnings:** in a machine, rename a layer's state by hand in the asset so a condition names a gone state (or delete an input a condition tests); compile a Widget Blueprint whose component plays it - a warning line per problem. Same for a timeline with a track bound to a deleted parameter.
- **Debug:** layer B "Interaction is Pressed": while previewing, the transition node shows a red dot, green while Pressed; hover - "Holds" / "Doesn't hold". An Entered condition flashes green for half a second.
- **Channels:** two Override layers on one widget, the top one Channels = Scale only: the lower layer's fade still shows. Badge "Scale". Affects / Targets now stick after editing them in the layer's Details.
- **Fire Event:** a state's On Enter Fire Event "Ready": the component's Events group lists "On <Machine> Ready"; + makes the Custom Event with an Info pin; it fires.
- **Parameters:** bind a Slide distance to Distance (default 200): the curve editor and the track tooltip read 200; tick Preview, set 40 - they follow. In the WidgetAnimationBuilder (Animation Timeline): Animation Parameters "+" works, binding and Preview play in the builder's preview, Create saves the parameters. Copy a bound track into another animation: the dialog; Yes adds the parameter as it was. Script: `anim.add_parameter("Distance", ParamType.FLOAT, 40)`, `anim.tracks[0].set(SlideDistance="$Distance")` - the chip fills. A Particles track: the chip after User Parameters, bind a Color to a Color parameter; Set Particle Track Parameter (Color) from the widget's graph changes the burst. A machine state with "Distance <- Drag": drag while it plays - the slide follows; Set Widget Animation Parameter while a state plays - it follows.

## Change set before — `NameTBD_StateMachine_v7_fix3.patch` (applies on top of `NameTBD_StateMachine_v7_fix2.patch`; code only; untested in editor)

Applies in order: v1 ... v7, v7_fix1, v7_fix2, v7_fix3 (checked with `git apply` on `d95ab56`).

### Asked
- The difference between Expand In Place and Release States (answered: Expand only shows the states in the layer again - they stay together under the name and box position, and Collapse States folds them back into the same box; Release dissolved the collapsed states for good). Release removed; Expand In Place renamed **Expand States**.
- Collapsed states lose their straightening after every restart, even when saved.

### Done
- **Menu:** collapsed states' menu: Open, Expand States, Rename, then Organize. `WidgetAnimMachineEdits::Ungroup` stays in code, unused. `GroupNodeHint` updated.
- **Restart unstraightening - two causes fixed:**
  - `CommitGraphEdits` wrote back only states' positions: a collapsed-states box dragged by hand never reached the machine (`FWidgetAnimMachineStateGroup::Position`), so it went back the next time the graph was made - a restart, an undo, any edit. Boxes are written back now, as states are (`MoveStates` already took group ids).
  - The node size keeper (`SWidgetAnimMachineStateNode::Tick`) acted on the first size measured: in a node's first frames (editor just opened, graph just made again) that can be on the way to its real size, and the half-difference shift then stuck. It now acts only on a size that has held two frames in a row (`LastSeenSize`).

### Files
- `SWidgetAnimStateMachineEditor.cpp`, `WidgetAnimStateMachineGraph.cpp`; `NameTBDTooltips.h`.

### Check by hand
- Right-click collapsed states: Open, Expand States, Rename, Organize - no Release States.
- Line collapsed states up (Q, or drag the box), save, restart the editor, open the state machine: still lined up. Drag a box, make any other edit: the box stays where it was dragged.

## Change set before — `NameTBD_StateMachine_v7_fix2.patch` (applies on top of `NameTBD_StateMachine_v7_fix1.patch`; code only; untested in editor)

Applies in order: v1 ... v6, v7, v7_fix1, v7_fix2 (checked with `git apply` on `d95ab56`).

### Reported
- v7_fix1's ring still completely wrong (white band, orange-filled node). Wanted: the old border back, and another fix for the seam.

### Done
- **Rounded boxes dropped entirely.** `SWidgetAnimMachineActiveRing` draws exactly as before v7 - each line one antialiased closed path (`MakeLines`), same offsets, thicknesses and colors - and so does the play / pause outline (`SWidgetAnimMachineRoundedOutline`). The `bMachineRingRoundedBoxes` setting is gone.
- **The seam, another way:** a closed antialiased path dims where its two ends meet. Each path now starts at the middle of the bottom edge (where it shows least) and runs on past its start along the bottom edge, so the ends overlap instead of meeting. How far: **DEVELOPER ONLY > State Machine Ring Seam Overlap** (`UWidgetAnimEditorSettings::MachineRingSeamOverlap`, pixels, default 1, 0 - 6; 0 = as before). Kept within the straight part of the edge.
- Rule: never draw these outlines with a rounded box brush - a rounded box's fill takes the draw color and its outline color is the brush's (premultiplied); neither gives a tinted, translucent outline.

### Files
- `SWidgetAnimStateMachineEditor.cpp`, `WidgetAnimStateMachineGraph.cpp`, `WidgetAnimEditorSettings.h`.

### Check by hand
- The state in play: exactly the pre-v7 look (orange line, fading glow, dark body), no crack at the top. At the middle of the bottom edge: no dark gap - if a faint brighter tick shows, lower State Machine Ring Seam Overlap; if a dark gap shows, raise it.
- Play / pause: the thin gray outline as before, no gap.

## Change set before — `NameTBD_StateMachine_v7_fix1.patch` (applies on top of `NameTBD_StateMachine_v7.patch`; code only; untested in editor)

Applies in order: v1 ... v6, v7, v7_fix1 (checked with `git apply` on `d95ab56`).

### Reported
- v7's ring on the state in play: a thick, pale, solid band instead of the orange line and fading glow.

### Cause and fix
- v7 set each line's color as the brush's outline color. A brush's outline color is taken as already multiplied by its opacity, so the faint halo lines were added at full strength and eight of them piled up into a pale solid band. Now the brush's outline is plain white and each line's color is the color the box is drawn with (`MakeBox` tint), which carries its opacity properly - in `SWidgetAnimMachineActiveRing` and `SWidgetAnimMachineRoundedOutline` (play / pause) alike.
- Fallback, live: **DEVELOPER ONLY > State Machine Ring As Rounded Boxes** (`UWidgetAnimEditorSettings::bMachineRingRoundedBoxes`, on). Off, the ring is drawn as before v7 - antialiased closed lines (the right look, with the hairline where each line's ends meet).
- Rule: a rounded box brush's `OutlineSettings.Color` is premultiplied - give it white and color the box with the draw tint.

### Files
- `SWidgetAnimStateMachineEditor.cpp`, `WidgetAnimStateMachineGraph.cpp`, `WidgetAnimEditorSettings.h`.

### Check by hand
- The state in play: an orange line on its edge and a glow fading out - as before v7 - with no crack. If it is wrong, untick State Machine Ring As Rounded Boxes and say what each looks like.
- Play / pause: a thin gray outline.

## Change set before — `NameTBD_StateMachine_v7.patch` (applies on top of `NameTBD_StateMachine_v6.patch`; code only; untested in editor)

Applies in order: v1, v1_fix1, v1_fix2, v2, v2_fix1, v3, v4, v4_fix1, v5, v6, v7 (checked with `git apply` on `d95ab56`).

### Asked
- The hairline on the state in play is still there (v6 moved it to the middle of the top edge) - gone entirely; the same crack on the play / pause button's outline.
- States and collapsed states renamed from their right-click menu, as tracks are; collapsed states had no way to be renamed.

### Done
- **No seams:** the crack was not the angle where the path's ends met but the ends themselves - an antialiased closed path fades out at both of its ends, so wherever it starts there is a hairline. Both outlines are now drawn by the rounded-box shader, as the node's own edge is - one seamless shape:
  - `SWidgetAnimMachineActiveRing`: each line (halo steps, selection outline, rim) is an `FSlateBrush` with `DrawAs = RoundedBox`, transparent fill, `OutlineSettings` (fixed radius = node radius + offset + half the thickness, the line's color, its thickness), drawn on the node's box grown by that much. One brush per line, kept as a member (`LineBrushes[16]`) - a drawn box refers to its brush until the frame is rendered.
  - `SWidgetAnimMachineRoundedOutline` (play / pause): the same, one member brush, color from the DEVELOPER ONLY setting.
- **Rename from the menu:** a **Rename** submenu with a name box (Enter renames and closes the menu; Escape / clicking away leaves it), as the Widget Animations panel renames in menus, in place of the generic Rename command: on a state (`WidgetAnimMachineEdits::RenameState`) and on collapsed states (`RenameGroup`). Read-only while the editor's lock is on (`UWidgetAnimMachineGraph::CanEditMachine`, set by the editor). Tooltips `RenameStateTooltip`, `RenameCollapsedStatesTooltip`. F2 / double-click on the name still rename in place.

### Files
- `SWidgetAnimStateMachineEditor.cpp`, `WidgetAnimStateMachineGraph.h/.cpp`; `NameTBDTooltips.h`.

### Unverified (no engine here)
- That a rounded box's outline takes its color's alpha from `OutlineSettings.Color` (the halo's faint lines depend on it), and that an outline-only rounded box (transparent fill) isn't culled; `FToolMenuSection::AddSubMenu` with an `FNewMenuDelegate`.

### Check by hand
- The state in play, selected and not, hovered: a smooth ring and glow, no crack anywhere; the glow still fades out.
- Play / pause: a gray outline with no crack; the DEVELOPER ONLY color still changes it.
- Right-click a state > Rename: type, Enter - renamed. The same on collapsed states. Locked: the box is read-only.

## Change set before — `NameTBD_StateMachine_v6.patch` (applies on top of `NameTBD_StateMachine_v5.patch`; code only, 1 new header; untested in editor)

Applies in order: v1, v1_fix1, v1_fix2, v2, v2_fix1, v3, v4, v4_fix1, v5, v6 (checked with `git apply` on `d95ab56`).

### Asked
- A thin vertical line in the top-right corner of the state in play (across its glow).
- Play / pause outline gray instead of bright white, and settable under DEVELOPER ONLY.
- Renaming a name by double-click: the text was cut off at the right while edited. The same double-click rename for the Widget Animations panel's name in Animation Timeline mode (a builder's animation name).
- The collapsed states' breadcrumb in the layer row, as the Widget Animations panel has its collapsed-track trail in its header.
- Collapsing states should straighten the new box automatically.
- Right-click menu: Node Comment last, Organize just above it. With several states selected, a selection menu: no Jump To State, Open Animation, Rename, Set As Entry State, Node Comment, or Transitions On This Wire.
- (Added before v6 was applied - folded into it) No white trim round the graph while collapsed states are open.

### Done
- **Ring seam** (`SWidgetAnimMachineActiveRing::OnPaint`): each line is one closed antialiased path; it started (and ended) where the top edge meets the top-right corner's arc, so its first and last pieces met at an angle and their square ends left a hairline wedge through the line and every halo line - the thin dark line. The path now starts and ends at the middle of the top edge, where both ends run in line.
- **Play / pause outline:** a rounded box brush's outline color can't be tinted (which is why v5's faint white tint showed as bright white), so the outline is drawn by `SWidgetAnimMachineRoundedOutline` (a small leaf widget: closed antialiased path, started mid-edge, in any color, read live). Its color: **Editor Preferences > Widget Animations Editor > DEVELOPER ONLY > State Machine Play / Pause Outline** (`UWidgetAnimEditorSettings::MachinePlayPauseOutline`), starting from `MachineColors::FloatingButtonOutline`, now gray (0.13, 0.13, 0.14).
- **Name fields:** new `WidgetAnimNameField.h` - `SWidgetAnimNameField`: the name as text; double-click swaps it for an `SEditableTextBox` holding the name, all selected, sized to its text (Enter / clicking away commits; Escape puts it back). Replaces `SInlineEditableTextBlock` (its text box is held to the width of the text it replaces - the cut-off) in: the State Machine name field, the timeline name field (and the timeline field lost its 240 max width), and - new - the builder's name in Animation Timeline mode: an input-colored field showing Animation Name; a rename sets the builder Class Defaults' `AnimationName` as a Details edit would (transaction, `Modify`, `PostEditChangeProperty`), tooltip `BuilderAnimationNameTooltip`.
- **Breadcrumb:** `MakeGroupBreadcrumb` (the old overlay, reading the shown graph live) sits in the layer row after the layer tabs, no dark backing; the layer tabs no longer fill the row (a spacer does). The graph overlay keeps only the vignette and play / pause.
- **Straighten on collapse:** the menu's Collapse States (new box, or folding expanded ones back) calls `UWidgetAnimMachineGraph::OnCollapsed(GroupId)`; the editor selects the box when the graph is made again (`PendingSelection`) and three frames later, laid out, runs `StraightenSelected` (`PendingStraightenFrames`) - the box alone: lined up with what it shares the most transitions with, slanted wires drawn straight.
- **No trim:** `SWidgetAnimMachineScopeVignette` no longer draws the 1px `ScopeFrame` lines round the graph - only its darkened edges. (The Track Editor's collapsed-track frame is unchanged.)
- **Node menu** (`UWidgetAnimMachineGraphNode::GetNodeContextMenuActions`): several states selected -> one "Selected States" section (Collapse States, Copy, Cut, Duplicate, Delete) then Organize; nothing else (no wire section either). One state: State section, wires, Organize, then Node Comment last. Collapsed states' box: its section, then Organize.

### Files
- `SWidgetAnimStateMachineEditor.h/.cpp`, `SWidgetAnimTimeline.cpp`, `WidgetAnimStateMachineGraph.h/.cpp`, `WidgetAnimEditorSettings.h/.cpp`, `WidgetAnimNameField.h` (new); `NameTBDColors.h`, `NameTBDTooltips.h`.

### Decisions made without asking
- The outline's default gray: (0.13, 0.13, 0.14), opaque, 1px.
- The multi-select menu keeps Collapse States, Copy, Cut, Duplicate, Delete and Organize.
- The straighten after collapsing waits three frames for the box's size.

### Unverified (no engine here)
- `SWidgetSwitcher` slot `VAlign`, `SEditableTextBox::MinDesiredWidth`; that a builder Class Defaults rename survives a compile (it is set the way its Details set it).

### Check by hand
- The state in play: no dark hairline in the top-right corner of its glow.
- Play / pause: a gray outline; changing the DEVELOPER ONLY color changes it live.
- Double-click the State Machine name, a timeline's name, and (in a Widget Animation Builder, Animation Timeline mode) the animation name: the whole name in the text box, nothing cut off; Enter renames, Escape doesn't.
- Open collapsed states: the trail "< Layer > [icon] Name" in the layer row next to the tabs; clicking the layer goes back. No light line round the graph, only the darkened edges.
- Collapse two states: the box appears selected and lines up with its partner a moment later.
- Right-click one state: ... Organize, then Node Comment last. Select two, right-click: Selected States (Collapse States, Copy, Cut, Duplicate, Delete) and Organize only.

## Change set before — `NameTBD_StateMachine_v5.patch` (applies on top of `NameTBD_StateMachine_v4_fix1.patch`; code only; untested in editor)

Applies in order: v1, v1_fix1, v1_fix2, v2, v2_fix1, v3, v4, v4_fix1, v5 (checked with `git apply` on `d95ab56`).

### Asked
- Entry / Exit / Any State: the name still not vertically centered (v4's fix wasn't enough).
- Play / pause: a slight outline round its dark box - it blends into the graph.
- "Collapsed Group" called **Collapsed States** everywhere; the node's name at a state's normal size; under it just "{N} states".
- Undo / redo broke straightening: remove a state's animation, undo - the node no longer lines up.
- The collapsed track's vignette (Widget Animations panel) a bit lighter, still stronger than the graph's.
- State Machine mode: the machine has a name - a default, double-click to rename - also as a field under State Machine in the Details. The same double-click rename for a timeline's name in the Widget Animations panel.

### Done
- **Centered names:** measured on the screenshots: the capitals sat ~3 graph units above the box's middle (the title line's descender space and drop shadow are all below them). Entry / Exit / Any State's title row is drawn `SpecialNodeTitleDrop` (3) lower, with the same negative bottom padding, so the box keeps its size. States (name + subtitle) unchanged.
- **Play / pause:** an `SOverlay` - the dark box, then `RoundedOutlineBrush(6)` (1.5px) in `FloatingButtonOutline` (white, 0.16) over it, hit-test invisible.
- **Collapsed States:** every UI word - menu "Collapse States", section "Collapsed States" with Open / Expand In Place / Release States / Rename, tooltip kind "Collapsed States", undo step names, the "can't join" message, tooltips. New ones are named "Collapsed States" ("Collapsed States 2"...) instead of "<first state> Group". Code names (`EditorGroups`, `bIsGroup`, `FWidgetAnimMachineStateGroup`...) unchanged. The name: `GetGroupTitleStyle` takes the state title's own font (`Graph.StateNode.NodeTitleInlineEditableText`) in its italic, `CollapsedNodeText`, the tracks' drop shadow. Subtitle "{N} states" (plural rule: "1 state").
- **Undo / redo and node sizes:** the cause - a node keeps its middle as its size changes by moving quietly (no undo step); the sizes it compared against were in memory (`KnownNodeSizes`), so after an undo, which brings back the position from before the edit, the node was shifted again from that. Now the size goes with the position in the machine: `FWidgetAnimMachineState::EditorSize` / `EditorSizeZoom` (and the same on `FWidgetAnimMachineStateGroup`), editor-only UPROPERTYs written quietly (`WidgetAnimMachineEdits::SetNodeSizeQuietly`, not dirtying), read with `GetNodeSize`. `RebuildFromMachine` empties `KnownNodeSizes`; a node made again starts from the stored size if it was stored at the current zoom. Undo restores position + size together (no shift); redo restores the state as it was at the undo (already shifted, matching size). The state proxy's Details write keeps `EditorSize` / `EditorSizeZoom` as it keeps the position.
- **Vignettes:** `ScopeVignette` (Track Editor) 0.38 -> 0.30; the graph has its own `GraphScopeVignette` 0.24 (it used `ScopeVignette`), so the Track Editor's always stays the stronger.
- **State Machine's name:** `UWidgetAnimTimelinesExtension::StateMachineName` (UPROPERTY, empty = default "<Widget Blueprint without WBP_> State Machine"). On the bar in State Machine mode, after the mode button: a field (`MakeDraftNameField`: input-colored rounded box, bold name, `SInlineEditableTextBlock`) - double-click to rename; read-only while locked. In the Details (layer view), State Machine category: a **Name** row (`SEditableTextBox`) above Description. Both go through `WidgetAnimDraftMachineName::Get` / `Set` (one undo step "Rename State Machine"; typing the default or empty keeps the default). Save State Machine To names the asset `WASM_` + the name's letters and digits ("Card Hover" -> `WASM_CardHover`); unrenamed, as before (`WASM_Card`).
- **Timeline name** (`MakeTimelinePicker`): no longer one combo button. An input-colored field: the timeline icon, the name (`SInlineEditableTextBlock`, double-click to rename -> `RenameTimeline`; read-only with no timeline open), and a small arrow combo (`SimpleComboButton`) at its right for the list / New / Rename / Duplicate / Delete menu.

### Files
- `WidgetAnimStateMachine.h`, `WidgetAnimTimelines.h`; `NameTBDColors.h`, `NameTBDTooltips.h`; `SWidgetAnimStateMachineEditor.h/.cpp`, `SWidgetAnimTimeline.cpp`, `WidgetAnimStateMachineGraph.h/.cpp`.

### Decisions made without asking
- The vertical correction is a measured constant (3 graph units, `SpecialNodeTitleDrop`), not computed from font metrics.
- The graph's vignette was made lighter (0.24) rather than leaving it equal to the Track Editor's, to keep "the collapsed track's is stronger" true.
- Default machine name "<Widget Blueprint> State Machine"; the Details row only appears for the Widget Blueprint's own machine (an asset is named by its asset name).
- The timeline picker's single click no longer opens the list: a single click on a field that renames on double-click would open and close the menu; the list is on the arrow.
- Existing collapsed states keep their names; only new ones are called "Collapsed States".
- Node sizes are stored per zoom; after zooming, an undo of an edit made at another zoom doesn't correct the size change (rare; one Q fixes it).

### Unverified (no engine here)
- That a transient-free editor-only UPROPERTY written outside a transaction comes back with the snapshot on undo (it should: the whole struct is recorded at Modify); `SInlineEditableTextBlock::EnterEditingMode` from the field's double-click; `SimpleComboButton` with no content showing just the arrow; plural format `{0}|plural(one=...,other=...)` with an int argument; `FStyleColors::Input`.

### Check by hand
- Entry / Exit / Any State: capitals centered top to bottom.
- Play / pause: a faint light outline round its box.
- Right-click a state: Collapse States; the box reads "Collapsed States", at a state's name size, italic beige, "2 states" under it; its menu: Open, Expand In Place, Release States, Rename.
- Line states up with Q; remove an animation from one (it shrinks, stays centered); Ctrl+Z - it lands exactly where it was, wires straight; Ctrl+Y - same, shrunk. Same for adding a blend sample, renaming to a longer name.
- Collapsed track open in the Widget Animations panel: a bit lighter than before; open collapsed states in the graph: lighter still.
- State Machine mode: the name field after the mode button; double-click, type, Enter - renamed; Ctrl+Z undoes it; the Details (click empty graph) show Name above Description; Save State Machine To proposes WASM_<name>.
- Widget Animations panel, Animation Timeline: double-click the timeline's name to rename it; the arrow opens the list.

## Change set before — `NameTBD_StateMachine_v4_fix1.patch` (applies on top of `NameTBD_StateMachine_v4.patch`; code only)

Applies in order: v1, v1_fix1, v1_fix2, v2, v2_fix1, v3, v4, v4_fix1 (checked with `git apply` on `d95ab56`).

### Reported (first build of v4)
- `WidgetAnimStateMachineGraph.cpp`: C2665 on the transition node menu's Straight Wire entry - `FToolMenuSection::AddMenuEntry` takes the action type right after the action (no `FName` extension hook before it, unlike `FMenuBuilder::AddMenuEntry`).

### Fixed
- The `NAME_None` argument removed; the entry is `(Name, Label, ToolTip, Icon, FUIAction, EUserInterfaceActionType::ToggleButton)`.
- Rule: `FToolMenuSection::AddMenuEntry(Name, Label, ToolTip, Icon, Action, ActionType, TutorialHighlightName)`; `FMenuBuilder::AddMenuEntry(Label, ToolTip, Icon, Action, ExtensionHook, ActionType)`.

### Files
- `WidgetAnimStateMachineGraph.cpp`.

### Check by hand
- The build gets past `WidgetAnimStateMachineGraph.cpp`; then everything under v4's "Check by hand".

## Change set before — `NameTBD_StateMachine_v4.patch` (applies on top of `NameTBD_StateMachine_v3.patch`; code only; untested in editor)

Applies in order: v1, v1_fix1, v1_fix2, v2, v2_fix1, v3, v4 (checked with `git apply` on `d95ab56`).

### Asked
- Left panel: the dark gray across the whole panel, edge to edge; the row selection and the category strips full width too; more space between categories; brighter category text.
- A collapsed group's name: much smaller - exactly the collapsed track's text (the v3 "italic, no shadow" request was a little off).
- Entry / Exit / Any State: the name not vertically centered.
- Widget Animations header: Seconds / Percent and Fit at the start of the button group on the right (not the far left of the header).
- Play / pause: first "the middle of the layer row", then changed to: floating at the graph's bottom left on a dark backing, Pause's bars in the color Play's triangle had.
- Jump To Active State: the state's name (in parentheses) colored and bold.
- Ctrl+Shift+Z (redo) in the state machine editor.
- Q does nothing to a collapsed group.
- A diagonal wire (Hovered -> Exit) couldn't be made perfectly straight without Q pulling the state out of line with the rest of the layout.

### Done
- **Left panel:** the panel's own border is `Brushes.Recessed` with no padding; sections' header strips and rows run edge to edge (`MakePanelSection` rows padded 0 sideways); every row pads itself by `RowPadding` (8, 3). Input rows and category strips use a square `WhiteBrush` (`GetRowBrush`) - the selection (`InputRowSelected`) fills the row's whole width, as in My Blueprint. Category strips: 8 above / 2 below (was 4 / 1), white bold name. Section headings in `FStyleColors::ForegroundHover` (were subdued), 10 between sections (was 6). Preview rows are inset by the same padding through an `SBox`.
- **Group title** (`GetGroupTitleStyle`): the font of `Graph.Node.NodeTitle` (what the timeline's tracks are drawn in, `SectionFont` / `LayerFont`) in its italic typeface when the family has one (the timeline's `ItalicOf` rule), `CollapsedNodeText`, with the tracks' drop shadow (`TrackTextShadow`, offset `TrackTextShadowOffset`).
- **Centered names:** the blend plot under a node's name had a 3px top gap even when empty, pushing the name up; it is now collapsed unless the state is a blend state with samples.
- **Widget Animations header:** `RightGroupIndex` is taken just before the spacer; Time In is inserted there and Fit right after - so: [spacer][Seconds][Fit][Save...][Dock][Lock].
- **Play / pause** (`MakePlayPauseButton`): out of the bar; an overlay slot over the graph (bottom left, 10 in), in a rounded box of `MachineColors::FloatingButtonBacking` (new, near black, 0.88). Both icons in `PlayTint`.
- **Jump To Active State:** "Jump to Active State" then "(Name)" in `NormalFontBold`, `MachineColors::ActiveRing` (the active glow's orange). `GetJumpToActiveLabel` became `GetJumpToActiveStateName`.
- **Undo / redo:** `OnPreviewKeyDown` takes Ctrl/Cmd+Z (undo), Ctrl/Cmd+Y and Ctrl/Cmd+Shift+Z (redo) anywhere in the editor except while a text box is typed in, as the Widget Animations panel does (`GEditor->UndoTransaction` / `RedoTransaction`, not mid-transaction).
- **Q on groups:** `StraightenSelected` maps every transition's ends through `GetShownNodeId` (a state in a collapsed group = the group's box), so groups have links, partners and routed wires like states; wires inside one box are skipped. One transaction for the whole Q.
- **Diagonals without moving anything:** Q no longer moves a state onto the 45-degree line. Pairs on a slant (`WidgetAnimMachineWires::IsSlanted`: corner to corner, 25 - 65 degrees) stay where they are, and their plain wires get **Straight Wire** - drawn as an exact straight line from corner to corner at whatever angle (`Compute(..., bStraight)` puts both tangents along the chord). Wires Q leaves level get it cleared. With one state selected, all of that state's own wires are covered.
  - Data: `FWidgetAnimMachineTransition::bEditorStraight` (editor-only UPROPERTY, default false), mirrored on `UWidgetAnimMachineTransitionNode::bStraight`; `WidgetAnimMachineEdits::SetTransitionsStraight`.
  - The transition node menu has a **Straight Wire** toggle (plain wires only: not dragged, no reroute points), tooltip `StraightWireTooltip`; `StraightenTooltip` updated.

### Files
- `WidgetAnimStateMachine.h`; `NameTBDColors.h`, `NameTBDTooltips.h`; `SWidgetAnimStateMachineEditor.h/.cpp`, `SWidgetAnimTimeline.cpp`, `WidgetAnimStateMachineGraph.h/.cpp`.

### Decisions made without asking
- The play / pause request changed mid-task; the last one wins: the play / pause button floats at the bottom left of the graph, not in the layer row.
- Diagonal straightening draws the wire straight where the states are, rather than moving either one - nothing else's alignment can break. A wire can be set or cleared by hand from its menu.
- The collapsed track has a drop shadow, so the group name has one too (v3 had removed it on request).
- The state's name on Jump To Active State uses the active glow's orange.
- Redo is also Ctrl+Y, as everywhere else in the editor.

### Unverified (no engine here)
- `FEditableTextBoxStyle::TextStyle`, `FTypefaceEntry` / `FCompositeFont` from `Fonts/CompositeFont.h` in the graph module; `Visibility_Lambda` on the blend plot's args; `SHorizontalBox::NumSlots`; that an empty `FScopedTransaction` (Q that changes nothing) leaves no undo step.

### Check by hand
- Left panel: one dark gray from edge to edge; a selected input's highlight spans the panel; category strips full width with space above; headings bright.
- A collapsed group's name: small, italic, beige, with a slight shadow - side by side with a collapsed track it should match.
- Entry / Exit / Any State: the name centered top to bottom with the icon.
- Widget Animations header: [... spacer][Seconds][Fit][Save][...][Lock].
- Play / pause at the graph's bottom left on its dark box, orange in both states; works as before (not in PIE).
- Jump To Active State (while previewing): "(Hovered)" bold orange.
- Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z in the graph, after moving a state; typing a name still undoes text, not the graph.
- Q with a collapsed group selected (alone, and with a state): it lines up with the state it's wired to.
- The Idle / Hovered / Exit layout of the screenshot: select all three, Q - Idle / Exit level, Hovered under Idle, Hovered -> Exit dead straight without Exit moving. Right-click the wire's node: Straight Wire ticked; untick it, it curves again.

## Change set before — `NameTBD_StateMachine_v3.patch` (applies on top of `NameTBD_StateMachine_v2_fix1.patch`; code only; untested in editor)

Applies in order: v1, v1_fix1, v1_fix2, v2, v2_fix1, v3 (checked with `git apply` on `d95ab56`).

### Asked
- Only collapsed groups should have the collapsed look (v2 gave it to every state); their icon changed; their name italic in the collapsed track's color with no drop shadow.
- The Hold button: legible text, a dimmer fill, no bottom line, an idle look only slightly different from Fire.
- Straightening: cases where Q doesn't straighten when it obviously should; corner-to-corner wires not perfectly straight.
- State machine bar: no "Previewing On" text; no New Machine In; Save right-aligned with a lock beside it (as the Widget Animations panel's lock); Jump To Active State beside Add State; play / pause centered, in the Widget Animations panel's play / pause colors.
- Inputs: the type text gray, and the value read-only with a tooltip, while the machine doesn't read the input; "+" the same Add button as Add Track; layer "+" in that green (look unchanged); the layer trash can Delete Track's red; the layer tabs in one border (a segmented control); a background behind the left panel's sections, as My Blueprint's Variables have; Targets / Components checked in a dimmed animation orange; the Plays On / Layer Group rows' pickers narrower.
- A state's Details: State Properties {Name, Plays, Playback, Playback Speed, Comment, Node Color}, and everything that is played in its own category.
- Widget Animations panel: the lock farthest right, the Seconds / Percent / Frames picker farthest left, Fit right of it; icons in the Animation Timeline / Edit Existing Asset menu.

### Done
- **Collapsed look:** `SWidgetAnimMachineCollapsedBody::Construct` set its own visibility, overriding the "groups only" one given it - so every state got it. Gone; groups only. A group's icon: `GraphEditor.SubGraph_16x`; its name: the state title style made italic, `CollapsedNodeText`, no shadow (`GetGroupTitleStyle`).
- **Hold button** (`SWidgetAnimHoldInputButton`): a real `SButton`, as Fire is, with SButton's own ButtonText; the label tinted 35% toward the hold color (the only idle difference); the fill 42% of the hold color over the button's dark, inside its edge (2px), under the label. No bottom track.
- **Straightening:**
  - Wires (`WidgetAnimMachineWires::Compute`): when both ends already point along the line between them within `StraightSnapDegrees` (8), the wire is drawn exactly straight - corner to corner included - instead of a Hermite curve bowing by a pixel's rounding.
  - Q (`StraightenSelected`): transition nodes dragged somewhere of their own on wires between the selected states go back onto their wires first (a dragged node bent the wire whatever the states did); a pair is lined up on the diagonal only when it really lies on a slant (25 - 65 degrees), otherwise level across or above - a corner wire between nearly level states no longer drags one to 45 degrees.
- **Bar** (`MakeToolbar`): on the Widget Animations panel's header strip (`Brushes.Header`, 28 high); left: State Machine / Edit Existing Asset and the asset picker; centered: play / pause in `PlayTint` (Play) / `PauseTint` (Pause); right: the PIE debug picker, Dock in Layout, the lock, Save (and Save State Machine To). Status text and New Machine In removed. Jump To Active State (`MakeJumpToActiveButton`) beside Add State.
- **Lock** (`bMachineLocked`, `IsMachineEditable`): the graph read-only (`SGraphEditor::IsEditable`), Details read-only (`SetIsPropertyEditingEnabledDelegate`), no Add input / Add State / layer add-delete-rename / input rename-menu, Delete / Rename / Cut / Duplicate / Paste off; the preview's inputs still work. Not saved.
- **Inputs panel:** sections (`MakePanelSection`): the heading in small capitals on the header strip with its button at the right, the rows on `Brushes.Recessed` - Preview, Inputs and Widget Inputs alike. Inputs "+": `MakeTypePickerButton(..., bAsAddButton)` = the Add Track button (plain combo, accent-green plus, bold Add) opening the type picker. Unread inputs: the type name gray (`MakePill(Kind, bMuted)`), the value disabled with `UnreadInputValueTooltip`. Layer tabs: in one dark rounded box, the shown one lighter (`Segments`); "+" `FStyleColors::AccentGreen`; trash `MenuDeleteIconTint`. Preview toggles: `GetPreviewSourceToggleStyle` (ToggleButtonCheckbox, checked brushes tinted 78% animation orange). Target rows: the name fills, the picker 140 wide.
- **State Details:** `FWidgetAnimMachineStateDetails` puts each of the state's fields into the category it names, in the order State Properties, Play, Actions (`FWidgetAnimMachineState`'s `Category` metas changed: "State Properties" / "Play").
- **Widget Animations panel header:** Seconds / Percent / Frames inserted first, Fit second (`InsertSlot`), so the lock is last; the mode menu's entries have the button's icons.

### Files
- `WidgetAnimStateMachine.h`; `NameTBDTooltips.h`; `SWidgetAnimStateMachineEditor.h/.cpp`, `SWidgetAnimTimeline.cpp`, `WidgetAnimStateMachineGraph.h/.cpp`, `WidgetAnimTypePill.h/.cpp`.

### Decisions made without asking
- The played category is called "Play".
- The lock isn't remembered across sessions; locked, the preview inputs stay usable (as the animation lock still plays).
- The Hold button's idle difference is its tinted label only.
- Straight-snap at 8 degrees; diagonal straightening only between 25 and 65 degrees.

### Unverified (no engine here)
- `FCoreStyle` "ButtonText", `Brushes.Recessed` / `Brushes.Header`, `SmallFontBold`; `FCheckBoxStyle` brush tint edits; `SHorizontalBox::InsertSlot`; `SGraphEditor` `IsEditable_Lambda`; `IDetailsView::SetIsPropertyEditingEnabledDelegate`.

### Check by hand
- States look as before; only a collapsed group has the dark body, the subgraph icon and the italic beige name.
- Hold: legible label, dim fill to the edge, nothing at the bottom; next to Fire it reads as the same kind of button.
- Q: two states nearly level with a dragged transition node between them line up straight; a 45-degree pair: perfectly straight wire; nearly level pair with a corner wire: lined up level.
- Bar: play / pause in the middle (orange / dim orange), Save at the right with the lock; lock: graph, Details and inputs read-only, preview inputs still work.
- Left panel: section strips and backgrounds; Add button; unread inputs gray and read-only with the tooltip; layer tabs' box, green +, red trash; orange toggles; long Layer Group names readable.
- A state's Details: State Properties, Play, Actions.
- Widget Animations header: Seconds first, Fit, ..., lock last; the mode menu's icons.

## Change set before — `NameTBD_StateMachine_v2_fix1.patch` (applies on top of `NameTBD_StateMachine_v2.patch`; code only)

### Reported (first build of v2)
- `SWidgetAnimTimeline.cpp` (`CustomizeParameters`, the "+" type picker's tooltips): C2039 `GetToolTipTextByValue` is not a member of `UEnum` (the C2664 / C2088 after it follow from it).

### Fixed
- `UEnum` has no tooltip by value: `GetToolTipTextByIndex(GetIndexByValue(Value))`. Rule for later: enum tooltips go by index.

### Files
- `SWidgetAnimTimeline.cpp`.

### Check by hand
- The build finishes; the Parameters "+" picker shows each type's tooltip.

## Change set before — `NameTBD_StateMachine_v2.patch` (applies on top of `NameTBD_StateMachine_v1_fix2.patch`; code + 1 SVG (2 changed); untested in editor)

Applies in order: v1, v1_fix1, v1_fix2, v2 (checked with `git apply` on `d95ab56`).

### Asked
- Everything listed under "Current limits in v1" of Animation Parameters.
- Bind chip: the type's exact color; filled (no hole) when bound; different when mirrored.
- State machine: a UE-style type picker for "+" inputs; wires re-straightened wherever a node's size changes; collapsed group boxes like collapsed graph nodes (as the collapsed track), the vignette while one is open, a breadcrumb trail instead of the back button; input categories; no Preview In Designer; Random Variations with chances (adding up to 1 at most); Transitions On This Wire with a visual enabled / disabled, under the State section; no hint text in Node Comment; Blueprint-style dark comment bubbles.
- Reconcile the two editors: the standard save icon; Save / Save ... To split, as floating buttons, in both; the state machine's three panels to the top with its bar over the graph alone; a {State Machine, Edit Existing Asset} choice with icons, the State Machine kept per Widget Blueprint until saved as an asset (as Animation Timeline); no "State Machine" label; a search box on the track Selection panel; consistent look; names editable from right-click and the Selection panel in both.
- Extra: keyframes over the start / end lines; Hold button different from Fire untouched, and its progress fitting it; "Widget" -> "Widget Inputs"; unread inputs at the bottom.

### Done
- See "Done in StateMachine v2" (Animation Parameters) and "StateMachine v2 additions" (state machine), and:
- **Reconciled with the Widget Animations panel:**
  - Panels: Inputs | (bar, layer tabs, graph) | Details, the three to the top; the bar (`MakeToolbar`) over the graph only, on the panel without a box of its own.
  - **State Machine / Edit Existing Asset** (in a Widget Blueprint): State Machine edits the Widget Blueprint's own machine - `UWidgetAnimTimelinesExtension::StateMachine` (NameTBDUncooked, beside the timelines), made the first time the mode is picked (one undo step), saved with the Widget Blueprint, editor data only. `SavedStateMachine`: the asset it was last saved as. Save State Machine To (Default Folder / Custom Location, named WASM_<Blueprint name without WBP_>) makes an asset of it (`SaveDraftTo`: factory-made asset, then every property copied - `CopyMachineInto`); after that Save puts it there again (`SaveDraftToSavedAsset`) with Save State Machine To beside it. Edit Existing Asset: the asset picker, New Machine In and Save, as before. Icons: GraphEditor.StateMachine_16x / ClassIcon.DataAsset. The edited machine (own or asset) is remembered per Widget Blueprint as before (an object path inside the Widget Blueprint works too); opening the editor never makes the own machine.
  - Save buttons in both editors: `SimpleButton` (floating), the editor's `Icons.Save`.
  - "State Machine" label and Preview In Designer removed (the preview is always on; pause is the switch).
  - Widget Animations panel: the Selection panel has a search box (`bAllowSearch`), and a track's **Name** row at the top of Track Properties (`RenameLayer`, as right-click Rename). State machine: layer tabs got right-click Rename / Delete (states, inputs and layers are now named both ways).
- **Track Editor:** the start / end lines run under the keys and track names (`RangeLineLayer`), their triangles still over the ruler.

### Files
- New: `Resources/WidgetAnim_ParameterMirrored.svg`. Changed SVGs: `WidgetAnim_ParameterBind.svg`, `WidgetAnim_ParameterBound.svg` (white, tinted by type; the bound one solid).
- Runtime: `WidgetAnimationComponent.h/.cpp`, `WidgetAnimPreset.h/.cpp`, `WidgetAnimStateMachine.h/.cpp`, `WidgetAnimStateMachineRuntime.cpp`.
- MVVM: `WidgetDataBindingTypes.h`, `WidgetDataBinderComponent.h/.cpp`. Uncooked: `WidgetAnimTimelines.h`.
- Editor: `NameTBDStyle.h/.cpp`, `NameTBDTooltips.h`, `SWidgetAnimStateMachineEditor.h/.cpp`, `SWidgetAnimTimeline.h/.cpp`, `WidgetAnimPanelSource.h/.cpp`, `WidgetAnimStateMachineGraph.h/.cpp`, `WidgetAnimTypePill.h/.cpp`.

### Decisions made without asking
- Live parameters lay rebuilt settings over the playing tracks' (one for one; extra linked settings left alone); a state machine's state still only reads on start.
- Random chances over 1 are scaled down together (not clamped one by one); v1's variations each get an equal share with the state's own.
- One own machine per Widget Blueprint (timelines can be many); it is not a component-playable asset until saved.
- Node size changes keep the node's middle (rather than running Q on its neighbours), with no undo step of its own.
- "Start / end times" read as the timeline's start and end lines.
- Missing-parameter warnings are data validation (on save / Validate), not a Widget Blueprint compile step.
- MVVM is a Data Binder trigger action (the binder already watches View Model fields), not a new component.

### Unverified (no engine here)
- `UGraphEditorSettings::DefaultCommentNodeTitleColor` and `SCommentBubble`'s `OnToggled` / `GraphLOD` / `IsGraphNodeHovered` arguments.
- `SGraphNode::GetOwnerPanel()` / `GetZoomAmount()` in a node's Tick; whether a node's desired size changes with zoom at all.
- `FToolMenuInsert(..., EToolMenuInsertType::After)` for the wire section; `EUserInterfaceActionType::ToggleButton` in a tool menu section.
- `IDetailPropertyRow::GetDefaultWidgets(Name, Value, Row)` (from fix2) with the wrapped value widget; `SColorBlock` arguments; `SNumericEntryBox<float>`.
- `SHorizontalBox` slot `FillWidth(TAttribute<float>)` for the Hold fill; mouse capture in `SWidgetAnimHoldInputButton`.
- Brush names: `GraphEditor.StateMachine_16x`, `GraphEditor.SubGraph_16x`, `TreeArrow_Collapsed/Expanded`, `Icons.ChevronRight`.
- `FDataValidationContext::AddWarning`; `TFieldIterator<FProperty>(..., ExcludeSuper)` copy for Save State Machine To.

### Check by hand
- Build all modules (Uncooked and MVVM included).
- **Parameters:** bind a field: chip filled in the parameter's color, the row reads "Name = value"; Mirrored: split chip, "(x -1)"; delete the parameter by hand in the asset: red chip, "(missing)", a warning on save. Tick a parameter's Preview and change it: the preview and motion paths follow, the default stays. Play the animation from a component, then Set Widget Animation Parameter while it plays: it follows at once. A Data Binder trigger Set Animation Parameter on a View Model Float.
- **State machine:** "+" input picker (type to filter, Enter picks); categories (move, fold, rename); unread inputs last; Hold looks unlike Fire and fills edge to edge; Random Variations chances (0.3 alone, three at 0.5 -> scaled), old assets with v1 variations; change a state's animation to a long name next to a straightened wire - it stays straight; zoom - nothing moves; a collapsed group's look, open it: vignette and breadcrumb, back through the trail; right-click a wire: check boxes, after State; node comment bubble style, its pin and toggle; layer tab right-click.
- **Modes:** in a Widget Blueprint switch to State Machine (made once, undo removes it), edit, save the Widget Blueprint, reopen: still there; Save State Machine To -> asset, then Save updates it; Edit Existing Asset goes back to the asset.
- **Widget Animations panel:** Save buttons floating; Selection search; track Name row; keys over the start / end lines.

## Change set before — `NameTBD_StateMachine_v1_fix2.patch` (applies on top of `NameTBD_StateMachine_v1_fix1.patch`; code only; untested in editor)

### Reported
- No bind chip on any track row (Start / End Scale, Overshoot...), with a Vector 2D parameter declared.
- The Parameters category header was blank - should read "Animation Parameters".
- A Vector 2D parameter's default showed no value (and a Color's would have been the same).

### Cause
- The chip relied on `FPropertyEditorModule::GetGlobalRowExtensionDelegate`, which this details view never showed (not verified why without the engine source - the extension column stays empty in the Selection panel).
- `Category.HeaderContent(..., bWholeRowContent=true)` replaces the whole header, title included; it only had the "+".
- `IPropertyHandle::CreatePropertyValueWidget` on a struct (FVector2D, FLinearColor) gives an empty widget - struct headers come from their type customizations.

### Fixed
- The chip is now the Selection panel's own: `SWidgetAnimTimeline::MakeParameterBindChip(Handle)` (same checks as before, icon and tooltip read live), added after the row's value by `FWidgetAnimSelectionDetails::AddBindChip` (`GetDefaultWidgets` + `CustomWidget(true)`). The global row extension registration is gone (`NameTBDEditor.cpp/.h`).
- Header: "Animation Parameters" title, then "+".
- `WidgetAnimTypePill::MakeValueEditor(Handle)`: Vector 2D as X and Y number boxes, Color as a swatch opening the color picker, anything else the plain value widget - used by the parameter rows and the entry rows (`WidgetAnimParameterValueCustomization.cpp`).

### Files
- `SWidgetAnimTimeline.h/.cpp`, `WidgetAnimTypePill.h/.cpp`, `WidgetAnimParameterValueCustomization.cpp`, `NameTBDEditor.cpp`, `NameTBDEditor.h`.

### Unverified (no engine here)
- `IDetailPropertyRow::GetDefaultWidgets(Name, Value, FDetailWidgetRow&)` and `FDetailWidgetDecl::MinWidth/MaxWidth` as `TOptional<float>`; `SColorBlock` `ShowBackgroundForAlpha` / `OnMouseButtonDown`.

### Check by hand
- A Scale track: chips after Start Scale, End Scale, Overshoot, Delay, Invert Track, Easing; Start Scale still expands to X / Y.
- Bind Start Scale to a Vector 2D: the chip fills; Mirrored, Unbind.
- The header reads "Animation Parameters" with "+" at the right; a Vector 2D default shows X / Y; a Color default shows a swatch that opens the picker; the same on a component entry's Parameters rows.

## Change set before — `NameTBD_StateMachine_v1_fix1.patch` (applies on top of `NameTBD_StateMachine_v1.patch`; code only)

### Reported (first build of StateMachine v1)
- `SWidgetAnimTimeline.cpp` (`OpenBindMenu`): C2665 on three `FExecuteAction::CreateSP(this, &SWidgetAnimTimeline::BindLayerField / SetLayerBindingScale, ...)` (and the `AddMenuEntry` calls around them, as a consequence); C2445 on `EnumProperty ? EnumProperty->GetEnum() : (ByteProperty ? ByteProperty->Enum : nullptr)` in `AddParameterFromField`.

### Cause
- `CreateSP` payloads are matched by their decayed types: the bound function must take them **by value** (`TArray<int32>, FString, FName`), not `const&`. Rule for later: bind a function taking `const T&` through a lambda, or give it by-value parameters (as `AddParameterFromField` has).
- `FByteProperty::Enum` is a `TObjectPtr<UEnum>`: in a `?:` against a raw `UEnum*` it is ambiguous - use `.Get()`.

### Fixed
- `OpenBindMenu` builds its actions as lambdas holding the panel weakly (`WeakPanel`, `MakeBind(Name)`); `ByteProperty->Enum.Get()`.

### Files
- `SWidgetAnimTimeline.cpp`.

### Check by hand
- The build finishes; the bind chip's menu binds, mirrors and unbinds.

## Change set before — `NameTBD_StateMachine_v1.patch` (applies on HEAD `d95ab56` on its own; code + 3 SVGs; untested in editor)

### Asked
- Animation Parameters (the planned feature), then the state machine additions items 1-6.
- Tweaks:
  - Entry / Exit couldn't be double-click jumped to.
  - Press And Hold "Is Let Go" stayed true forever - should fire once, like a trigger.
  - Inputs panel: align names and types; types drawn like UE's My Blueprint variable rows (a pill in the type's exact UE pin color + the type name); Choice = enum color; Trigger and Hold keep their colors; animation parameters use the same design.
  - No "X" to delete an input - right-click instead.

### Done
- Everything above, as described in "State machine (as it is now)" and "Animation Parameters (v1 done)". Item 5 is Option A (collapsed groups).

### Files
- New: `NameTBD/WidgetAnimation/WidgetAnimParameters.h/.cpp`; `NameTBDEditor/Private/WidgetAnimTypePill.h/.cpp`, `WidgetAnimParameterValueCustomization.h/.cpp`; `Resources/WidgetAnim_ParameterBind.svg`, `WidgetAnim_ParameterBound.svg`, `WidgetAnim_StateMachineActions.svg`.
- Changed (runtime): `WidgetAnimHost.h/.cpp`, `WidgetAnimLibrary.h/.cpp`, `WidgetAnimPreset.h/.cpp`, `WidgetAnimStateMachine.h/.cpp`, `WidgetAnimStateMachineRuntime.h/.cpp`, `WidgetAnimationComponent.h/.cpp`.
- Changed (editor): `NameTBDEditor.cpp/.h`, `NameTBDStyle.h/.cpp`, `NameTBDColors.h`, `NameTBDTooltips.h`, `SWidgetAnimStateMachineEditor.h/.cpp`, `SWidgetAnimTimeline.h/.cpp`, `WidgetAnimMachineConditionCustomization.cpp`, `WidgetAnimMachineJump.h/.cpp`, `WidgetAnimPanelSource.cpp`, `WidgetAnimStateMachineGraph.h/.cpp`.

### Decisions made without asking
- **Parameters:** the defaults are what the editor preview plays. Precedence: play-time / Set > entry rows and state inputs > default. Deleting a parameter removes its bindings; changing its type drops the bindings that can't take it.
- **Is Let Go** fires on any release of a press that had started, before or after its Hold Time (never the frame it completes).
- **Jump To Entry** restarts the whole designer preview (every input to its default), not just one layer.
- **Pill colors:** Float = the Real/Double pin color, Vector 2D = the struct pin color, Color = FLinearColor's; Trigger and Hold unchanged.
- **Layer-state conditions** read a snapshot taken before any layer moves; a layer mid-transition counts as in its target.
- **Actions** run after the end-of-tick reset - seen next frame.
- **Blend states:** one player per sample, all running; weights only change when inputs do.
- **Groups** are editor-only views; an open group shows only its own states.
- Patch named `NameTBD_StateMachine_v1` (as planned), though it also carries Animation Parameters v1 and the tweaks.

### Unverified (no engine here)
- `UEdGraphSchema_K2::GetPinTypeColor` for the pill colors, and the `Kismet.VariableList.TypeIcon` brush name (5.7).
- `FPropertyEditorModule::GetGlobalRowExtensionDelegate` and `FPropertyRowExtensionButton` usage for the bind chip.
- `IDetailGroup::AddCustomContextMenuAction` (right-click Rename / Delete on a parameter group).
- Writing through `FInstancedPropertyBag` / `FInstancedStruct` by reflection (`WidgetAnimParameters::WriteField`).
- UHT: an enum value named `None` in `EWidgetAnimMachineBlendSpace` (shown "Single Animation").
- `FAppStyle` `Icons.Lightning` (only a fallback when the SVG is missing).

### Check by hand
- **Build:** the whole plugin; watch for C4458/C4456 (names were checked) and UHT on the new USTRUCTs / UFUNCTIONs.
- **Old assets:** open an existing state machine and a few animations - nothing changed, nothing dirty.
- **Is Let Go:** a Press And Hold with "held -> Holding" (Is Held) and "Holding -> Idle" (Is Let Go): let go early goes back once; nothing re-fires while idle.
- **Jump:** double-click Entry (preview restarts) and Exit; jump to a state behind an Is Let Go.
- **Inputs panel:** columns line up; pills match a Blueprint variable's colors; right-click Rename / Delete.
- **Parameters:** add a Float, bind a Slide distance with the chip, change the default - the preview follows; a component entry row with Set Here; `Set Widget Animation Parameter` before Play; New Parameter From This Field; Mirrored; a Choice with Plays When on a track.
- **State parameters:** a state's Parameters From Inputs ("Distance <- Drag") - re-enter the state after changing the input.
- **Layer-state conditions:** layer B "Interaction is Pressed" - follows A regardless of layer order; Entered fires once; rename the state in A and the condition follows.
- **Masks:** two Override layers on the same widget, one set to All But That Widget - the other keeps it; the tab badge.
- **Blend:** Blend 1D on a Float with three samples - scrub the input; Blend 2D on a Vector 2D; the node plot dots while watched; Get State Machine Blend Weights.
- **Actions:** On Enter Fire Trigger chaining two states; Fire Event reaching On State Event (Event Name set) and a Custom Event `On <Machine> <Event>`; the lightning badge; Last Actions in the tooltip while watched.
- **Groups:** Collapse To Group, double-click to open, back button, Expand In Place, Ungroup, Rename, undo of each; transitions into a collapsed box; add a state inside an open group.
- **Random:** two Random transitions with weights 1 and 3; a state with Random Variations; Speed Input at 0, 0.5, 2.

## Change set before — `NameTBD_DockFix_v1_fix2.patch` (applies on top of `NameTBD_DockFix_v1_fix1.patch`; committed in `d95ab56`)

### Fixed
- `SDockTab::SetLayoutIdentifier` is protected (the fix1 "Unverified" item): `WidgetAnimDockPreference::FDockTabLayoutIdAccess` (derives `SDockTab`, never made) takes a pointer to the member and calls it on the tab. `InsertDocumentTab` uses it.

## Change set before — `NameTBD_DockFix_v1_fix1.patch` (applies on top of `NameTBD_DockFix_v1.patch`; code only; untested in editor)

Applies in any order with NiagaraUI v1-fix5, TargetLabels v1 and MotionPaths fix8-fix12 (checked with `git apply`).

### Reported
- **Ensure still fires:** the same `FindTabInLiveAreas` ensure, now from Widget Animation Scripting's Dock in Layout (`DockScriptingInLayout`, line 798 - that is with DockFix v1 applied).
- **Edited state machine forgotten:** close UE and reopen, and the State Machine editor asks which machine to edit again.
- **Docked choice forgotten:** whether Widget Animations, Widget Animation Scripting and the State Machine editor were docked should persist.

### Cause
- **The ensure (DockFix v1 got it wrong):** `FTabManager::InsertNewDocumentTab` ensures that nothing in the live layout matches the new tab's layout id *before* giving it its document id. A freshly made `SDockTab`'s id is empty (`FTabId()`), which matched whatever else had none. So it fired on any dock, duplicate or not. DockFix v1's duplicate handling (`CloseTabNow`, the pre-insert check) stays: it is still right, it just wasn't this.
- **Edited machine:** nothing was saved. The editor started from `FindMachineInWidgetBlueprint`, which finds nothing while the designer preview isn't built yet - at editor startup.
- **Docked choice:**
  - only kept in memory (`bScriptingDocked`, per editor);
  - the docked tabs live under spawner-less document ids, which a saved layout can't bring back.

### Fixed
- **New `NameTBDEditor/Private/WidgetAnimDockPreference.h`:**
  - **`InsertDocumentTab`:** gives the tab a unique placeholder id (`NameTBD.PendingDock_<n>`) before `InsertNewDocumentTab`. Used by all three Dock in Layouts (Widget Animations, Scripting, State Machine).
  - **Docked choice:** kept in `EditorPerProjectUserSettings.ini` `[NameTBD.DockedPanels]` - `WidgetAnimations`, `WidgetAnimationScripting`, `WidgetAnimationStateMachine` = the editor mode it was docked in (Designer or Graph), empty when not docked; flushed at once.
    - **Set** by every Dock in Layout that ends with the tab open.
    - **Cleared** when the tab is closed by hand: each tab body is wrapped in `SDockWatch`, whose destructor checks 0.25 s later and clears the choice only if:
      - the engine isn't exiting;
      - the editor is still open (`UAssetEditorSubsystem::FindEditorForAsset`);
      - it's still in the mode it was in (a Designer <-> Graph switch lays the editor out afresh and destroys the tabs - that isn't a close);
      - no tab of the panel is open again (Dock in Layout replacing a floating one).
  - **Restored** 0.3 s after every Widget Blueprint editor's toolbar is (re)built, if docked in the mode the editor is in now. That is on open, and after each mode switch. Restoring is quiet: no drawer dismissed, nothing brought forward, the choice re-saved. `DockInLayout(..., bRestore)` / `DockScriptingInLayout(..., bRestore)` / `WidgetAnimStateMachineEditorHooks::RestoreDock`.
- **Scripting:** a scripting tab restored by the shared layout now stays in every editor while the choice is docked (`WasScriptingDocked` also reads it).
- **Edited machine:** kept per Widget Blueprint in `EditorPerProjectUserSettings.ini` `[NameTBD.WidgetAnimationStateMachineEditor.EditedMachines]` (Blueprint path = machine path), saved by `SetMachine`.
  - **On construct:** the machine asked for, else the one saved for this Blueprint, else the first the Blueprint plays.
  - Picking none removes the entry.

### Files
- New: `NameTBDEditor/Private/WidgetAnimDockPreference.h`.
- Changed: `WidgetAnimBuilderToolbar.cpp`, `SWidgetAnimStateMachineEditor.h/.cpp`.

### Unverified (no engine here)
- **Tab ids:** `SDockTab::SetLayoutIdentifier` being public. If it isn't, the placeholder has to come another way - e.g. through a spawner-made tab.
- **Editor mode:** `FWidgetBlueprintEditor::GetCurrentMode()` gives the mode, reached by `static_cast` after checking `GetToolkitFName() == "WidgetBlueprintEditor"`.
- **Editor open:** `FindEditorForAsset` returning the same instance as the toolkit while it is open, and nothing once closed.

### Check by hand
- **No ensure:** Dock in Layout for each of the three. No ensure in the log, no hitch.
- **Docked choice survives a restart:** dock all three, close UE, reopen the Widget Blueprint. All three come back docked, quietly.
- **Closing clears it:** close the docked State Machine tab by hand, then reopen the editor. It stays in its drawer.
- **Mode switch keeps it:** switch to the Graph and back. It doesn't clear the choice, and the tab comes back in the Designer.
- **Edited machine:** pick a state machine in the editor, close UE, reopen. The same machine is open, with no prompt.

## Change set before — `NameTBD_DockFix_v1.patch` (applies on HEAD 55a5763 on its own; code only; untested in editor)

Applies in any order with every other outstanding patch: NiagaraUI v1-fix5, TargetLabels v1, MotionPaths fix8-fix12 (checked with `git apply`).

### Reported
- **Ensure on dock:** Dock in Layout of the Widget Animation State Machine drawer docks fine but hitches, with a handled ensure: `! FindTabInLiveAreas( FTabMatcher(UnmanagedTab->GetLayoutIdentifier()) ).IsValid()` (TabManager.cpp:1872).
- **Where:** from `WidgetAnimStateMachineEditorHooks::DockInLayout`, at its `InsertNewDocumentTab`.

### Cause
- **Duplicate id:** `InsertNewDocumentTab` ensures that no live tab already has the inserted tab's id (`DockedTabId`). Two ways one did:
  - the tab manager of the well docked into (the dock target's) isn't always the editor's own, so `FindLiveTab` - which only asked the editor's manager - missed a tab already docked there, and a second one was inserted;
  - a floating tab, or a failed attempt, was only asked to close (`RequestCloseTab`) before the next insert under the same id, and could still be live.
- **The hitch:** the ensure's callstack capture.

### Fixed
- **`CloseTabNow`:** requests the close, then removes the tab from its parent at once if it is still live.
- **Before each insert:** the dock target's manager is asked for a live tab under the docked id. Docked already: brought forward, done. Floating: closed now.
- **Same fix in the Widget Animation Scripting Dock in Layout** (`WidgetAnimBuilderToolbar.cpp`), which had the identical pattern.

### Files
- `NameTBDEditor/Private/SWidgetAnimStateMachineEditor.cpp`
- `NameTBDEditor/Private/WidgetAnimBuilderToolbar.cpp`

### Unverified (no engine here)
- `SDockTab::RemoveTabFromParent` being public in 5.7.

### Check by hand
- **State machine:** Dock in Layout from the drawer: no ensure, no hitch. Again while docked: brought forward. Float the tab, Dock in Layout again: docked, still no ensure.
- **Scripting:** the same with the Widget Animation Scripting drawer's Dock in Layout.

## Change set before — `NameTBD_NiagaraUI_v1_fix5.patch` (applies on top of `NameTBD_NiagaraUI_v1_fix4.patch`; code only; untested in editor)

Applies in any order with `NameTBD_TargetLabels_v1.patch` and MotionPaths fix8-fix12 (checked with `git apply`).

### Asked
- Support the high and medium priority parameter types:
  - Curve for Floats, Color Curve (high);
  - Vector Curve / Vector 2D Curve, the Array data interfaces, Texture Sample (medium).

### Done
- **Curves and arrays** are listed in User Parameters on the widget and on the Particles track, and handed to the simulation. See Niagara particles in UI > User Parameters > Data interfaces.
- **Arrays:** all nine Niagara array types, not just the four named.
- **Texture Sample:** deliberately not added. It samples on the GPU only, and only CPU emitters are drawn in UI, so it would never show anything. A plain Texture object parameter (already supported) is what UI systems should expose.

### Files
- `NameTBDNiagara/Private/NiagaraUIParameters.cpp` (data interface support);
- `Public/NiagaraUIParameters.h`, `Public/NiagaraUIWidget.h` (comments).

### Decisions made without asking
- **Vector 2D Curve** is shown as a vector curve; its Z is ignored.
- **A changed curve restarts the effect it is set on:** the simulation is made again so it binds the new curve. This happens only when a curve really changed, and in practice only as an effect starts.
- **Arrays** are set live, without a restart.

### Unverified (no engine here)
- **Data interface class names:** `NiagaraDataInterfaceCurve`, `ColorCurve`, `VectorCurve`, `Vector2DCurve`, `ArrayFloat`, `ArrayFloat2/3/4`, `ArrayPosition`, `ArrayColor`, `ArrayQuat`, `ArrayInt32`, `ArrayBool`. A wrong one just isn't listed.
- **Curve members:** `Curve`, `Red/Green/Blue/AlphaCurve`, `X/Y/ZCurve` - matched by name.
- **Curve base:** `UNiagaraDataInterfaceCurveBase::UpdateTimeRanges` / `UpdateLUT`, called virtually. If either is missing or not virtual: a compile or link error, to fix by name.
- **Arrays:** `UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArray*` (header `NiagaraDataInterfaceArrayFunctionLibrary.h`) taking the full `User.` name.
- **Parameter store:** `FNiagaraParameterStore::GetDataInterface` / `SetDataInterface`, `UNiagaraComponent::GetOverrideParameters`, `ReinitializeSystem`.
- **Property bags:** the `FPropertyBagPropertyDesc(Name, EPropertyBagContainerType::Array, Type, Object)` constructor.
- **Details:** how `FRuntimeVectorCurve` displays inside a bag - a curve editor or plain key lists.

### Check by hand
- **Curve:** a CPU system with a User **Curve for Floats** driving size over life, set on a Niagara Particles widget. User Parameters shows the curve at the system's shape. Change it: the preview restarts with the new shape. Save, reopen: kept.
- **Color Curve:** same with a color over life.
- **Array:** a User **Color Array** read by index in the system. Edit it: the colors change at once, without a restart.
- **On a track:** the same parameters on a Particles track with its own system. A burst uses the track's curve, and the next burst without it is back to the widget's (or the system's).

## Change set before — `NameTBD_NiagaraUI_v1_fix4.patch` (applies on top of `NameTBD_NiagaraUI_v1_fix3.patch`; code only; untested in editor)

Applies in any order with `NameTBD_TargetLabels_v1.patch` and MotionPaths fix8-fix12 (checked with `git apply`).

### Reported
- With **Use Widget's Niagara System** on, Niagara System and User Parameters should grey out, not disappear.
- The track's **User Parameters** were greyed out / read-only even when they should be editable.

### Cause
- **Read-only:** the bag carried an engine EditCondition (`!bUseWidgetSystem && (Action == ...)`). A property bag nested in Type Params (an instanced struct) under an EditCondition comes back read-only. The panel already avoided this for Shape key bags.

### Fixed
- **No EditCondition on either field:** Niagara System and User Parameters now use two new metadata keys the Widget Animations panel understands:
  - `ShowWhen = "Action=BurstAtTarget|Play"` - hidden for Stop Emitting / Clear, as before;
  - `EnableWhen = "bUseWidgetSystem=false"` - greyed out while Use Widget's Niagara System is on.
- **The panel side:** `FTypeParamsDetails::OnChildRowAdded` (`SWidgetAnimTimeline.cpp`) records each settings field's handle as it is added and binds a later field's ShowWhen / EnableWhen to the row's Visibility / IsEnabled.

### Files
- `NameTBDNiagara/Public/WidgetAnimParticleCue.h`
- `NameTBDEditor/Private/SWidgetAnimTimeline.cpp`

### Unverified (no engine here)
- **Bag rows:** that a row's `IsEnabled(false)` greys the bag's child rows too (it does for ordinary struct children).
- **Enum names:** that `GetValueAsFormattedString(PPF_None)` gives an `enum class` value's short name (`BurstAtTarget`) and a bool's `true` / `false` (compared case-insensitively).
- **Cause:** if User Parameters are still read-only with Use Widget's Niagara System off, the cause is elsewhere. Next suspects:
  - the bag customization's own enabled state under nested external structures;
  - the instanced struct provider.

### Check by hand
- **Burst / Play, Use Widget's Niagara System off:** Niagara System and User Parameters are editable. Change a parameter: it sticks, and the preview uses it.
- **Use Widget's Niagara System on:** both are greyed out and keep their values.
- **Stop Emitting / Clear:** both hidden.

## Change sets before — `NameTBD_NiagaraUI_v1_fix3.patch` and `NameTBD_TargetLabels_v1.patch` (code only; untested in editor)

`NameTBD_NiagaraUI_v1_fix3.patch` applies on top of `NameTBD_NiagaraUI_v1_fix2.patch`. `NameTBD_TargetLabels_v1.patch` applies on HEAD (55a5763) on its own. Both apply in either order, with or without MotionPaths fix8-fix12 (checked every order with `git apply`).

### Asked
- **Particles track:**
  - add "Use Widget's Niagara System": on, the system can't be picked;
  - with it, an enum: act on the first found Niagara Particles widget or all of them;
  - off, works as before;
  - auto-populating User Parameters on the track, like the widget's.
- **Widget Details:** Parameters sat at the bottom by UWidget's own categories; it belongs right under Particles.
- **Labels:** the default option "None" of Target Widget (animation editor), and of Plays On and the Layer Group rows (state machine preview) should read "(this widget)".

### Done
- **Track settings:** Use Widget's Niagara System, Act On (First Found / All Found), and User Parameters - see Niagara particles in UI > Particles cue.
- **Track parameters:** laid out from the track's system when the track is edited; the Selection panel rebuilds when the rows change (new evaluator hooks, see Widget animation architecture > Evaluators).
- **Parameter code shared:** moved out of `NiagaraUIWidget.cpp` into `NiagaraUIParameters.*`, which the widget and the track both use.
- **Widget category order:** `PrioritizeCategories`.
- **"(this widget)":** one constant (`WidgetAnimTargetOption.h`) used by:
  - the panel source's options and pick-in-designer;
  - the Layer Group and track Target Widget rows;
  - the watch menu;
  - the state machine editor's Plays On / group rows and its pick;
  - the component details' list;
  - scripting.

### Files
- **NiagaraUI fix3:**
  - New: `NameTBDNiagara/Public/NiagaraUIParameters.h`, `Private/NiagaraUIParameters.cpp`.
  - Changed: `NiagaraUIWidget.h/.cpp`, `WidgetAnimParticleCue.h/.cpp`, `NameTBD/WidgetAnimation/WidgetAnimEvaluator.h` (two virtuals), `NameTBDEditor/Private/SWidgetAnimTimeline.cpp` (refresh after edits, layout hash).
- **TargetLabels v1:**
  - New: `NameTBDEditor/Private/WidgetAnimTargetOption.h`.
  - Changed: `SWidgetAnimTimeline.cpp`, `SWidgetAnimStateMachineEditor.h/.cpp`, `WidgetAnimPanelSource.h/.cpp`, `WidgetAnimationComponentDetails.cpp`, `WidgetAnimScripting.cpp`.

### Decisions made without asking
- **Use Widget's Niagara System** is off by default (old tracks work as before). It is hidden for Stop Emitting / Clear; Act On shows for those instead, since they always act on the widget's own.
- **Hidden, not greyed:** the track's Niagara System and User Parameters are hidden while Use Widget's Niagara System is on. (Changed to greyed in fix4, at the user's request.)
- **Track over widget:** the track's changed values go on over the widget's for the same name. The track's unchanged ones leave the widget's (or the system's) in place.
- **The "(this widget)" string** is an editor label only; nothing stored changes (still None). The component's "(This Widget)" value is untouched.

### Unverified (no engine here)
- **Details:** `PrioritizeCategories` ordering several categories in the listed order (StyledButtons uses one).
- **API:** `FInstancedStruct::GetMutablePtr<T>`.
- **Edit conditions:** a compound EditCondition mixing a bool and enum compares (`bUseWidgetSystem || Action == ...`).
- **Bags in instanced structs:** an `FInstancedPropertyBag` with `FixedLayout` + `EditConditionHides` inside a Custom track's Type Params.

### Check by hand
- **Widget:** the Details panel of Niagara Particles shows Particles, then Parameters, then Playback, Layout, Appearance.
- **Track parameters:**
  - Add a Particles track and pick a system with User parameters: User Parameters lists them at once.
  - Change one: the preview plays with it. The widget's own value of another parameter still applies.
- **Use Widget's Niagara System:**
  - Turn it on: Niagara System and User Parameters go, Act On appears.
  - With Act On = All Found and two Particles widgets with different systems: a Burst plays each one's own system.
  - Turn it off: the track's system and values are back as left.
- **Labels:** a Layer Group's Target Widget and a plain list track's Target Widget show "(this widget)" when empty; picking it stores None. The state machine preview's Plays On and group rows show the same. Pick-in-designer on the preview itself still works.

## Change set before — `NameTBD_NiagaraUI_v1_fix2.patch` (applies on top of `NameTBD_NiagaraUI_v1_fix1.patch`; code + config; untested in editor)

### Reported
- **Wrong place:** "The Niagara UI widget ignores the actual position of the widget in the designer, and always plays at the origin of the entire widget."
- **Parameters:** they should populate in the Details panel from the Niagara System.
- **System mismatch:** why does the track have to match the Niagara System on the widget? Unify for production workflows.

### Cause
- **Wrong place:** the Particles cue's only burst was "Burst At Track's Widget". A track whose Layer Group has no Target Widget is aimed at the whole Widget Blueprint, so the burst came out at its middle, not where the Particles widget is placed. The main system (Auto Play / Play) already plays at the widget's Origin. If it ever doesn't, `NiagaraUI.Log 1` now logs where each simulation starts: in the widget, as drawn, and the widget's rect.
- **Parameters:** they were three hand-typed maps (float / color / vector).
- **System mismatch:** the cue only took a system for bursts (Burst System); Play always played the widget's own.

### Fixed
- **User Parameters:** laid out from the system (see Niagara particles in UI > User Parameters). Blueprint setters for every type, plus Reset Parameters.
- **Burst At Origin:** bursts where the widget is.
- **Particles cue unified** (see Niagara particles in UI > Particles cue):
  - The track's Niagara System drives Burst and Play.
  - Bursts play At Particles Widget by default.
  - Aim the track at the Particles widget, or name it.

### Files
- Changed:
  - `NameTBDNiagara/Public/NiagaraUIWidget.h`, `Private/NiagaraUIWidget.cpp`;
  - `Public/WidgetAnimParticleCue.h`, `Private/WidgetAnimParticleCue.cpp`;
  - `NameTBDNiagara.Build.cs` (`PropertyEditor` for editor builds);
  - `Config/DefaultNameTBD.ini` (`BurstSystem` -> `NiagaraSystem` redirect).

### Decisions made without asking
- **Which parameters are kept:** only parameters changed on the widget are written to the simulation; the rest follow the system, including later edits in the Niagara editor.
- **Removed and retyped parameters:** a parameter removed from the system disappears from the widget, and one whose type changed is reset to the system's value.
- **Play replaces the system:** the cue's Play with a Niagara System makes that the widget's system from then on, and Set Niagara System does the same. Burst never changes the widget's system.
- **v1 Blueprint access:** the v1 parameter maps are no longer Blueprint-visible. v1 was just released, so any Blueprint reading them should use the setters instead.

### Unverified (no engine here)
- **Niagara:**
  - `FNiagaraUserRedirectionParameterStore::GetUserParameters`, `FNiagaraParameterStore::GetParameterValue<T>` / `GetUObject`.
  - `FNiagaraTypeDefinition` (`GetPositionDef`, `GetQuatDef`, `IsEnum`/`GetEnum`, `IsUObject`, `IsDataInterface`, `GetClass`).
  - `UNiagaraComponent::SetVariablePosition`, `SetVariableVec4`, `SetVariableQuat`, `SetVariableTexture`, `SetVariableMaterial`, `SetVariableObject`.
- **Property bags:** `FInstancedPropertyBag::GetMutableValue`, `SetValueEnum`, `GetValueEnum`; `UScriptStruct::CompareScriptStruct`; `FPropertyEditorModule::NotifyCustomizationModuleChanged`.
- **Header:** `UObject/ObjectSaveContext.h` for `PreSave(FObjectPreSaveContext)`.

### Check by hand
- **Parameters:**
  - Give a system User parameters (a float, a color, a texture), set it on a Niagara Particles widget: User Parameters lists them at the system's values.
  - Change one: the designer preview plays with it. Save, reopen: kept.
  - Change another's default in the Niagara editor: the widget follows it. Add a parameter there: it appears on reopening the Widget Blueprint (or on picking the system again).
- **Bursts and cues:**
  - Place the widget anywhere and call Burst At Origin (or a Particles cue, Burst, At Particles Widget): it plays where the widget is.
  - Particles cue with its own Niagara System on a widget set to another (or none): plays the track's system.
  - Particles cue Burst At Track's Widget on a staggered list: one burst per item.
- **Logging:** `NiagaraUI.Log 1`: the "plays ... at" line shows the start point in the widget and on screen.

## Change set before — `NameTBD_MotionPaths_v1_fix12.patch` (applies on top of `NameTBD_MotionPaths_v1_fix11.patch`; code only; untested in editor)

### Reported (with `WidgetAnim.LogMotionPaths 1`, in the WidgetAnimationBuilder)
- Still no clicking, and no grab cursor over the diamonds.
- fix11 fixed the hit test: some presses now logged "on diamond N".
- But they went on: "the layer's designer isn't what is under the mouse (SOverlay) - left alone".
- "the designer widget wasn't found yet" repeated every second.
- A second layer with 0 markers was also over the same designer (harmless).

### Cause
- `IsDesignerUnder` and `FindDesignerWidget` looked for a widget whose `GetType()` is `SDesignerView`. No widget in the path under the mouse or up the preview's parents had that type.
- The geometry the designer hands its extensions comes from a widget we can't name reliably.
- No cursor: Slate asks the hovered widget (the designer) for the cursor right after every mouse move, and nothing else ever set one.

### Fixed
- **Designer matched by place, not type:** `IsDesignerRect(Geometry)` - a desktop-space geometry whose absolute position and size equal `Designer`'s within 2 px. That is the widget the paths were drawn with, whatever its type.
  - `IsDesignerUnder` accepts the path when any widget in it is that rect, and adopts it as `DesignerWidget`.
  - `FindDesignerWidget` walks up the preview's parents to that rect (`GetTickSpaceGeometry`). Logged once while not found, once when found.
  - A menu, drawer or window over the designer has no widget with that exact rect in its path.
- **Cursor:**
  - Over a diamond or handle, `FMotionPathInput` sets the platform cursor (`SlateApp.GetPlatformCursor()->SetType`): Grab Hand for draggable keys and handles, Hand for pick-only edges.
  - Pressed, and during a drag: Grab Hand Closed.
  - **That mouse move is consumed**, so Slate doesn't ask the designer for its cursor and put it back.
  - Elsewhere moves pass through untouched.
- **Log:** a press refused because the designer isn't under it lists the innermost 10 widgets under the mouse with their desktop rects, and the rect the paths are drawn over.
- **Naming:** the input processor's press members are `LeftMarker` / `LeftHandle`. `PressedMarker` is a local in its button-up handler (C4458 rule).

### Check by hand
- Hover a diamond: grab cursor and highlight. Click, Ctrl/Shift-click, drag, Alt-drag, handle drag, right-click menu, all with nothing selected in the designer.
- If a press is still refused, send the "Motion path press ... Under the mouse: ..." line: it shows which rects didn't match.

## Change set before — `NameTBD_MotionPaths_v1_fix11.patch` (applies on top of `NameTBD_MotionPaths_v1_fix10.patch`; code only; untested in editor)

### Reported (with `WidgetAnim.LogMotionPaths 1`)
- Still no clicking on the diamonds. The log repeated "the designer widget wasn't found yet", and every press said "over the designer, but on no diamond or handle".

### Cause
- **Coordinate spaces:** Slate paints each window in **window space** (`SWindow::GetWindowGeometryInWindow`, origin at the window's top left). Mouse events and `GetTickSpaceGeometry()` are in **desktop space** - `SWidget::Paint` makes the desktop geometry by `AppendTransform(WindowToDesktop)`. The extension's `Paint` geometry is window space. The layer mapped desktop-space path points through it and hit-tested desktop mouse positions against it, so everything was off by the window's position on the desktop. On a maximized window that is a few pixels, so the paths looked right, but it is about the size of a diamond's hit box, so every press missed.
- The designer widget was never found up the preview's parents. Its geometry fallback compared desktop-space path geometry with the window-space designer geometry, so it could never match either.

### Fixed
- **Two geometries:** `FMotionPathLayer::Paint` keeps the paint geometry (window space) for drawing and clipping. It builds `Designer` = that geometry `AppendTransform`ed by the paint window's `GetPositionInScreen()` (desktop space), and uses `Designer` for `Update` / `MapToCanvas`, `IsOver`, `HitTest` and the drag threshold. Canvas-local coordinates are the same in both. A side effect: the paths now sit exactly on the widgets in a window that isn't at the desktop's origin; before, they were offset by the window position.
- **Rule:** in an `OnPaint` / extension `Paint`, the geometry is window space. Anything compared with mouse positions or tick-space geometry must go through the window's position first.
- **Designer widget:** `IsDesignerUnder` adopts the `SDesignerView` found in the path under the mouse when its geometry matches `Designer` (`DesignerWidget`). So the designer is known after the first press even where `FindDesignerWidget` can't find it.
- **Log:** a missed press now logs where it landed on the designer, the nearest diamond and its distance, and the hit radius.

### Check by hand
- Same as fix10. With `WidgetAnim.LogMotionPaths 1`, a press on a diamond logs "taken". If it doesn't, the "nearest ... away" numbers say by how much it missed.

## Change set before — `NameTBD_MotionPaths_v1_fix10.patch` (applies on top of `NameTBD_MotionPaths_v1_fix9.patch`; code only; untested in editor)

### Reported
- fix8/fix9 work - paths show with nothing selected, keys stay still - but the diamonds can't be interacted with: no drag, no multi-select, no right-click menu.

### Cause
- `FMotionPathInput` only took a press when the layer had been painted this frame or the last (`PaintedFrame + 1 >= GFrameCounter`) and in the very window object the paint reported. An idle designer isn't painted again every frame - its last drawing is kept and shown - so by the time of a click the layer counted as not drawn and every press went to the designer.

### Fixed
- `IsOver`: drawn at least once and the mouse over the designer as last drawn (no frame test). `IsDesignerUnder`: the widget path under the mouse must go through **this layer's own `SDesignerView`** - found once (`FindDesignerWidget`: up the parents of `FindWidgetAnimDesignerPreview`'s Slate widget; retried at most once a second while unknown); until found, an `SDesignerView` with the layer's geometry. No window test. A designer tabbed in front of another in the same place can't have its clicks taken by the hidden one's diamonds.
- `RequestRepaint` (invalidates that designer, Paint) on hover change, press, every drag move and release - so highlights and the dragged path update even when nothing else repaints the designer.
- `WidgetAnim.LogMotionPaths 1` also logs each press over a designer: taken (which diamond/handle), on no diamond, or on a diamond but the designer isn't under the mouse (with the widget that is).

### Check by hand
- Nothing selected: hover a diamond (it lights up), click it (key picked in the Track Editor), Ctrl/Shift-click others (multi-select), drag (moves; one undo), Alt-drag (handles), drag a handle, right-click a diamond (Path / Easing Across menu, acting on the picked keys of its track). Empty canvas still selects widgets.
- If a press still isn't taken: `WidgetAnim.LogMotionPaths 1` and read the "Motion path press" lines.

## Change sets before — build fixes `NameTBD_NiagaraUI_v1_fix1.patch` and `NameTBD_MotionPaths_v1_fix9.patch`

Apply on top of `NameTBD_NiagaraUI_v1.patch` and `NameTBD_MotionPaths_v1_fix8.patch` respectively. Code only.

### Reported (first build of v1 / fix8)
- `WidgetAnimMotionPath.cpp`: C2665 - `FSlateApplication::PushMenu` has no overload taking a `TSharedPtr<SWidget>` parent: on 5.7 the owner-path overload takes `const TSharedRef<SWidget>& InParentWidget, const FWidgetPath& InOwnerPath, ...`.
- `NiagaraUIWidget.cpp`: C2027 - `FVersionedNiagaraEmitterData` undefined (only forward-declared by `NiagaraTypes.h`).

### Fixed
- `OpenMarkerMenu` returns early on an invalid owner path and passes `OwnerPath.GetLastWidget()` (a `TSharedRef`). Rule: `PushMenu`'s parent widget is a `TSharedRef<SWidget>` - pass the path's last widget.
- `NiagaraUIWidget.cpp` includes `NiagaraEmitter.h`, as `NiagaraUIMesh.cpp` already did. Rule: anything reading `FVersionedNiagaraEmitterData` members includes `NiagaraEmitter.h`.
- Everything else in both change sets compiled without errors in that build (NiagaraUIMesh, NiagaraUIRuntime, SNiagaraUIWidget, WidgetAnimParticleCue, the module, WidgetAnimPanelSource, SWidgetAnimTimeline, NameTBD). The NameTBDEditor and NameTBDNiagara links weren't reached yet, so unresolved externals are still possible (e.g. a Niagara function not exported).

## Change sets before — `NameTBD_NiagaraUI_v1.patch` and `NameTBD_MotionPaths_v1_fix8.patch`

Both apply on top of the current HEAD `55a5763` ("Mid motion-path update", which has MotionPaths fix6 + fix7), independently of each other (no shared files). Code only; untested in editor (no engine here).

### NiagaraUI v1 (`NameTBD_NiagaraUI_v1.patch`)
#### Asked
- A Niagara UI particle system: highly performant and production-grade out of the box, fitting the plugin.
#### Done
- New runtime module `NameTBDNiagara` (see "Niagara particles in UI"): the Niagara Particles widget, its mesh builder, the shared runtime (ticker, brushes, cvars), and the Particles cue.
#### Files
- New: `Source/NameTBDNiagara/NameTBDNiagara.Build.cs`; `Public/NiagaraUIWidget.h`, `Public/WidgetAnimParticleCue.h`; `Private/NameTBDNiagaraModule.cpp`, `NiagaraUILog.h`, `NiagaraUIMesh.h/.cpp`, `NiagaraUIRuntime.h/.cpp`, `NiagaraUIWidget.cpp`, `SNiagaraUIWidget.h/.cpp`, `WidgetAnimParticleCue.cpp`.
- Changed: `NameTBD.uplugin` (module `NameTBDNiagara`, plugin dependency `Niagara`), `Source/NameTBD/WidgetAnimation/Evaluators/WidgetAnimCues.h` (comment only).
#### Decisions made without asking
- Drawn as Slate meshes from CPU particle data (the approach of the free Niagara UI Renderer), not render targets: correct UI layering, clipping and DPI, no extra GPU passes. GPU emitters can't be drawn this way - said at compile time and in the log.
- Default plane XZ (X right, Z up), matching the common UI-Niagara convention; XY / YZ selectable.
- Non-UI materials don't fail silently: they draw with Particle Image / a soft dot, with a one-time warning and a compile note. Additive glows need a UI material set to Additive.
- Play While Paused on by default (menus are often shown paused); Pause When Hidden on by default.
- Simulations are made lazily on first paint and released with the Slate resources; removing a widget from its parent keeps them (paused) until released.
- Kept in its own module so the core never depends on Niagara (as CommonUI / MVVM are).
#### Unverified (no engine here) - APIs most likely to need a touch on 5.7
- `UNiagaraComponent::SetRenderingEnabled`, `SetAllowScalability`, `SetForceSolo`, `AdvanceSimulationByTime`, `SetPaused` / `IsPaused`, `GetSystemInstanceController()->GetSystemInstance_Unsafe()`, `FNiagaraSystemInstance::WaitForConcurrentTickAndFinalize`, `GetEmitters()` element type (used through `auto` + `.Get()`), `FNiagaraEmitterInstance::GetParticleData` / `GetVersionedEmitter` / `IsDisabled`, `UNiagaraEmitter::GetUniqueEmitterName`, `FNiagaraEmitterHandle::GetEmitterData`.
- Renderer property names: sprite `Alignment`, `SubImageSize`, `PivotInUVSpace`, `SortMode` (`ENiagaraSortMode::CustomDecending`), `*Binding`s; ribbon `RibbonIdBinding`, `RibbonLinkOrderBinding`, `RibbonWidthBinding`, `UV0Settings` / `UV1Settings` (`ENiagaraRibbonUVDistributionMode::TiledFromStartOverRibbonLength`, `TilingLength`).
- `UWidget::SetVisibilityInternal` in the constructor; `UTexture2D::CreateTransient` + `GetPlatformData()->Mips[0].BulkData.Lock`; `FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Pos, UV, UV2, Color)` (as ShapeBox uses).
#### Check by hand
- Make a CPU sprite system (Material: a UI-domain material reading TexCoord0 and Vertex Color; or none - the soft dot shows). Drop Niagara Particles in a Widget Blueprint, set the system: it plays in the designer. PIE: plays; pause the game: still plays (Play While Paused). Collapse the widget / switch a Widget Switcher away: `NiagaraUI.Log 1` says paused; show it: resumed.
- A ribbon system (trail): ribbons drawn, smooth; with the widget moving (Render Transform animated) a world-space ribbon trails behind, a local-space one moves with it.
- Rotate/scale the widget's Render Transform: local-space particles turn/scale with it.
- A GPU emitter: compile warning in the Widget Blueprint; one log line in PIE.
- Button with a Widget Animation: add a Particles cue (Burst At Track's Widget) and a Niagara Particles widget over the screen: a burst at the button on each play; Max Bursts recycles.
- `stat NiagaraUI`: Meshes Rebuilt 0 while the game and the system are paused.

### MotionPaths v1 fix8 (`NameTBD_MotionPaths_v1_fix8.patch`)
#### Reported
- After fix6/fix7 (the user's commit), motion paths don't show at all.
- What fix6/fix7 tried to fix: the keys on the path moved while the animation played (only the "now" ring should move), and paths only showed with the widget selected in the designer (they should show whenever the open timeline has a track that moves a widget).
#### Cause (as far as can be read without the engine)
- Not showing: fix6/fix7 relied on a canvas laid into the editor window's overlay from `FDesignerExtension::Tick`, and switched the selection fallback off as soon as that canvas existed - if that canvas didn't draw (never ticked/painted there, wrong window, clip), nothing could show.
- Keys moving: the selection-anchored canvas (fix5 fallback) is placed by the designer at the selected - animated - widget; and the unposed baselines were re-read twice a second from the preview's live, animated transforms (and chain widgets without a baseline used their live transform), so the path and diamonds jumped as anything played on the preview.
#### Fixed
- Hosting and mouse rewritten (see Motion paths > Hosting / Mouse): drawn from `FDesignerExtension::Paint` every frame the designer draws, selection or not; diamonds/handles hit-tested by an input preprocessor. No overlay, no ticker, no selection-anchored canvas.
- Rest poses from the Widget Blueprint as authored (see Motion paths > Rest poses).
#### Files
- Changed: `NameTBDEditor/Private/WidgetAnimMotionPath.h/.cpp` (hosting, layer, input; the path maths, menu and drawing kept), `WidgetAnimPanelSource.h/.cpp` (`GetWidgetAnimAuthoredTransform`, authored unposed baselines), `NameTBDEditor.cpp` and `SWidgetAnimTimeline.h` (comments).
#### Unverified
- `FDesignerExtension::Paint`'s exact signature on 5.7 (`const TSet<FWidgetReference>&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&, int32) const`) and that `SDesignerView::OnPaint` calls it with no selection. If the override fails to compile, match the engine's signature; if paths still don't show, `WidgetAnim.LogMotionPaths 1` writes the reason over the designer (if nothing is written at all, Paint isn't being called).
- `FSlateWindowElementList::GetPaintWindow`, `FSlateApplication::LocateWindowUnderMouse`, `RegisterInputPreProcessor`, `FWidgetPath::GetWindow`.
#### Check by hand
- Open a Widget Blueprint with a Transform track on a widget in the Widget Animations panel; select nothing (click empty space): the path, dots and diamonds show. Close the drawer: still shown.
- Play in the panel: only the ring moves; the line and diamonds stay perfectly still. Play from the designer toolbar preview with the panel closed: same.
- Click a diamond (picks the key), drag one (moves the key, one undo), Alt-drag (handles), drag a handle, right-click (menu). Click empty canvas: the designer's selection works as before; a menu or drawer over the diamonds takes its own clicks.

## Change set before — `NameTBD_MotionPaths_v1_fix5.patch` (applies on top of `NameTBD_MotionPaths_v1_fix4.patch`; code only; untested in editor)

### Reported (fix4 works)
- Paths only show with the widget selected - should be automatic.
- Key time labels can lie right on the path.
- Keys and path drift while playing (Transform alone and with other tracks); dragging a key makes the others shake.
- Use exactly the Track Editor's key colors.
- Paths between keys are always straight - no arcs.
- No way to ease the whole track instead of each key-to-key stretch: with 3 keys the widget pauses at the middle one.

### Fixed / added
- **Automatic:** a layer laid over each open designer by a ticker (`WidgetAnimMotionPaths::Register/Unregister`, see Motion paths above); the selection extension is only the fallback.
- **Still:** layout-only mapping (see Mapping above).
- **Labels** placed off the path, on its upper side, at right angles to it.
- **Colors:** diamonds use `WidgetAnimEditor::Key*`; removed `MotionPath::KeyOutline/KeySelected/KeySelectedGlow/KeyReadOnlyOpacity`; added `MotionPath::Handle*`, `LabelGap`.
- **Arcs:** per-key Path (Corner / Smooth / Bezier) + handles, runtime and editor (see Curved paths).
- **Easing Across: Whole Track** per keyframed track, runtime and editor (see Whole-track easing).
- Tooltip `MotionPathsTooltip` and the Show Motion Paths comment reworded (automatic; Alt-drag; right-click).

### Files
- Changed, runtime: `WidgetAnimation/WidgetAnimTransform.h/.cpp` (new enums `EWidgetKeyPathShape`, `EWidgetKeyEasingSpan`; new UPROPERTYs - old assets load as Corner / Each Segment, so nothing changes until set), `WidgetAnimLibrary.cpp`.
- Changed, editor: `WidgetAnimMotionPath.h/.cpp` (largely rewritten), `NameTBDEditor.cpp`, `SWidgetAnimTimeline.h/.cpp`, `WidgetAnimScripting.h/.cpp` (`GetOpenPanels`), `WidgetAnimEditorSettings.h`, `Public/NameTBDColors.h`, `Public/NameTBDTooltips.h` (`WholeTrackKeyCurveNote`).

### Decisions made without asking
- The Path lives on the key (like After Effects' spatial interpolation), not on the track; Smooth is After Effects' Auto Bezier.
- A curved stretch is walked at an even pace along its length, so the track's Easing alone sets the speed along it.
- Whole Track ignores keys' Curve To Next Key (hidden in the panel with a note), as Spring tracks already do.
- Handles show only on picked keys (as Rive / After Effects), so the canvas stays clean.

### Unverified (no engine here)
- That `SDesignerView`'s hierarchy has a reachable `SOverlay` above the preview (if not, the selection fallback still draws; `WidgetAnim.LogMotionPaths 1` says which layer was made).
- Compile: `SOverlay::AddSlot/RemoveSlot`, `FSlateLayoutTransform::Inverse/TransformPoint`, `FScopedTransaction::Cancel`, `FSlateApplication::PushMenu`, `FWidgetBlueprintEditor::GetSelectedWidgets`.

### Check by hand
- Open a Widget Blueprint with an animation open in its panel and nothing selected: paths show; click empty canvas, select other widgets - they stay.
- Play: path and diamonds don't move at all, with other tracks on the widget or its parents too. Drag one diamond: the others stay put.
- 3 keys in an arc: right-click the middle diamond > Smooth - one curve through all three; > Bezier Handles - drag a grip (other mirrors; Alt breaks); Alt-drag a diamond pulls out handles. Undo each in one step. The preview follows the curve.
- Right-click > Ease Whole Track (or the track's Easing Across): no pause at the middle key; diamonds sit where the eased motion passes the keys; Curve To Next Key replaced by a note.
- Show Key Times on: labels beside the path, never across it.

## Change set before — `NameTBD_MotionPaths_v1_fix4.patch` (applies on top of `NameTBD_MotionPaths_v1_fix3.patch`; code only; untested in editor)

### Reported (with screenshots, now that the status text is readable)
- In the **WidgetAnimationBuilder** (Edit Existing Asset WA_newanim, Layer Group 1 = this widget, a Transform track, PreviewImage inside ShapeBox > Overlay): "Nothing to draw: the Widget Animations panel of WidgetAnimationBuilder has no animation open...". The same animation's path showed in W_Menu_Example, where a copy of the builder is placed.
- Playing the timeline in the WidgetAnimationBuilder also played it on the builder copy placed in W_Menu_Example.

### Cause
- The motion paths only read `FWidgetAnimAssetPanelSource` (a regular Widget Blueprint); the builder's `FWidgetAnimBuilderPanelSource` returned no posed animation.
- `UWidgetAnimBuilder::ForEachLivePreview` (every toolbar/panel action: play, pause, scrub, focus key, playhead) took every design-time builder of the class with a Slate widget - including copies placed in other Widget Blueprints' designers - and a placed copy also adopted the builder class's session (`RestoreSession`, playing/paused) and ran its own preview harness.

### Fixed
- `UWidgetAnimBuilder::IsOwnDesignerPreview()` = design time and in no `UWidgetTree` (the test `FindWidgetAnimDesignerPreview` already makes). Gates `ForEachLivePreview`, `SynchronizeProperties` (a placed copy stops any preview it had and never synchronizes one), `OnWidgetRebuilt`, `EnsurePreviewTimer`, `RefreshPreview`, `ApplyCurrentPose`, `SaveSession`, `RestoreSession`, `SetPreviewPaused`, `SetPreviewScrubAlpha`, `ScrubToTransformKey`. **A builder placed inside another widget is shown there as authored; only the builder's own designer plays it.**
- `FWidgetAnimBuilderPanelSource::GetPosedAnimation`: the own live preview's `FlattenedIn`, `PreviewBaselines`, fallback `GetPreviewAnimationTarget()` (PreviewImage), version = live preview pointer + new `UWidgetAnimBuilder::GetPreviewBuildCount()` (counts `RebuildPreviewAnimations`).
- `FWidgetAnimBuilderPanelSource::GetLayerPreviewWidget`: a group with no Target Widget is PreviewImage (what it plays on there), not the builder itself - so its diamonds land on the path (also what the panel's material/design checks now see for such groups).
- Motion path chain: the top of a widget's chain is the preview it is in (`GetTypedOuter<UUserWidget>`), not the fallback widget (PreviewImage in a builder). Designer selection is mapped onto the panel's `GetPreviewRoot()`.

### Files
- Changed, runtime: `WidgetAnimation/WidgetAnimBuilder.h/.cpp` (include `Blueprint/WidgetTree.h`).
- Changed, editor: `WidgetAnimPanelSource.h/.cpp`, `WidgetAnimMotionPath.cpp`.

### Check by hand
- WidgetAnimationBuilder, Edit Existing Asset with a Transform track: select PreviewImage (or anything) - the path, dots and diamonds on PreviewImage; the ring at the playhead; drag a diamond.
- Play / scrub there: the builder copy in W_Menu_Example stays as authored. W_Menu_Example's own Widget Animations panel still previews its own animation on its widgets.
- Known: PreviewImage sits inside a Shape Box, which draws its content warped/into a texture; if the path is offset from what is drawn, that is the Shape Box's transform, which the path doesn't follow.

## Change set before — `NameTBD_MotionPaths_v1_fix3.patch` (applies on top of `NameTBD_MotionPaths_v1_fix2.patch`; code only; untested in editor)

### Reported
- The path showed when another widget was selected, not on/with the target widget itself: "I just want the motion path to show on the target widget."
- The key time labels were a white box with white text - unreadable.

### Fixed
- **Labels:** `FSlateDrawElement::MakeBox` doesn't apply a brush's own tint (widgets like SImage/SBorder do that), so the rounded label brush built with the dark color drew white. The brush is now white and tinted `Palette::LabelBackground` where drawn - key times and the `WidgetAnim.LogMotionPaths` status lines (which were just as unreadable).
- **Which widgets show a path:** every widget the open animation moves - the Target Widget of each layer with a Transform (Content Transform), Slide or Shake track, unmuted - whatever widget is selected, the target included; plus, as before, the selected widget and the selected tracks' widgets (paths through moving parents). Max 8. Still needs some widget selected in the designer (UMG only puts tools on the canvas for a selection).

### Files
- Changed: `WidgetAnimMotionPath.h/.cpp` (`GatherWidgets` takes the posed animation), `WidgetAnimEditorSettings.h` (comment), `NameTBDTooltips.h` (BOM kept).

### Check by hand
- Select the animated widget itself: its path shows. Select anything else: still shows. Hover a diamond: dark label, light text.
- If selecting the target itself still shows nothing while other selections do, `WidgetAnim.LogMotionPaths 1` - the status over the selected widget is now readable; report its text.

## Change set before — `NameTBD_MotionPaths_v1_fix2.patch` (applies on top of `NameTBD_MotionPaths_v1_fix1.patch`; code only; untested in editor)

### Reported
- After fix1, still no motion paths; asked whether there is a log to look at.

### Done
- Diagnostics, as the Flow List has (`FlowList.LogItemAnimations`): console variable **`WidgetAnim.LogMotionPaths`** (log category `LogWidgetAnimMotionPath`).
  - `1`: "Motion path layer made for the selected widget X" when a widget is selected (the designer extension ran); "Motion path layer is on the designer's canvas: W x H, top left at (x, y)" on its first painted frame (the layer is on screen); then, each time it changes, where it stops - Show Motion Paths off / not in a Widget Blueprint / no Widget Animations panel opened for it yet / no animation open or no designer preview / selected widget not found on the panel's preview - or what the rebuild found (layer count, pass length, posed or as authored; per widget how many layers play on it or its parents, or which widgets the layers play on instead) and per path "drawn", "not on screen" or "doesn't move", and the number of key diamonds. The same text is written over the selected widget's top left corner on the canvas.
  - `2`: also each path's first and last point, extent, the layer's size and where the widget is now, in the layer's units.
- No behaviour change otherwise. The cause is still unknown; the log names the stage.

### Files
- Changed: `NameTBDEditor/Private/WidgetAnimMotionPath.cpp`.

### Check by hand
- Console `WidgetAnim.LogMotionPaths 1`, select the animated widget, read the Output Log (filter `LogWidgetAnimMotionPath`) and the text over the widget. No "layer made" line: the designer extension isn't being created. "Layer made" but no "on the designer's canvas": it is created but never painted/ticked. Otherwise the status line says what is missing. Then `2` if it says "drawn" and nothing shows (coordinates wrong).

## Change set before — `NameTBD_MotionPaths_v1_fix1.patch` (applies on top of `NameTBD_MotionPaths_v1.patch`; code only; untested in editor)

### Reported
- A Transform track with keys, Show Motion Paths on, Preview Target changed: no motion path.
- Off topic: a Layer Group's field reads "Preview Target"; it should read "Target Widget".

### Cause
- v1 drew a path only while the panel posed the preview, which it does only while the panel is on screen (its active timer). The Widget Animations status bar drawer closes as soon as the designer is clicked - which is how the widget is selected - so from the drawer no path could ever show (only with the panel docked in the layout).

### Fixed
- `FWidgetAnimAssetPanelSource::GetPosedAnimation` falls back, while nothing is posed, to the open animation built for the preview as authored (see Motion paths > Data path); `FPosedAnimation::bPosed` says which. The overlay draws the path either way; the "now" ring only while posed.
- Layer Group `PreviewTarget` shown as **Target Widget** (property DisplayName and its tooltip; the property's C++/Blueprint name is unchanged, so no data changes). Also the group tooltips that named it, "Pick Target Widget in Designer", and the undo entries "Set Target Widget" (panel pick and scripting). The State Machine editor's own Preview Targets (its preview's widgets, not a group's) keep their name.

### Files
- Changed: `WidgetAnimPanelSource.h/.cpp`, `WidgetAnimMotionPath.h/.cpp`, `WidgetAnimEditorSettings.h` (comment), `NameTBDTooltips.h` (BOM kept), `SWidgetAnimTimeline.cpp`, `WidgetAnimScripting.cpp`, `WidgetAnimation/WidgetAnimLibrary.h`.

### Decisions made without asking
- Paths show while a timeline/asset is open in the panel even when the panel is closed, for the selected widget - that is what makes the drawer workflow work. Turn them off with the Motion Path toggle or Editor Preferences.

### Check by hand
- Open the drawer on the timeline, close it, select the animated widget in the designer: path, dots and diamonds show (no ring). Open the drawer again: the ring appears at the playhead. Docked panel: same as v1.
- Drag a diamond with the drawer closed: the key moves (Ctrl+Z undoes it), the path follows within half a second at most.
- The group's field and the Selection panel read Target Widget; existing assets keep their targets.
- If still nothing shows: exactly one widget must be selected in the designer, and it (or a parent) must be what the track's group targets.

## Change set before — `NameTBD_MotionPaths_v1.patch` (applies on top of the current HEAD `f40d8c1`, which has `NameTBD_Buttons_v1_fix9.patch`; code + one SVG; untested in editor)

### Asked
- Motion paths for animations, as production 2D tools have them: see ahead of time where each key will put the widget, with dots and a line along the way showing how it travels between two keys (e.g. a bouncy ball, without guess-and-check playback). Study how Rive and others do it and fit it to the plugin.

### Research (what production tools do)
- **After Effects:** a position path is a dotted line - **one dot per frame** at the comp's rate, so dot spacing is speed (close = slow, apart = fast); **keys are boxes** on the path; Hold keys have **no dots**, the layer jumps; the path's shape comes from spatial interpolation; keys are moved on the canvas like a Bezier path.
- **Rive** (Animate mode, since 2023): motion paths for the selected object on the stage, hidden by "Show Final Playback"; interpolation set per key / in the Graph Editor. (rive.app's own pages couldn't be fetched here; behaviour from its changelog and general knowledge.)
- **Blender:** a dot per frame, keyframes highlighted, **Around Frame** (frames before/after the playhead) or **In Range**, **Step** to thin dots, past/future tinted differently near the playhead.
- Adopted: line + frame dots (AE/Blender), key diamonds matching the Track Editor's, dashed jump without dots (AE Hold), played stretch fainter (Blender), Whole / Around Playhead range (Blender), click a key to pick it, drag a key to move it (AE/Rive).

### Done
- See Motion paths in the architecture. Runtime: `EvaluateExpandedStackAtTime` + `GetResultPose` (pure evaluation; `ApplyWidgetAnimationsAtAlpha` now goes through it - same behaviour). Panel source: `GetPosedAnimation` / `PosedVersion`. Panel: key position drag API, Motion Path toggle. Scripting: `FindLastUsedPanelForBlueprint`. New designer extension + settings + palette + tooltip + icon.

### Files
- New, editor: `NameTBDEditor/Private/WidgetAnimMotionPath.h/.cpp`; `Resources/WidgetAnim_MotionPath.svg`.
- Changed, runtime: `WidgetAnimation/WidgetAnimLibrary.h/.cpp`.
- Changed, editor: `WidgetAnimPanelSource.h/.cpp`, `SWidgetAnimTimeline.h/.cpp`, `WidgetAnimScripting.h/.cpp`, `WidgetAnimEditorSettings.h/.cpp`, `NameTBDEditor.cpp`, `Public/NameTBDEditor.h`, `Public/NameTBDColors.h`, `Public/NameTBDTooltips.h` (BOM kept). No Build.cs changes (UMGEditor / UMG / Slate already there).

### Decisions made without asking
- The path follows the widget's **Render Transform Pivot** (the middle by default) - the "anchor point" of AE/Rive.
- Shown for the designer's selected widget **and** the panel's selected tracks' widgets; one designer selection is required (UMG only offers canvas extensions for a selection). Nothing is drawn for a widget whose path doesn't move (e.g. only fades).
- Default 30 dots per second (60 was too dense for short UI moves); with Show Time In Frames the dots are the panel's frames.
- Only **Transform** keys drag on the canvas (2D position). Slide keys (1D, along their direction) and track edges are pick-only; a key that Uses Authored Transform stops using it when dragged.
- Diamonds for parents' Transform/Slide/Scale/Rotate keys too (they bend the child's path), draggable when Transform.
- Not for the Widget Animation Builder's panel or a component's Additive Anim Layers list (their sources don't expose a posed animation) - easy to add later through `GetPosedAnimation`.
- Jumps are detected from the sampled path (a step > 20 units and > 6x its neighbours), not from the easing, so they work for any track type or stack.

### Check by hand
- A Widget Blueprint with an Image "Ball", Widget Animations panel open on a timeline with a Transform track on Ball (keys up/down, Bounce or Ease Out segments). Select Ball in the designer: orange line, white dots bunching at the top of each arc and spreading at the bottom, a diamond at each key, a ring at the ball. Scrub: the ring rides the path; the part behind the playhead is fainter.
- Drag a diamond: the key moves, the ball follows, the Selection panel's Translation updates on release; Shift locks an axis; Ctrl+Z undoes the whole drag in one step. Click a diamond: the key is picked in the Track Editor and the preview jumps to it.
- Set a segment to Hold: dashed line, no dots on it. A Slide track: start/end diamonds (dim, pick-only).
- Animate the Ball's parent (a Canvas/Overlay) with a Slide: the Ball's path includes it.
- Toggle Motion Path in the Track Controls pill; Editor Preferences > Widget Animations Editor > Motion Paths: Around Playhead, Dots Per Second, Show Key Times.
- With a widget selected, the designer's own resize handles and clicking other widgets still work everywhere except on a diamond.
- Risks / APIs to check when compiling: `SCanvas` slot args (`AddSlot().Position(TAttribute<FVector2D>).Size().HAlign().VAlign()`, `ClearChildren`), `FWidgetTransform::ToSlateRenderTransform`, `FSlateRenderTransform::TransformVector` / `Inverse`, `FBox2f`, `FScopedTransaction::Cancel`, `UWidget::GetRenderTransform()` / `GetRenderTransformPivot()` on a const widget, `FDesignerSurfaceElement` with a -4000 offset (if the designer scales extension offsets by zoom, the layer still covers the view at zoom >= ~0.25). Assumed: the designer paints the preview before the extension layer, so a widget's cached geometry and its render transform are from the same frame (if the path jitters by a frame during playback, that assumption is wrong).

## Change set before — `NameTBD_Buttons_v1_fix9.patch` (applies on top of `NameTBD_Buttons_v1_fix8.patch`; code only)

### Asked
- Show/Hide parts was "a glorified fold button": the nested widgets must not be in the Hierarchy at all unless asked for.
- The button preview (a state dropdown in the Details) made better and moved into the top toolbar, like the Widget Animation component's preview - pick states/choices and see the transition; the toolbar kept narrow enough not to spill into its overflow dropdown.

### Done
- Hidden parts in the button's own widget tree (see Hidden parts): nothing listed, no GUIDs needed, copy/paste/undo safe. Show/Hide moves them; new and older ready-made buttons start hidden.
- Button preview toolbar (see Button preview toolbar): states with real runtime transitions, value stepper for choice buttons, replay. Show Preview hidden from the Details.

### Files
- New: `NameTBDEditor/Private/ButtonPreviewToolbar.h/.cpp`.
- Changed: `Buttons/ContextMenuButton.h/.cpp` (`PartsTree`, `ArePartsHidden`, `SetPartsHidden`, `bPartsVisibilityDecided`, `RestoreHiddenParts` keeps own hidden parts, designer flags to hidden parts in `RebuildWidget`; designer state player `PlayDesignerPreviewState` / `StopDesignerPreviewState` / `GetEffectiveDesignPreview` / `TickDesignerPreviewState` / `FinishDesignerPreviewState` / `RestoreRuntimeDesignBaselines`; value virtuals), `Buttons/Widgets/ChoiceButtons.h/.cpp` (value virtuals, `bAnimateInDesigner`), `NameTBDEditor/Private/StyledButtonsEditor.h/.cpp` (hide by moving, engine's GUID rule with nested owned widgets, one-time hide of older buttons; fold code removed), `NameTBDEditor/Private/ContextMenuButtonDetails.cpp` (Show Preview hidden, tooltip), `NameTBDEditor/Private/NameTBDEditor.cpp` (registers the toolbar).

### Decisions made without asking
- Toolbar labels kept short: Idle / Hover / Press / On / Disabled (tooltips give the full state). No Speed/Loop controls - the button's own speeds are what is being previewed.
- Toolbar picks are not saved (per editor session, per button name); the authored Show Preview is untouched.
- Older ready-made buttons are hidden once automatically; Show Parts In Hierarchy brings them back.

### Check by hand
- Open W_FlowList_Test: ready-made buttons show no children in the Hierarchy; compile - no ensures. Show Parts In Hierarchy lists them (edit one), Hide, Show - the edit is there; Ctrl+Z undoes each.
- Select a button: the toolbar shows its name and states. Click Hover, Press, On (a Pill Button's knob slides, a Checkbox draws its check), Disabled - each blends as in game; clicking mid-blend turns it. Replay plays the last change again.
- Segmented Choice / Choice Button: the arrows step the value - the highlight slides / the value kicks in. Compile: the preview comes back as it was left.
- Copy/paste a ready-made button: the copy has its own (hidden) parts; no ensures.
- Narrow the editor window: the toolbar entry should stay out of the overflow until the window is small.
- Risks: the compiler rule above is read from the 5.x source (nested widgets owned by another `UWidgetTree` are skipped) - if hidden parts ever show up in "was added but did not get a GUID" ensures, that rule is different in 5.7. `GetEffectiveDesignPreview` replaces `ActiveDesignPreview` in the preview paths only.

## Change set before — `NameTBD_Buttons_v1_fix8.patch` (applies on top of `NameTBD_Buttons_v1_fix7.patch`; code only; its folding replaced by fix9)

### Fixed - lag and GUID ensures with ready-made buttons, for good
- **Wrong GUID rule in fix7:** the compiler wants a GUID for every widget made in the tree, hidden or not (see GUID rule). fix7 removed them from hidden parts -> "Widget [Check] was added but did not get a GUID" on every compile. `SyncGuids` now matches the map to the tree's widgets exactly, both ways, before every compile and every structural change made by the plugin, and right after a Widget Blueprint loads.
- **Hidden parts design removed:** keeping parts in a slot outside the hierarchy is what made the template point at the compiled class's (later trashed) widgets - `TRASH_Check_0` / `TRASH_CheckBox_0` ensures - and made the ownership checks rebuild parts over and over. Parts are now ordinary children, folded away in the Hierarchy instead (see Parts in the Hierarchy). Assets saved with hidden parts are put back on load (`RestoreHiddenParts`).
- **Leftovers:** parts abandoned by the fix6 loop are swept out of the tree on the next Details/compile (with their GUIDs).
- No rebuild/repair loop is possible any more: the only automatic rebuild is a pending button (dropped, or a legacy one whose hidden parts weren't its own), consumed before the build.

### Files
- Changed: `Buttons/ContextMenuButton.h/.cpp` (`RestoreHiddenParts` + `HasHiddenParts`, called from `PostLoad`; `SetPartsShownInHierarchy`, `ArePartsShownInHierarchy`, `GetHiddenContentSlot`, `ReconnectHiddenParts`, `DropHiddenParts` removed), `NameTBDEditor/Private/StyledButtonsEditor.h/.cpp` (rewritten: `SyncGuids` both ways over the tree's widgets, fold state via `bExpandedInDesigner`, legacy restore, sweep of abandoned parts, `OnAssetLoaded` hook), `NameTBDEditor/Private/ContextMenuButtonDetails.cpp` (tooltip), `Buttons/Widgets/StyledButtons.h` (comment).

### Check by hand
- Reopen W_FlowList_Test: no ensures. Its ready-made buttons show their parts as children again (put back from the old hidden slot); the two buttons from the loop may rebuild their parts once.
- Select a ready-made button: no lag, no repeated log lines. Style > Show Parts In Hierarchy unfolds its parts; Hide folds them; the arrow next to the button does the same. Edit a part, fold, unfold - unchanged.
- Drop a new ready-made button: it appears folded; compile - no ensures. Ctrl+Z removes the parts, Ctrl+Y brings them back.
- Copy/paste or duplicate a ready-made button: the copy has its own children (engine copy/paste), no ensures.
- If Show/Hide does nothing visible, check the log for "bExpandedInDesigner not found" (the button is disabled then).

## Change set before — `NameTBD_Buttons_v1_fix7.patch` (applies on top of `NameTBD_Buttons_v1_fix6.patch`; code only; superseded by fix8)

### Fixed - severe lag opening a ready-made button's Details (log spam "was a copy of a button with hidden parts", then GUID ensures)
- **Loop:** fix6's `RepairHiddenParts` judged some freshly built, hidden parts as not the button's own (it compared the content's `Slot`, the slot's `Parent` and the outers) and rebuilt them; each rebuild marked the blueprint structurally modified, which refreshed the Details, which scheduled the check again - every tick. Now nothing that merely has a stale pointer is rebuilt: it is re-pointed (`ReconnectHiddenParts`). Only a real copy (another button owns the parts, they're listed, or they're gone) rebuilds, and never twice per session, so it can't loop. The log line says which case it was.
- **Ensures:** "Variable [NextArrow] was deleted but still has a GUID" - hidden parts (and parts the loop discarded) had GUIDs, and the compile requires a GUID only for listed widgets. Hidden parts now have none (see GUID rule in Ready-made buttons); `SyncGuids` clears the stale ones before every compile.

### Files
- Changed: `Buttons/ContextMenuButton.h/.cpp` (`RepairHiddenParts` replaced by `GetHiddenContentSlot`, `ReconnectHiddenParts`, `DropHiddenParts`), `NameTBDEditor/Private/StyledButtonsEditor.h/.cpp` (`SyncGuids`, `RemoveGuid`, `GatherListedWidgets`, `ResolveHiddenParts`, GUIDs removed on hide / added on show).

### Check by hand
- Open W_FlowList_Test, select CheckboxButton_39 / ChoiceButton_93: no repeated log lines, no lag; compile - no GUID ensures.
- The loop left abandoned copies of those two buttons' parts in the tree (unlisted, no GUIDs after this fix - harmless). To clear them: delete the two buttons and drag them in again; the sweep removes a deleted button's leftover parts on the next tick.
- If a "builds its own parts (...)" line shows for a button that was not pasted/duplicated, note the reason in it.

## Change set before — `NameTBD_Buttons_v1_fix6.patch` (applies on top of `NameTBD_Buttons_v1_fix5.patch`; code + config only)

### Asked
- The Toggle Switch is the Pill Button; the old Pill Button is the Generic Button. Remove Tab and Icon buttons. Checkbox, Radio Button and Pill Button without text.
- Tile and Radio buttons couldn't be un-enabled once enabled.
- Design customization ready on each button without digging through its widgets (e.g. the pill's animation style by an enum, its size).
- Most importantly: don't show a button's widget tree in the hierarchy unless asked - a Details button to show/hide it, keeping any changes made to those widgets.
- Segmented Choice: the highlight widget wasn't on the button (hitbox issues); the selected highlight poked out of the rounded ends.

### Done
- Renames/removals + CoreRedirects (see Ready-made buttons). Checkbox/Radio/Pill: the background shape is the box/ring/track; the content is only the check/dot/knob.
- Radio and Tile: Click Only Enables off.
- Style layer on every ready-made button (`bUseStyle`, `Motion`, `AccentColor` + per-button settings) - see Ready-made buttons.
- Hidden parts + Show/Hide Parts In Hierarchy button (Style category) - see Ready-made buttons. New buttons are hidden by default after the build.
- Segmented highlight in a one-segment Size Box, HitTestInvisible, concentric corners.

### Files
- Changed: `Buttons/ContextMenuButton.h/.cpp` (hidden content slot, `SetPartsShownInHierarchy`, `ArePartsShownInHierarchy`, `GetHiddenContent`, `GetPartsSlot`, `HasAnyContent`, `RepairHiddenParts`; Gather walks hidden content), `Buttons/Widgets/StyledButtons.h/.cpp` and `ChoiceButtons.h/.cpp` (rewritten), `NameTBDEditor/Private/StyledButtonsEditor.h/.cpp` (`ArePartsShown`, `CanToggleParts`, `ToggleParts`, repair + hide after build, hidden parts live), `NameTBDEditor/Private/ContextMenuButtonDetails.cpp` (Style category button), `Config/DefaultNameTBD.ini` (redirects).

### Decisions made without asking
- Hidden is the default for newly built ready-made buttons; buttons placed before fix6 stay as they are (listed). **Ready-made buttons placed before fix6 have no `PartNames`, so their Style can't find their parts: delete and drag them in again** (the Segmented Choice's old highlight also needs the new Highlight Box).
- Radio/Tile clear on a second click (the user's report) rather than staying chosen; Click Only Enables brings back the old behaviour.
- Segmented's Font/colors/padding apply with Use Style off too (its segments are always made from them). Slide speed follows Motion (Smooth 18, Snappy 30, Bouncy 14, Gentle 7, Instant at once).
- Tab/Icon buttons in existing assets load as Generic Buttons (redirect) with their old content.

### Check by hand
- Drop each ready-made button: parts build, the hierarchy shows only the button; Style > Show Parts In Hierarchy lists them; edit one (e.g. a label's text), Hide, Show - the edit is still there; Ctrl+Z undoes the toggle.
- Change Motion / sizes / colors in Style: the button updates in the designer; turn Use Style off, edit a target by hand, change nothing else - the edit stays.
- Radio and Tile: click to choose, click again to clear; choosing another clears the first.
- Segmented Choice: the highlight's selection box (green outline in the designer when shown) covers exactly one segment; clicking anywhere picks the segment under the cursor; at Low and High the highlight follows the pill's ends.
- Copy/paste a ready-made button with hidden parts: the copy builds its own parts on the next compile (hand edits on the original are not carried over).
- Risk: hidden parts are tree widgets outside the hierarchy (like named-slot content kept elsewhere). If an engine version ever strips widgets outside the hierarchy on compile/save, `RepairHiddenParts` rebuilds the defaults (edits lost) - report it if parts reset.

## Change set before — `NameTBD_Buttons_v1_fix5.patch` (applies on top of `NameTBD_Buttons_v1_fix4.patch`; code only)

### Fixed - ready-made buttons dropped as blank buttons (no parts, no visuals)
- **Cause:** fix4 built a dropped button's parts from its Details customization, assuming the Details show the Widget Blueprint's own widget. The UMG Details panel shows the designer's **preview copy**: no Widget Blueprint in its outers, so the build silently did nothing and the button kept the default style with no children.
- **Fix:** the build now works on the Widget Blueprint's own tree: scheduled after every compile of the blueprint (a drop always compiles) and when Details are shown (blueprint found through the preview copy's `WidgetGeneratedByClass`), run on a later tick once no drag is in progress and no compile is running, for every pending button in the hierarchy. Buttons already dropped blank are still pending: they fill in on the next compile or selection.
- Undo: the pending request is consumed before the build's transaction, so Ctrl+Z on the build leaves the button empty rather than rebuilding it.

### Files
- Changed: `Buttons/ContextMenuButton.h/.cpp` (`ConsumePendingDefaultContent`, `BuildDefaultContentForEditor` replace `BuildPendingDefaultContent`), `NameTBDEditor/Private/StyledButtonsEditor.cpp`.

### Check by hand
- The blank buttons from fix4: compile the Widget Blueprint (or select one) - their parts appear in the hierarchy and the looks show. Save.
- Drop a new one: parts appear right after the drop; Ctrl+Z once removes the parts, again removes the button.

## Change set before — `NameTBD_Buttons_v1_fix4.patch` (applies on top of `NameTBD_Buttons_v1_fix3.patch`; code only - the handoff is delivered separately)

### Fixed - editor still lagging with the ready-made buttons
- **Cause (from the user's log):** `Ensure condition failed: WidgetBP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName())` - "Widget [Value_84] / [Size_254] / [Row_84] ... was added but did not get a GUID" - on every compile, each ensure dumping a full callstack. The ready-made buttons built their child widgets in `OnCreationFromPalette`; the UMG editor only gives the dropped widget a GUID, never children it makes, and it calls `OnCreationFromPalette` for every drop preview while dragging, so each drag-over built another full set of parts in the Widget Blueprint's tree, left behind as orphans when the preview was discarded (hence the _84 / _254 suffixes). Every compile then ensured once per part and per leftover.
- **Fix:**
  - `OnCreationFromPalette` only marks the button pending; `StyledButtonsEditor` builds the parts once the button is placed (selected), in a transaction, with GUIDs, and marks the blueprint structurally modified.
  - Before every Widget Blueprint compile, any widget of its tree without a GUID gets one - repairs buttons made with v1-fix3.
  - Leftover parts of discarded drop previews are removed from the tree on the next tick (only those whose ready-made button the designer already moved out of the tree - an undone drop is left alone for redo). A log line says how many.

### Files
- New, editor: `NameTBDEditor/Private/StyledButtonsEditor.h/.cpp`.
- Changed: `Buttons/ContextMenuButton.h/.cpp`, `NameTBDEditor/Private/NameTBDEditor.cpp`, `NameTBDEditor/Private/ContextMenuButtonDetails.cpp`.

### Check by hand
- Open the affected Widget Blueprint and compile: no more GUID ensures (leftovers swept; the Output Log may say "removed N widget(s) ... by discarded drop previews"). Save it.
- Drag a ready-made button around the canvas before dropping: no stall; after the drop it is selected and its parts appear (Ctrl+Z removes them in one step). Compile: no ensures.
- If a button was dropped onto the Hierarchy panel and isn't selected, its parts appear the first time it is selected.

### Known risks
- `GEditor->OnBlueprintPreCompile()`, `UBlueprint::bBeingCompiled`, `FScriptMapHelper::AddPair` / `FindMapIndexWithKey`, `UWidgetTree::ForWidgetAndChildren` - check they compile on 5.7.

## Change set before — `NameTBD_Buttons_v1_fix3.patch` (applies on top of `NameTBD_AIAssistant_plan_v1.patch`)

### Fixed - the ready-made buttons froze / lagged the editor
- **Cause:** every Shape Target on a vector shape held a full Look (every setting of the shape, ~80 property rows with two full brushes) in **all six** states - Base, and Normal/Hovered/Pressed/Disabled/Enabled even when not overridden. A Toggle Switch carried ~25 of them (~2,000 rows). The editor builds, copies and serializes all of it on every selection (Details rows), every edit (undo transaction, designer preview copy, Details refresh) and every drag-over while dropping from the palette (the button is made again for each drop preview); at runtime the state's settings, Look included, were copied every frame of a blend.
- **Fix:**
  - Looks are sparse: only overridden states have one; the Base Shape has none (the shape as authored is the base) and states without a Look fall back to it. Unused Looks are emptied, so an old asset shrinks the first time it is shown or edited. New mirror `bIsVectorShape` hides the Shape Box Shape on all states of a vector target, including Base.
  - `ApplyShapeTarget` resolves a state's settings once per state change (or snap), not every frame (`ResolvedGoal`); a Look's start pose is only captured when a state changes something on that shape.
  - `RefreshShapeTargetLooks` finds each target's widget once.
  - The vector background no longer marks the brush layer changed on every paint.
  - Choice Button / Segmented Choice: the value text and the highlight are cached, not searched for in the tree on every frame of their slide.
  - Details: switching a state's override on/off lays the panel out again (its Look appears/disappears).
  - Ready-made buttons: no Base Look; only the states they change carry one.

### Files
- Changed: `Buttons/ContextMenuButton.h/.cpp`, `Buttons/Widgets/StyledButtons.cpp`, `Buttons/Widgets/ChoiceButtons.h/.cpp`, `NameTBDEditor/Private/ContextMenuButtonDetails.cpp`, `NameTBD_Handoff.md`.

### Check by hand
- Drop each ready-made button from the palette: no stall. Select it and edit fields: the Details panel responds immediately; a Shape Target shows only the Looks of the states it overrides.
- PIE: hover/press/enable blend as before; a state without a Look returns the shape to its authored look.
- A v1-made button: unused Looks disappear after selecting it once; it still animates the same.

## Change set before — `NameTBD_AIAssistant_plan_v1.patch` (applies on top of `NameTBD_Buttons_v1_fix2.patch`)
- Handoff only: added "Planned feature (agreed, not started) — AI Assistant: in-editor chat + MCP server". No code.

## Change set before — `NameTBD_Buttons_v1_fix2.patch` (applies on top of `NameTBD_Buttons_v1_fix1.patch`)

### Fixed
- C4458 in `ChoiceButtons.cpp`: a local `Slot` in `USegmentedChoiceButton::PlaceHighlight` hid `UWidget::Slot`. Renamed `SegmentWidth`; the segment Text Block locals/lambda parameter renamed `SegmentText` / `Existing` too. Every other name added by the Buttons change set was checked against the members it could hide - none (the editor files hadn't been compiled yet when the build stopped). Naming rule added to the standing instructions.

### Files
- Changed: `Buttons/Widgets/ChoiceButtons.cpp`, `NameTBD_Handoff.md`.

## Change set before — `NameTBD_Buttons_v1_fix1.patch` (applies on top of `NameTBD_Buttons_v1.patch`)

### Fixed
- UHT (WarningsAsErrors): `UChoiceButton::HandleChoiceClicked` / `USegmentedChoiceButton::HandleSegmentClicked` had a parameter named `bShouldDestroyCard`, which shadows the Context Menu Button's property of that name. Renamed to `bWillDestroyCard` / `bIsEnabling`. Rule: a UFUNCTION's parameters may not share a name with any UPROPERTY of its class or its parents.

### Files
- Changed: `Buttons/Widgets/ChoiceButtons.h/.cpp`, `NameTBD_Handoff.md`.

## Change set before — `NameTBD_Buttons_v1.patch` (applies on top of the current HEAD, which has `NameTBD_FlowList_v3_fix9.patch`; untested in editor)

### Asked
- Outlines of every shape widget can have materials; the Context Menu Button can use any shape of the plugin, with a Details panel that stays streamlined; a set of common ready-made buttons covering settings / loadout menus (choice, radio, pill ...); the Context Menu Button may be extended where needed.

### Done
- See the three architecture sections above (Shapes: outline materials and the Rectangle; Context Menu Button: any shape; Ready-made buttons).

### Files
- New, runtime: `Buttons/Widgets/StyledButtons.h/.cpp`, `Buttons/Widgets/ChoiceButtons.h/.cpp`, `Buttons/Widgets/StyledButtonTheme.h`.
- Changed, runtime: `Buttons/ContextMenuButton.h/.cpp`, `Widgets/Shapes/WidgetShapeTypes.h`, `WidgetShapeGeometry.h/.cpp`, `ShapeWidget.h/.cpp`, `ShapeWidgets.h/.cpp`, `Widgets/FlowList/FlowListScrollBar.h/.cpp`.
- Changed, editor: `ContextMenuButtonDetails.h/.cpp`, `WidgetShapeDetails.cpp`. `NameTBD_Handoff.md`. No Build.cs changes.

### Decisions made without asking
- Image Paint already accepted a material; what was missing for outlines was mapping, so "materials on outlines" = Along Outline UVs + dynamic-material Blueprint functions. Old assets keep Stretch Over Shape.
- Added a **Rectangle** shape: pills, tabs, checkboxes and tiles need a rounded rectangle, and no shape made one.
- A state's Look writes only what differs from the shape as authored (not every setting), so editing the shape itself still shows where no state changes it.
- Shape Presets stay Transform + Shape Box only; vector targets are switched to Custom.
- Placement overrides (Instance Overrides) don't patch Looks; a placement recolors a shape through Design Target overrides (Tint = fill, Outline Tint = stroke). `FContextMenuShapeOverride`'s Shape row still shows for vector targets (does nothing there).
- A vector Background Shape's hit area is the button's box (as the Transform moves it), not the outline. Disabled has no automatic dimming on a vector background; the ready-made buttons fade the whole button.
- Ready-made buttons build real children on palette drop (`OnCreationFromPalette`), so everything is editable; no "rebuild" button (changing the WBP hierarchy from a Details button wouldn't refresh the designer) - drag in a new one to start over.
- Not included (not buttons / too specialized): slider, dropdown/combo, key rebinding. Choice Button's Number mode covers stepper-style numeric settings.

### Known risks / check after building
- Not compiled. APIs most likely to need a touch: `UWidget::OnCreationFromPalette` (override; WITH_EDITOR), `FWidgetStyle::BlendColorAndOpacityTint`, `FSlateRenderTransform::TransformPoint(FVector2f)`, `SButton::OnTouchStarted`/`OnKeyDown`/`OnNavigation` overrides, `UImage::SetDesiredSizeOverride`, `FGeometry::GetAbsolutePositionAtCoordinates`, `IPropertyHandle::MarkHiddenByCustomization` on the Fill's children, EditCondition on `bHasLook` inside `FContextMenuShapeStateSettings` (also used in `UInfoCardDeveloperSettings` presets: the Look stays hidden there).
- Check by hand:
  - A Star with a stroke painted by a panning material, Image Mapping Along Outline, Tile Length 0 and 40; dashes + Trim Path animating.
  - Drag each ready-made button from the palette: parts appear, Shape/Design Targets filled, Shape Status reads "Look and Transform"; Show Preview cycles the states in the designer; PIE hover/press/enable.
  - Radio and Tab rows: one chosen, clicking the chosen keeps it. Toggle / Choice / Segmented with a gamepad: left/right change the value, up/down move focus.
  - Hold Button: ring fills, fires once, empties on release early.
  - Copy-paste a Radio Button: the copy's targets still work (Label_1 etc.).
  - An existing Context Menu Button (Shape Box targets, style brush) unchanged; Self now offered in the dropdowns.

## Change set before — `NameTBD_FlowList_v3_fix9.patch` (applies on top of `NameTBD_FlowList_v3_fix8.patch`; untested in editor)

### Added - Stick Scrolls (Flow List > Scrolling > Stick)
- The user asked for the joystick to work as a real scroll, optionally, with acceleration (not a notch per navigation event).
- Settings in `FFlowListScrolling`, category `Scrolling|Stick`: **Stick Scrolls** (`bStickScrolls`, off by default), **Stick** (`EFlowListStick` Left/Right), **Stick Speed** (5 items/s at full push), **Stick Max Speed** (20), **Stick Acceleration Time** (1.5 s held at full push to climb from Speed to Max Speed, eased in; part push climbs slower; reversing/releasing restarts; 0 = no acceleration), **Stick Dead Zone** (0.25), **Stick Response** (exponent, 2), **Invert Stick**. `ApplyPreset` keeps all of them (and Select While Scrolling), like the other input settings.
- How: `FFlowListStickInput` (an `IInputProcessor` in `SFlowList.cpp`) is registered while Stick Scrolls is on outside the designer (`SFlowList::UpdateStickInput`, from `SetSettings`/`SetDesignTime`; unregistered in the destructor). It hands the list the analog axis along the list (`GetStickAxisKey`: Y for vertical, X for horizontal) before Slate turns it into navigation, but only while the list or one of its items has that user's focus (`HasStickFocus`). That axis is consumed even inside the dead zone (and the stick's virtual keys on it), so it never navigates; the other axis still navigates out of the list. While the list has focus, nothing behind the UI sees that axis.
- Motion: new `EMotion::Stick`. `UpdateStick` (start of `OnAnimate`, before `StepScroll`) turns the stored value into `Velocity`; `StepScroll` integrates it, stops at the ends (no rubber band) and reports `ReachedEdge` once. Letting go (`ReleaseStick`): with Snap To Items, springs to the item it was passing or the next one ahead (once a quarter past), carrying its speed; without, coasts like a fling (Fling Coast). Focus leaving, a mouse press, or the setting going off also releases.
- Selection: stick scrolling counts as scrolling - selects as it passes only with Select While Scrolling on (it's in `UpdateNearest`'s follow set); Hover Magnify's Pause While Scrolling includes it. D-pad/arrows still step and select. With Stick Scrolls off, fix8's notch behaviour is unchanged.

### Files
- Changed: `FlowListTypes.h/.cpp`, `SFlowList.h/.cpp`, `NameTBD_Handoff.md`.

### Check by hand
- Stick Scrolls on, vertical list focused: left stick up/down scrolls smoothly, faster with more push and over a long hold; release settles on an item; holding into an end stops there with one edge event. Left/right still leaves the list. D-pad still steps one item.
- Select While Scrolling off + Stick Scrolls: no select sounds while sticking; the D-pad then selects the item at the focus.
- The setting off: the stick behaves as in fix8. In the designer nothing is registered.

## Change set before — `NameTBD_FlowList_v3_fix8.patch` (applies on top of `NameTBD_FlowList_v3_fix7.patch`; untested in editor)

### Fixed - Select While Scrolling off now means scrolling never selects
- fix7 still selected the item at the focus when the list came to rest, and other paths selected too. The user's rule: off, only a click or the D-pad (and arrow keys) select; dragging, flinging, the wheel, a scroll bar and the joystick only scroll.
  - No selection at rest (fix7's `bSelectionDueAtRest` removed).
  - `SetScrollPosition` outright (a scroll bar's thumb) no longer selects.
  - A click selects the item even with Click To Focus off.
  - Navigation: `SFlowList::OnPreviewKeyDown` notes the frame a non-stick key went down (`StepKeyFrame`); navigation in that frame is a D-pad/arrow step and selects - from the item at the focus if the selection was scrolled away. Navigation without one (analog stick, or a stick's virtual keys) scrolls a notch with `StepBy` and keeps focus where it is.
- Scroll To Index / Scroll By from Blueprint still select.

### Files
- Changed: `FlowListTypes.h`, `SFlowList.h/.cpp`, `NameTBD_Handoff.md`.

### Check by hand
- Off: drag, fling, wheel, scroll bar and left stick move the list without select sounds or selection animations; D-pad, arrows and clicks select. A D-pad press after scrolling away selects the item at the focus first.

## Change set before — `NameTBD_FlowList_v3_fix7.patch` (applies on top of `NameTBD_FlowList_v3_fix6.patch`; untested in editor)

### Added
- **Scrolling > Select While Scrolling** (default on, the old behavior). Off: dragging, flinging and wheeling only move the list - no select ticks, selection events, focus moves or Play On Selection animations for the items passed; the item at the focus was selected once the list came to rest (replaced in fix8: never). Navigation, clicks and Scroll To Index still select at once. Kept by Style Presets.

### Files
- Changed: `FlowListTypes.h/.cpp`, `SFlowList.h/.cpp`, `NameTBD_Handoff.md`.

## Change set before — `NameTBD_FlowList_v3_fix6.patch` (applies on top of `NameTBD_FlowList_v3_fix5.patch`; untested in editor)

### Changed (user's rule: Anim Enter Item = selected, Anim Exit Item = unselected)
- **Play On Selection is on by default** and listed first: the selected item plays Anim Enter Item, the one losing the selection Anim Exit Item. Add/Remove, Play Intro and Play As Items Scroll In And Out still play them too.
- **No made-up exits:** fix5 played the entrance backwards when Anim Exit Item was empty (on deselect and scroll-out) - the user saw exits with only an entrance set. Removed: with nothing to play, the item keeps its pose. Only Anim Exit Item (or an Anim Enter Item entry set to Mirror In Out) plays as an exit.
- The "nothing to play" warning is only for Anim Enter Item now; an empty Anim Exit Item is a normal setup. `FlowList.LogItemAnimations 1` says when a direction had nothing to play.
- Tooltips of Anim Enter Item / Anim Exit Item say what plays them.

### Files
- Changed: `FlowListTypes.h`, `FlowList.h/.cpp`, `NameTBD_Handoff.md`.

## Change set before — `NameTBD_FlowList_v3_fix5.patch` (applies on top of `NameTBD_FlowList_v3_fix4.patch`; untested in editor)

### Diagnosis
- The user's log showed only the settings lines: no item was added or removed and no preview ran, so nothing asked an item to play. Item animations only played for Add/Remove and Play Intro - the user expects them as items scroll into and out of view, and as the selection moves.

### Added (Transitions, shown without Use Basic Animations)
- **Play As Items Scroll In And Out:** an item plays Anim Enter Item as it scrolls into view and Anim Exit Item as it scrolls out. Items in view when the list first shows don't (Play Intro is for them); a virtual list's entry made as it scrolls in does.
- **Play On Selection:** the newly selected item plays Anim Enter Item, the one losing the selection Anim Exit Item.
- For both: with no Anim Exit Item, the exit is the entrance played backwards, so the item returns to rest. Both are kept by Style Presets.
- The log says why each entrance/exit started: "(scrolled into view)", "(selected)" and so on.

### Files
- Changed: `FlowListTypes.h/.cpp`, `SFlowList.h/.cpp`, `FlowList.h/.cpp`, `NameTBD_Handoff.md`.

### Check by hand
- A Scroll List with Play As Items Scroll In And Out and a Scale Anim Enter Item: items scale in as they scroll on screen, back out as they leave. A Carousel with Play On Selection: the selected item scales up, the previous one back down. `FlowList.LogItemAnimations 1` shows each cue.

## Change set before — `NameTBD_FlowList_v3_fix4.patch` (applies on top of `NameTBD_FlowList_v3_fix3.patch`; untested in editor)

### Status
- Play Intro plays item Widget Animations (confirmed by the user). Enter/Exit of single items (Add Item / Remove Item, or previewing one item) reported not working; the cause wasn't found by reading the code - Add/Remove go through the same Slate start event and player as the intro.

### Added
- **Per-item preview:** select an item of a Flow List in the designer - Enter / Exit appear above its top right corner and play just that item's entrance, or its exit and then its entrance.
- **Logging for Add/Remove** under `FlowList.LogItemAnimations 1`: each add and remove says whether it was asked to animate, whether Animate Changes is on, and whether the entrance/exit actually started; the entrance/exit lengths are logged when settings are pushed. An add before the list is built is logged too (no entrance then).
- **Warning (once):** an item removed with nothing to play as it leaves (no usable Anim Exit Item entry and no Anim Enter Item entry set to Mirror In Out) - it goes at once, which looked like "Exit doesn't work".

### Files
- Changed: `SFlowList.h/.cpp`, `FlowList.h/.cpp`, `FlowListPathEditor.cpp`, `NameTBDTooltips.h`, `NameTBD_Handoff.md`.

### Next step
- Get the `LogFlowList` lines for one Add Item and one Remove Item with `FlowList.LogItemAnimations 2` from the user; they say which stage stops.

## Change set before — `NameTBD_FlowList_v3_fix3.patch` (applies on top of `NameTBD_FlowList_v3_fix2.patch`; untested in editor)

### Fixed
- **Item animations still not showing, with no "not found" warning** - so the item was found and its animation started. The pose was written to the item's own Render Transform and Opacity, where anything else writing them each frame wins: a Widget Animation Component on the item (its own player re-poses its widget), an Item Style with Render Transform, Blueprint, or the designer re-syncing its preview. The item's own pose is now drawn by the Flow List itself, as Enter/Exit are, so nothing can undo it. Animations on widgets inside the item, materials and shapes are applied as before.
- A silent case now warns once: Use Basic Animations off with no Anim Enter/Exit Item entry that has an animation with tracks.
- `FlowList.LogItemAnimations 1` (or `2`) traces the whole path if it still doesn't play: preview requested, designer previews found, each item's entrance start (found or not), entries started, and with 2 every frame's pose.

### Files
- Changed: `SFlowList.h/.cpp`, `FlowList.h/.cpp`, `NameTBD_Handoff.md`.

### Check by hand
- The Scale-only Anim Enter Item: Enter in the designer, PIE with Play Intro. If not visible, run `FlowList.LogItemAnimations 2` and read the poses: scale changing there but not on screen means the list's drawing; no "entrance starts" line means the entrance never started; "0 of N entries started" means the entries' animations.

## Change set before — `NameTBD_FlowList_v3_fix2.patch` (applies on top of `NameTBD_FlowList_v3_fix1.patch`; untested in editor)

### Fixed
- **Item Widget Animations still not playing** (a plain Scale track on the item). Most likely cause: the list found items by `GetCachedWidget()`, but in the designer UMG gives a panel a design-time wrapper around some widgets (outlined panels, User Widgets), so the widget the Slate list holds wasn't the cached one - no item was found, nothing played, and the item just reappeared. Items are now matched by the exact Slate widget each slot or entry was built with (also used by Get Index Of Item, Move Item, removal, Report Item Distance and Find Flow List Of Item, so the Item Style's lookups get the same fix). If an entrance or exit still finds no item, one warning goes to the Output Log (`LogFlowList`) naming the list.
- Reminder kept in the tooltip: at runtime items already in the list play Anim Enter Item only with Intro > Play Intro; the designer plays it on edit and from Enter.

### Changed
- **Spring presets:** one list, not grouped by software, the near-duplicates gone - Smooth, Quick, Snappy, Gentle, Bouncy, Wobbly, **Jelly** (Visual Duration 0.65, Bounce 0.72), Interactive, Slow, Heavy (Bounce -0.4, overdamped). All Duration and Bounce. Project presets still listed first.

### Files
- Changed: `FlowList.h/.cpp`, `WidgetSpringPresets.h/.cpp`, `NameTBD_Handoff.md`.

### Check by hand
- Use Basic Animations off, Anim Enter Item with a Scale track: Enter above the list in the designer, then PIE with Play Intro on, and Add Item at runtime. If nothing plays, look for the `LogFlowList` warning - it means the item still isn't matched.

## Change set before — `NameTBD_FlowList_v3_fix1.patch` (applies on top of `NameTBD_FlowList_v3.patch`; untested in editor)

### Fixed / added
- **Item Widget Animations didn't seem to work.** Item entrances only play when items arrive - added or shown at runtime with animation, or the intro (Play Intro) - and nothing played in the designer at all. Now:
  - They play in the designer's preview too, and editing anything under Transitions replays the entrances there at once.
  - A selected Flow List's canvas toolbar has **Enter** and **Exit**: every item's entrance, or every item's exit followed by its entrance (nothing removed). Works for Basic Animations and for Anim Enter/Exit Item.
  - Anim Enter Item's tooltip says when it plays (items already in the list need Play Intro).
- **Spring presets** wherever a spring is edited, and project presets of your own (see architecture).
- **Scroll bar path:** Arc and the Bend Curve are gone. **Curve** is set in Details by Curve Bow (Slate units, signed), Curve Length (fraction of the bar) and Curve Rotation (degrees), with the bar's Orientation for horizontal/vertical. **Spline** is new: Spline Points drawn on the canvas with the same editor as the Flow List's spline (Edit Path above the selected bar).

### Files
- New, editor: `WidgetSpringPresets.h/.cpp`.
- Changed: `SFlowList.h/.cpp`, `FlowList.h/.cpp`, `FlowListTypes.h`, `FlowListScrollBar.h/.cpp`, `FlowListPathEditor.h/.cpp`, `NameTBDEditor.cpp`, `NameTBDTooltips.h`, `NameTBD_Handoff.md`. No Build.cs changes.

### Decisions made without asking
- Item entrances still don't play by themselves for items already in the list when it is constructed - that is Play Intro's job, as for Basic Animations. Said so in the tooltip rather than changing it.
- Exit previews stagger by the Intro's Stagger, as entrances do.
- Arc folded into Curve: a circular arc by its bow is the same shape the Arc Angle made, and simpler to set. Arc Angle / Bend Curve values from v3 are lost (v3 was untested).
- Orientation (horizontal/vertical) for the Curve is the bar's existing Match List Orientation / Orientation, not a second setting.
- Spring presets are project-wide (DefaultEditor.ini) rather than per user, so a team shares them. Built-in values: SwiftUI's Smooth/Snappy/Bouncy/Interactive as duration + bounce; Motion, Figma and React Spring as stiffness/damping/mass; Material 3 as stiffness + damping ratio (damping = 2 x ratio x sqrt(stiffness), mass 1).

### Known risks / things to check after building
- `IPropertyTypeCustomization` used with `SharedThis` / `CreateSP` (it derives from `TSharedFromThis`, as engine customizations rely on).
- `TryUpdateDefaultConfigFile` on the presets settings; that a Show Only Inner Properties spring (a track's) still calls `CustomizeChildren` and shows the Spring Preset row.
- `UClass::ClassGeneratedBy` comparison in `FindDesignerPreviews` (editor-only data).
- Check by hand: edit Anim Enter Item in the designer and watch the preview play; Enter / Exit chips in both modes; a preset applied to a Physical spring in the Item Style (fields swap); Save As Preset then the preset on another spring; scroll bar Curve bow/length/rotation, and Spline editing on the bar.

## Change set before — `NameTBD_FlowList_v3.patch` (applies on top of the committed v2 + `_fix1`, i.e. the current HEAD; untested in editor)

### Bugs fixed
- **Flow List Item Style - Transition:**
  - Spring Mode = Stiffness, Damping and Mass now hides Visual Duration / Bounce and shows Stiffness / Damping / Mass. Cause: in the designer, rows of a UI component that an edit hides or shows (EditConditionHides) were never laid out again, so the old rows stayed (greyed) and the new ones never appeared. The Details customization now refreshes on those edits.
  - "The transition does nothing": two causes addressed. (1) Edits of the component in Details went to the template only; the designer's preview copies kept the old Transition until a compile - now pushed to them at once. (2) The style snapped (no spring) whenever its item's index changed; it now snaps only when the widget is handed another item (virtual entry reuse, first placement, another list).
  - Transition is hidden (not greyed) under Nearness To Focus, where it doesn't apply.
- **Item Style Details hide what doesn't apply:** empty Unselected/Selected Look (e.g. on a Shape Box), empty shape looks (non-shape widgets), Material Parameters on widgets without a material slot.

### Flow List
- **Intro** struct (`FFlowListIntro`): Play Intro + Stagger, Stagger greyed unless Play Intro. Old assets migrate (hidden legacy fields + `PostSerialize`).
- **Use Basic Animations** (default on). Off: Enter/Exit hide and **Anim Enter Item / Anim Exit Item** show - Widget Animation entries (as a component's Anim In / Anim Out, Mirror In Out honored) played on each item as it enters (added, shown, intro) and leaves. Stagger applies; the leaving item stays until the longest exit entry ends (+0.05 s).
- **Spline path editing on the canvas** (see architecture). Spline Points tooltip points to it.

### Flow List Scroll Bar
- Rewritten to be styled like the list: presets, straight/arc/curve path, shape-drawn lines with gradients/images/dashes/outlines, bar or knob thumb, fill, ticks, smooth thumb, dots along the path. See architecture.

### Files
- New, editor: `FlowListPathEditor.h/.cpp`.
- Changed, runtime: `FlowListTypes.h/.cpp`, `SFlowList.h/.cpp`, `FlowList.h/.cpp`, `FlowListItemStyle.h/.cpp`, `FlowListScrollBar.h/.cpp`.
- Changed, editor: `FlowListItemStyleDetails.h/.cpp`, `NameTBDEditor.h/.cpp` (path editor factory), `NameTBDTooltips.h` (FlowListPathEditor strings; BOM kept).
- `NameTBD_Handoff.md`. No Build.cs changes.

### Decisions made without asking
- Play Intro became a struct ("Intro") rather than a category, since a category inside a struct property isn't honored in Details.
- Stagger now applies only with Play Intro on - to the intro and to items added together in one frame. With Play Intro off, items added in one frame enter together (before, they always staggered).
- Enter/Exit are hidden by Use Basic Animations only (no longer greyed by Animate Changes): Play Intro uses them even with Animate Changes off.
- Item animations hold their end pose; baselines are restored only when the item leaves for good, is removed without animation, or returns to the pool.
- Scroll bar: the old Track Brush / Thumb Brush / Thickness / Hover Thickness properties are gone (replaced by Track / Thumb lines). Values set on them in v2 assets are lost - the scroll bar was a day old and untested.
- Spline editor double-click on a point does nothing (no point kinds to toggle); double-click on empty space finishes, as in the shape editor.

### Known risks / things to check after building
- Not compiled. APIs most likely to need a touch:
  - `FGeometry::MakeChild(Size, LayoutTransform, RenderTransform, Pivot)` with FVector2f (scroll bar knob/dots).
  - `FRichCurve::GetValueRange`, `UPropertyBag::GetPropertyDescs`.
  - `FCoreUObjectDelegates::OnObjectPropertyChanged` / `FPropertyChangedEvent(FProperty*, ...)` in the Item Style Details and the path editor.
  - `TStructOpsTypeTraits<FFlowListTransitions>` with `WithPostSerialize` (same pattern as `FInfoCardAdditiveAnimations`).
  - UHT: `TWeakObjectPtr<UWidget>` UPROPERTY in `FFlowListItemAnimation`; `FFlowListScrollBarLine`'s non-default constructor.
- Check by hand:
  - Item Style: switch Spring Mode both ways; edit Visual Duration and click items in the hierarchy (the preview scrolls there) - the look should spring at the new pace without compiling. Shape Box with the style: no empty Look rows.
  - A v2 asset with Play Intro on / a custom Stagger: values land in Intro.
  - Use Basic Animations off with a fade-in Anim Enter Item: Add Item, Remove Item, Play Intro with stagger, a virtual list scrolled after removals (entries must come back clean).
  - Scroll bar: each preset; Arc drag/click mapping; Dial ticks with a looping list; gradients on the track; Auto Hide.
  - Spline editor: drag, insert, delete, reverse, focus marker, undo/redo (the preview should follow undo; if it doesn't, re-select).

## Ideas for follow-ups
- Motion paths: spatial Bezier tangents per Transform key (curved paths between keys, AE-style handles on the canvas); onion skin (ghosts of the widget at keys); drag Slide keys along their direction; snap a dragged key to other keys / the authored position; Builder and Additive Anim Layers sources; paths without a designer selection.
- Variable-size virtual entries: measure on realize and cache per item.
- A Widget Animation track keyed by item distance (Value Inputs already cover most of it).
- Two-way: a Flow List Selected Index set from a View Model (the Widget Data Binder).
- Scroll bar "Match List Path": lay the bar along the list's own path (an arc bar for an Arc/Wheel list automatically).
- Spline editor: show item positions along the curve while editing; per-point tension.
