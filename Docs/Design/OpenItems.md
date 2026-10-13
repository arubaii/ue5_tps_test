# Open items carried forward

# Open items carried forward
- **Handoff gap:** the change sets `NameTBD_Workflow_v2_fix6.patch` and `_fix6_fix1` ... `_fix6_fix5` (committed in HEAD `9f5a706` "Updated dope sheet") have no section here - their handoff wasn't saved to the repo. What they did can be read from the patch files at the repo root.
- **Designer preview arbitration** (Workflow v2_fix5 follow-ups, not asked): only the State Machine editor's preview stands down for the Widget Animations panel (`WidgetAnimPanelPosing`); a Widget Animation Component's designer-toolbar preview, data-binder design-time playback and a Design track on a `UUserWidget_Animatable` root (`SynchronizeProperties` -> `NotifyOwnerDesignTimeRefresh` restores baselines) could still fight the panel on (this widget). The builder's working copy can't be a Dope Sheet (it has no `bDopeSheet`).
- **Dope Sheet** (Workflow v2 follow-ups, not asked): copy/paste of keyframes (rows aren't copied - fix4); merging two rows of one kind made before fix4; a live value per row in the Keys viewer (Unity's property fields); Auto Key making a Transform row when the widget has none; per-channel sub-rows (a Transform row's Translation / Scale / Angle keyed separately, as Blender/AE channels); a summary row of all keys at the top.
- **Key workflow** (Workflow v1 follow-ups, not asked): per-property sub-rows under a track in the Track Editor (After Effects' twirled-down properties, Sequencer's channels) with their own diamonds; a ◀ ◆ ▶ key navigator on the Animation Tracks list rows (add key at playhead / step); key reduction (simplify keys) and copy/paste of key values; Key Values for a parameter-bound field (today it edits the constant the binding hides - same as the Graph Editor); dropping an animation onto an existing state to swap what it plays; dragging a timeline from the Widget Animations panel's timeline picker; listing timelines in a state's Animation picker in the Details.
- **Shape Box designer outline offset** (WidgetGizmo fix2, item 6): not found from code - run `ShapeBox.DebugContentBox 1` and compare (see the fix2 change set). The gizmo would sit off the same way on a widget inside a Warped Shape Box (its geometry is the offscreen window's).
- **Widget Gizmo** (follow-ups, not asked): one gizmo for a multi-selection (shared pivot, like the level editor's); relative typed values (Blender's "R 90" adds) as an option; arithmetic in typed values; grid snapping for Move from the designer's own grid settings; anchoring Scale to the opposite edge with Alt; keying Slide tracks along their direction; keying while the panel is in its (closed) drawer.
- **Reliable material generation** (AIAssistant v2): run the eval per model and fill in first-try rates; see "Still open (material generation)" and "Unverified" there. Optional (fix5 follow-up): a per-platform compile check (Vulkan / Metal / ES3.1 shader formats) after a build, if wanted - left out because it multiplies compile time and needs those platforms' shader compilers installed.
- **AI Assistant** (AIAssistant v1 follow-ups): see "Still open (AI Assistant)"; first compile in the engine expected to need small API fixes (see "Unverified" there).
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
- **State machine**: see "Still open (state machine)" (v9 follow-ups). Animation Parameters: nothing planned left.
- **Diegetic UI** (DiegeticUI v1 follow-ups): see "Still open (diegetic UI)" - Info Cards opening inside a screen first.

