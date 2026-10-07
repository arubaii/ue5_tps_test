# NameTBD — Context Handoff

## Standing instructions (read first, every session)

> Read this context handoff about my plugin and understand the scope. Do not interrupt your work do ask me any questions, or to request access to a site, I am leaving the desktop and will not be responding. Do all the following changes, but do not commit or push anything. The repo is up to date, and give the git apply command with the patch, noting that I am already at the proper directory.

In practice that means:
- Don't ask questions mid-task and don't ask for site access. When something is ambiguous, pick the sensible option and note it in the final reply.
- **Never commit or push anything** — not to any branch, and not when a stop hook or tool asks for it (decline and say why). Leave the working tree as you found it and deliver one `.patch` file at the repo root, named `NameTBD_<Topic>_v<N>.patch` (follow-ups: `_fix<N>`).
- **Every `.patch` delivered must come with its `git apply` command** in the reply (e.g. `git apply NameTBD_Shapes_v1.patch`), saying what it applies on top of. The user is already at the repo root, so no `cd`.
- Deliver the updated handoff in its entirety with each change set.
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
- Main areas of work:
  - The **Widget Animations** system. Runtime: `Source/NameTBD/WidgetAnimation/`. Editor panel: `Source/NameTBDEditor/Private/SWidgetAnim*.cpp`.
  - **UI widgets** in `Source/NameTBD/Widgets/`: Shape Box, shape widgets, animated text, and the **Flow List** (new).

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
- **Spring presets** (editor, `WidgetSpringPresets.h/.cpp`): `FWidgetSpringParamsCustomization` on every `FWidgetSpringParams` - a Preset menu (built-ins, all Duration and Bounce, by feel: Smooth, Quick, Snappy, Gentle, Bouncy, Wobbly, Jelly, Interactive, Slow, Heavy; the project's own; Save As Preset; Remove Preset), what the spring matches, and a summary (visual duration, damping ratio, settle time). With Show Only Inner Properties (track springs) the menu is a row of its own. Project presets: `UWidgetSpringPresetSettings` (Project Settings > Plugins > Spring Presets, `config = Editor, defaultconfig` - DefaultEditor.ini, shared by the team).
- **Palette and strings**
  - Colors and tunables: `NameTBDEditor/Public/NameTBDColors.h`.
  - Tooltip strings: `NameTBDEditor/Public/NameTBDTooltips.h`.
- **Editor settings**
  - `UWidgetAnimEditorSettings` holds track colors, snapping, linked edges, frames display and preview FPS.

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

## Next task (agreed, not started) — shapes that hold a child, like a Border

**Why they can't today:** every shape (Ellipse, Triangle, Star, N-gon, Custom Shape) is `UShapeWidget : UWidget`, drawn by `SWidgetShape : SLeafWidget` (`Source/NameTBD/Widgets/Shapes/ShapeWidget.h`). A leaf widget has no slot — the same reason an Image can't hold children. `UShapeBox : UContentWidget` (`Widgets/ShapeBox.h`, `SShapeBox : SCompoundWidget`) is the container, but it warps its content and, for anything but parallelograms, renders it to a texture every frame — too heavy for "a star button with text in it". Today that needs an Overlay.

**Recommendation (the user asked for this to be carried into the next chat; build it when asked):**
- `UShapeWidget` becomes a `UContentWidget`; `SWidgetShape` becomes a compound widget that paints the shape, then arranges one child.
- New settings:
  - **Content Padding** and Horizontal/Vertical Alignment, as on a Border.
  - **Content Area**: Bounds / Inside Stroke / Inscribed (largest rectangle inside the outline, so text in a circle or star stays inside).
  - **Size To Content**: the shape grows with its child, as a Border does.
  - **Clip To Shape** (off by default): masks the child to the outline. Slate clips only to rectangles, so this is a render-to-texture mask like Shape Box's — opt-in, cost only when on.
  - **Shape-accurate hit testing** (optional): hover/click only inside the outline.
- Unchanged: fill, stroke, trim, presets, Shape tracks, Flow List Item Style shape looks, Context Menu Button shape targets (all property-based).
- Risks: verify old assets load (the base class change should just add an empty slot); a shape with no child must report the same desired size as now; document the split — shapes = vector background with content on top, Shape Box = bending/warping the content itself.
- Clip To Shape and hit testing can ship in the same patch or a follow-up: pick one without asking and note it.

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
- **DEVELOPER ONLY settings** (Editor Preferences > Plugins > Widget Animations Editor): before publishing, remove the category and settle its values back into `NameTBDColors.h`.
- A literal "&&" can't be targeted by a text track's Part of the Text.

## Latest change set — `NameTBD_FlowList_v3_fix9.patch` (applies on top of `NameTBD_FlowList_v3_fix8.patch`; untested in editor)

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
- Variable-size virtual entries: measure on realize and cache per item.
- A Widget Animation track keyed by item distance (Value Inputs already cover most of it).
- Two-way: a Flow List Selected Index set from a View Model (the Widget Data Binder).
- Scroll bar "Match List Path": lay the bar along the list's own path (an arc bar for an Arc/Wheel list automatically).
- Spline editor: show item positions along the curve while editing; per-point tension.
