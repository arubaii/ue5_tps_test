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
  - `FFlowListScrolling`: Snap To Items, Loop, Stop At Content End, drag (sensitivity, Fling Coast, Max Items Per Fling, Elastic/Clamp overscroll), `ScrollSpring` (`FWidgetSpringParams`), wheel, navigation, Focus Follows Selection, Click To Focus.
  - `FFlowListTransitions`: Enter/Exit (`FFlowListTransition`: duration, delay, easing, opacity, scale, offset along/across the path, rotation), Stagger, `ReorderSpring`, Play Intro, Keep Focused Item.
  - `FlowListTypes::ApplyPreset` holds the presets.
- **Slate** (`SFlowList.h/.cpp`, an `SPanel`):
  - Scroll position is in items. A slot's `Place` springs to its `ItemIndex`, which is how items slide aside on insert, remove and move.
  - `ComputeLayout` sweeps outward from the focus, integrating each pitch scaled by Scale × Squash (Simpson's rule per item) when Spacing Follows Scale is on. That's what makes a taper pack into a curve.
  - Uniform scale goes in the **layout** transform (text stays crisp). Turn and squash go in a **render** transform about the pivot, composed after the child's own render transform, so Widget Animations on items still play.
  - Opacity and tint go through the child's `FWidgetStyle` in `OnPaint`. Each item paints at `MaxLayer + 1`.
  - Motion states: Idle, Drag, Coast, Spring.
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
  - Trigger: Selected, Nearness To Focus, Hovered, or Selected Or Hovered. A spring `Transition` moves between the looks (Nearness follows the list directly).
  - Looks are property bags, reusing the Design track's machinery: Selected/Unselected design settings (brush, outline, colors, font, padding; `WidgetDesignAnimation`) and shape settings (`WidgetShapeAnimation`, and Shape Box through `ApplyShapeToBox`).
  - Also: material parameters, render opacity and transform, and Anim IDs played each way.
  - Only settings that differ between the two looks are written.
  - `FFlowListItemStyleDetails` (editor) lays the bags out for the owner widget as Details are shown. It finds the owner through a live preview copy or the authored tree, as the Widget Animation Component does.
- **Scroll bar:** `UFlowListScrollBar` / `SFlowListScrollBar` (`FlowListScrollBar.h`).
  - Styles: Bar (track and thumb brushes, hover thickening, Auto Hide) or Dots (gliding active dot, Max Dots window with shrinking edge dots).
  - It follows its list by name or the nearest one, through `OnListUpdated`. Dragging the thumb calls `SetScrollFraction`, and letting go calls `SnapToNearestItem`.

## Previous change sets
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

## Latest change set — `NameTBD_FlowList_v1_to_v2.patch` (applies on top of v1; untested in editor)

### Fixes after building
- `NameTBD_FlowList_v2_fix1.patch` (apply after v1_to_v2): UHT rejected `BlueprintReadOnly` on the Item Style's four `FInstancedPropertyBag` looks ("Type 'FInstancedPropertyBag' is not supported by blueprint"). They are now `EditAnywhere` only, like the plugin's other bags (`FWidgetTransformKeyframe::ShapeValues`). Blueprint switches looks with Force Look / Clear Forced Look.
- Rule for later work: never put `BlueprintReadOnly` / `BlueprintReadWrite` on an `FInstancedPropertyBag` UPROPERTY.

### Flow List
- **Performance:**
  - The layout is cached per frame.
  - Virtual lists: Entry Widget Class, List Items, a pooled entry per item in view, `IFlowListEntry`, and Num Designer Preview Entries.
  - Blueprint for virtual lists: Set / Add / Insert / Remove / Clear / Refresh List Items, Get List Item At, Get Selected List Item, Get Entry For List Item, Scroll To List Item.
  - Even virtual lists compute the content end in closed form.
- **Hover Magnify** in Effects: Scale, Radius, Easing, Fade Time, Pause While Scrolling.
- **Flow List Scroll Bar** widget (Bar or Dots), and `UFlowList::SnapToNearestItem`.
- **State machine plumbing:**
  - The Flow List Value Input.
  - Slot Item Value.
  - Built-in Selected for widgets inside the selected item.
  - `FindNearestFlowList`, `FindFlowListOfItem`, `IsInSelectedItem` and `GetItemValue` (Blueprint).
- **Flow List Item Style** UI component, with its editor Details customization.

### State machine editor
- **Disabling transitions from the right-click menu.**
  - Diagnosis: the "Disable Transition" entry existed, but only on the transition node's own menu. Right-clicking a wire usually hands the menu a state's pin, so the state's menu showed and the option looked missing.
  - Now a state's menu opened from a pin lists **Transitions On This Wire**, each with Disable/Enable.
  - The transition node's entry acts on the whole selection when the node is part of it ("Disable Selected Transitions").
- **Jump To State:** double-click a state, or use its menu's Jump To State. Open Animation moved to the menu only.
  - `WidgetAnimMachineJump::Plan` finds the fewest enabled transitions from the state the watched preview is in. Any State's transitions count from anywhere; Entry's only before start.
  - For each step, every input is reset to its default ("unnecessary unset"), the transition's conditions are solved (number comparisons together, as an interval), and every other way out of the step's state is blocked where that doesn't undo a required input.
  - The last step also blocks every way out of the target.
  - `SWidgetAnimStateMachineEditor::TickJump` applies each step once the layer has arrived and its transition has finished, or after 4 s.
  - Every step also blocks leaving the state it arrives at, so the layer doesn't run on before the next step is set.
  - Increased / Decreased / Changed are moved from, and judged against, what the step before left (the current inputs for the first step).
  - Inputs compared against another input are solved after those compared against fixed values; both sides count as required. Choices compare by option name, as the runtime does.
  - Blocking repeats (up to 4 passes) until nothing more changes.
  - With nothing watched, the last step is applied at once.
  - Unreachable targets show a notification and set the conditions of a transition into the target. A step whose own transition stops holding after blocking also shows a notification ("the jump may stop short").

### Files
- New, runtime (`Source/NameTBD/Widgets/FlowList/`):
  - `FlowListInput.h/.cpp`
  - `FlowListScrollBar.h/.cpp`
  - `FlowListItemStyle.h/.cpp`
- New, editor:
  - `FlowListItemStyleDetails.h/.cpp`
  - `WidgetAnimMachineJump.h/.cpp`
- Changed, Flow List: `FlowListTypes.h`, `SFlowList.h/.cpp`, `FlowList.h/.cpp`.
- Changed, state machines and modules:
  - `WidgetAnimStateMachine.h/.cpp`
  - `WidgetAnimStateMachineRuntime.cpp`
  - `NameTBD.cpp`
  - `NameTBDEditor.cpp`
- Changed, state machine editor:
  - `WidgetAnimStateMachineGraph.h/.cpp`
  - `SWidgetAnimStateMachineEditor.h/.cpp`
  - `NameTBDTooltips.h`
- `NameTBD_Handoff.md`.
- No Build.cs changes.

### Reviewed before delivery
An independent review pass over the v2 diff found these, all fixed in this patch:
- **Virtual list:**
  - Entry releases are queued and fired after `LiveOrder` is rebuilt, so a release callback can't re-enter the list mid-update.
  - The selected entry and any entry holding focus are never released, so gamepad focus isn't lost when scrolling.
  - `RealizeIndex` gives the widget back if the re-check fails, so no entry leaks.
  - Entries built or released at the selection call `OnFlowListSelected` (true/false) and keep `SelectedItem` right; pooled entries no longer keep a stale selection.
  - The measured entry extent resets on Refresh and on `SetSettings`.
- **Looping lists:** the scroll is normalized into one lap before any count change (virtual insert/remove/set, and plain insert/remove/move or visibility change), so the anchor isn't measured laps away.
- **Painting:** `OnPaint` copies the cached layout rather than holding a reference into it. `OnAnimated` fires only when the scroll or magnify actually moved.
- **Click:** a press and release must be on the same item widget.
- **Slots:** `SetPadding` / `SetSizeOverride` invalidate the layout cache (`NotifySlotChanged`).
- **Selected built-in:** the "selected by its Flow List" fallback applies only when no provider answered and the host isn't a Check Box, so a Check Box's checked state and Common UI's answers stand.
- **Item Style:** a pooled entry given another item snaps to that item's look without animating or firing `OnSelectedChanged`.
- **Jump To State planner:** see the State machine editor notes above (step-to-step Increased/Decreased/Changed, blocking at each arrival, input-vs-input ordering, repeated blocking, Choice names, warning when a step breaks).

### Decisions made without asking
- The state machine lives on whatever reacts (the portrait), not on the list. The list publishes through the Value Input and the built-in Selected. One list can drive any number of machines, and machines stay reusable assets.
- The item style is a UI Component rather than a wrapper widget. It goes on any widget at any depth of the item, which covers the "text inside a button" case. Looks reuse the Design and Shape track bags, so anything those tracks can key, a style can switch.
- Virtual lists assume even items. Variable-size virtual entries would need measuring every item and were left out.
- Double-click on a state now jumps instead of opening its animation, as requested. Open Animation is still in the menu.

### Known risks / things to check after building
- UHT passes as of `_fix1`; the C++ compile hasn't been seen yet. Expect more `_fix` patches.
- APIs most likely to need a touch:
  - `UUIComponent` overrides on 5.7 (as `UWidgetAnimationComponent` does).
  - `FInstancedPropertyBag` with `meta = (FixedLayout)`.
  - `CreateWidget<UUserWidget>(UWidget*, ...)`.
  - `UWidget::SetDesignerFlags` / `GetDesignerFlags` (wrapped in `WITH_EDITOR`).
  - `TPanelChildren::AddSlot`.
  - `IPropertyUtilities::ForceRefresh`.
  - `UGraphNodeContextMenuContext::Pin` in the state node's menu.
  - `SWidget::HasAnyUserFocusOrFocusedDescendants` (used to keep focused entries alive).
  - `TWeakObjectPtr<const UFlowList>` in `UFlowListItemStyle`.
- Check by hand:
  - A virtual list of 1,000 items: scroll fast, and check entries recycle with the right items.
  - Add/Remove List Item animations.
  - Gamepad navigation onto entries that aren't realized yet.
  - Hover Magnify on Carousel and Scroll List.
  - Scroll Bar: drag, track click, Auto Hide, and Dots with 30 items.
  - Item Style on a Button, on the Text Block inside it, and on an Image with a material parameter. Also that its Details lay out the looks the first time they are shown.
  - A portrait state machine Driven By Selected Index.
  - Jump To State through a two-hop path with Bool, Float and Trigger conditions, and one with an Increased condition. Also right-click on a wire to disable it.
  - A looping virtual list: insert and remove items after scrolling several laps.
- Behavior to be aware of: the Item Style now snaps (no animation) whenever its item's index changes, including when an item is inserted in front of it in a plain list. The look is the same either way unless the selection also changed in that frame.

### Ideas for follow-ups
- Variable-size virtual entries: measure on realize and cache per item.
- A Widget Animation track keyed by item distance (Value Inputs already cover most of it).
- Two-way: a Flow List Selected Index set from a View Model (the Widget Data Binder).
