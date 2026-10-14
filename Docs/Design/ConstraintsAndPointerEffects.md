# Widget Constraints and Pointer Effects

> **Standing expectation (the request's own words):** "There could be more use cases that users desire that I haven't thought of, but would be expected in a production-grade application." Each part has a *Beyond the request* list for it and a *Still open* list of what is still expected.

Two UI Components (Add Component on a widget's Details), both wrapping the widget's own Slate widget the way Drop Shadow does (`RebuildWidgetWithContent`):
- **Constraints** (`Widgets/Constraints/`): the widget's place, turn, size and opacity tied to other widgets, the pointer, paths and areas.
- **Pointer Effects** (`Widgets/Pointer/`): how a widget answers the pointer (tilt, lean, press, lights, ripples), and the pointer handed on to materials, shapes, Widget Animations and state machines.
Both are driven by Widget Animations through the **Interaction** tracks (`WidgetAnimation/Evaluators/WidgetAnimInteractionTracks.h/.cpp`).

## Constraints

### How production tools do it (researched)
- **Rive** (Translation, Rotation, Scale, Transform, Distance, Follow Path, IK, Scroll): a constraint stack applied in order; every constraint has **Strength** (0..1, animatable, blends from the unconstrained value); Source / Destination / Min-Max **space** (World or Local); **Copy** per axis with a factor; an **Offset** toggle (keep the starting offset); **Min / Max** limits. Distance has Closer / Further / Exact. Follow Path: Distance (0..100%, may exceed and loop), Orient, Offset.
- **Spine** path constraint: Position Fixed / Percent, Spacing, Rotate Tangent / Chain; **Unity**: Aim (Look At) with world-up, Position / Rotation / Scale / Parent constraints with weights; **Blender**: Limit Location / Distance, Transformation (map a source channel's range to a destination channel's range), Damped Track.
- **Framer Motion / web UI**: springs on derived values (`useSpring`), `useTransform` range mapping, drag constraints with elasticity; magnetic buttons and parallax are this.
- Taken: the stack, Strength, Copy factors, Offset / Keep Starting Offset, Min / Max relative to rest, Screen / Local source space, Distance modes, Follow Path (+ Closest To Target, Speed, Orient, path offset), Look At with limits and keep-upright, Stay Inside with elasticity, Remap (range -> range, curve), and - beyond Rive - Activation and spring Smoothing per constraint. Not taken: IK and bones (2D rigging, out of scope).

### Model
- **`UWidgetConstraintComponent`** ("Constraints"): `Constraints` (instanced `UWidgetConstraint` objects, applied in order), Strength (all at once), Solve In Designer, Show In Designer.
- **Never writes the Render Transform.** `SWidgetConstraintHost` arranges the widget with an extra transform `E` (about its pivot) over its own render transform (`Concatenate(Own, E)` in `MakeChild(..., Pivot)`), and multiplies its opacity. So Widget Animations, the gizmo and Blueprint keep owning the Render Transform; constraints layer on top; hit-testing follows what is drawn.
- **Space**: everything in the widget's layout space (Slate units from its laid-out box's top left; the host is never transformed). A pose (`FWidgetConstraintPose`) is the pivot's place, the angle (degrees, clockwise), scale and an opacity multiplier. Rest = laid out + own Render Transform. `E` is solved so the pivot lands on the pose: `L = Rot(dAngle) * Scale(ratio)`, `T = Pose - Pivot - L * OwnTranslation`.
- **Solver** (`UWidgetConstraintComponent::SolveAll`, on `FSlateApplication::OnPreTick`): every live component, depth first through what each depends on (targets, path / area widgets, constrained parents), so chains (a tail of dots following the cursor) solve in one frame; a cycle is broken where it closes. A constrained target is read where its constraints put it this frame (`SolvedLocalToDesktop`); anything else where it was last drawn (tick-space geometry). The pointer: `WidgetAnimInput::ReadPointer` (desktop, or a Diegetic Screen's world pointer); hover / press as the Widget Inputs read them.
- **Per constraint** (`UWidgetConstraint::Apply`): Solve -> what the widget would be at full strength; the change it adds (move, turn, scale ratio, opacity ratio) x Strength x component Strength x **Activation** (Always / While Hovered / While Pressed / Near Pointer within Activation Radius of its edges; faded over Activation Fade; a constraint whose target is missing fades out keeping its last change); **Smooth** springs that change (`FWidgetSpringParams` Duration + Bounce, `AdvanceSpring`), so the widget's own animation underneath is never lagged.
- **Targets** (`FWidgetConstraintTarget`): Widget (by name - dropdown - or Set Target Widget at runtime; Anchor 0..1 on it), Pointer (When Pointer Away: Release / Hold Last), Parent (Anchor), Point (Slate units from the rest pivot).

### Constraint types
- **Copy Position**: Screen (part of the way to the target, per axis: Copy Amount; Keep Starting Offset) or Local (the target's own animated Translation x amount); Offset; Limit Min / Max from rest.
- **Copy Rotation**: Local (target's Render Transform angle x amount: meshing gears with -1) or Screen (its turn as drawn); Angle Offset; limits from rest.
- **Copy Scale**: Local / Screen, per axis, amount of the change, Min / Max scale.
- **Copy Transform**: pinned to a widget (its anchor, turned and scaled with it): Offset in the target's own units, Angle Offset, Keep Starting Offset (as if parented); a pointer / point copies only the place.
- **Distance**: Within (Closer) / Outside (Further) / Exact Distance from the target.
- **Follow Path**: Path Source **Shape Outline** (a shape's outline as drawn - `MakeFrame` + `BuildOutline`, longest contour: an Ellipse ring, a Custom Shape), **Widget Edge** (any widget's box, rounded as drawn, or its ellipse), **Circle** (Radius round the Target, Start Angle), **Widget Path Connection** (a Widget Path Layer's line From -> To: `GetConnectionPath`); Path Offset (along normals, outward on closed paths); Mode **Distance** (Path Position as a fraction or Pixels, + Speed a second; Loop) or **Closest To Target** (projected onto the path - a knob dragged round a dial, a bead on a wire); Orient + Orient Offset; Get Current Fraction.
- **Look At**: Forward Angle (which way it points at rest), Angle Offset, limits from rest, Keep Upright (flips Y while pointing left).
- **Stay Inside**: the Parent or a widget, Area Shape Auto (an Ellipse is round) / Box / Ellipse, Margin, Keep Whole Widget Inside, **Elasticity** (rubber-band edge).
- **Remap (Drive)**: Measure (Offset X / Y, Distance - from edges or pivot, Direction, Target Angle / Scale / Opacity) from From Range to To Range (Clamp, optional curve) -> Drive (Offset X / Y, Angle, Scale, Scale X / Y, Opacity). The macOS-dock magnify, fade-in-as-you-approach, a needle following a slider.
- **Pointer Parallax**: shifts by Depth (and turns by Tilt) with the pointer across the screen (outermost User Widget) or an Area Widget.
- Channels (`EWidgetConstraintChannel`: Strength, Path Position, Offset X / Y, Angle Offset, Distance) - `SetChannel` / `GetChannel`, Blueprint `Set / Get Constraint Value`, the **Constraint** track.

### Recipes
- Ball round a ring: Follow Path, Shape Outline of an Ellipse, Speed 0.25 (or a Constraint track on Path Position 0 -> 1, looping).
- Eyes following the cursor: on each pupil, Copy Position -> Pointer (Copy Amount 0.15, Smooth), then Stay Inside the eye (Ellipse, Keep Whole Widget Inside). Or Look At -> Pointer on an eye graphic that turns.
- Magnetic button: Copy Position -> Pointer, Copy Amount 0.3, Activation Near Pointer (80), Limit +-12, Smooth (Bounce 0.3). Or Pointer Effects' Magnet.
- Dial knob: Follow Path (Circle round the dial, or its Ellipse), Closest To Target -> Pointer (Hold Last), Activation While Pressed.
- Tail of dots: dot 1 Copy Position -> Pointer (Smooth); dot N Copy Position -> dot N-1 (Smooth, slower).
- Label pinned to a moving marker: Copy Transform -> marker, Offset (0, -24), Copy Rotation off.

### Beyond the request (production expectations added)
- Activation (hover / press / proximity) with fades; spring smoothing that never lags the underlying animation; Local space to mirror another widget's animation; Remap for any pointer- or widget-driven value; parallax; dependency-ordered solving; designer preview with debug lines (magenta: links to targets, paths, limit boxes, distance circles); Diegetic Screen pointers; runtime API (add / find / remove constraints, set targets to widgets made at runtime, reset motion).

### Decisions made without asking
- Instanced `UWidgetConstraint` objects (each type its own class, a "+" picker in Details, settable from Blueprint by reference) rather than an `FInstancedStruct` list.
- Constraints apply over the Render Transform (an extra transform) rather than writing it, so they never fight animations or the designer.
- Copy Position's Copy Amount means "how far toward the target" (Screen) - the UI meaning - not Rive's multiply-the-coordinate; Local multiplies the target's offset.
- Limits are relative to where the widget rests (UI-friendly), not absolute coordinates.
- Solved on Slate's pre-tick, from geometry of the last paint (no layout pass of its own).

### Still open
- 2D rigging: IK chains, bones, mesh deformation (out of scope, as asked).
- Rive's Scroll constraint (content following a scroll with physics) - the Flow List covers lists; a general one isn't done.
- Path offset on sharp concave corners (simple normal offset); a Follow Path that spaces several widgets along one path (Spine's spacing) - today one constraint per widget.
- A canvas gizmo to set targets / anchors by dragging (today: Details).
- Order between several UI components on one widget (Constraints, Pointer Effects, Drop Shadow each wrap the widget; which is outermost follows the component list).

### Unverified (no engine here: written, not compiled or run)
- `UUIComponent::RebuildWidgetWithContent` called at design time and at runtime (as Drop Shadow assumes); which Slate widget `GetCachedWidget` returns for a widget with components (the solver prefers the solved transform for constrained targets).
- `FSlateApplication::OnPreTick().AddStatic`, `FWidgetTransform::ToSlateRenderTransform`, `FGeometry::MakeChild(Size, LayoutTransform, RenderTransform, Pivot)`.
- Instanced sub-objects inside a UI component duplicated into each User Widget instance; `GetOptions` on a constraint's fields (the outer chain at design time is the Widget Blueprint's component extension).
- Hit-testing a widget moved by a large extra transform.

## Pointer Effects

### How production UIs do it (researched)
- Card tilt (CSS `perspective` + rotateX/Y from the pointer, "VanillaTilt", holographic trading-card demos: tilt + glare + a rainbow foil shifting with the angle), magnetic buttons (translate a fraction toward the cursor with a spring), the "spotlight border" (a radial gradient on the border following the cursor across a grid of cards - Vercel / Linear), shine / shimmer bands following the pointer, Material ripples from the press point, press scale. Taken: all of these; plus gamepad focus as hover (consoles) and a virtual pointer for sticks.

### Model
- **`UPointerEffectsComponent`** ("Pointer Effects"): Preset (Tilt Card, Holographic Card, Shimmer Button, Magnetic Button, Spotlight Border, Ripple, Lift, Custom), Intensity (all effects; animatable), **Tilt** (Max Angle, Press Away / Rise Toward, Perspective, Hover Scale, spring), **Magnet** (Pull, Max Offset, Range beyond the edges, spring), **Press** (Press Scale, spring), **Lights** (`FPointerLight` list: Spotlight / Sheen / Border Glow / Holographic, Show While Hovered / Near Pointer / Always, Color (alpha = strength), Radius, Sheen Angle / Width, Border Width), **Ripple** (Color, Duration), corners (Match Widget via `ReadCaster`, or Corner Radius), Proximity Range, Hover Fade, React To Focus, **Outputs**, Preview In Designer + Designer Pointer.
- **Slate**: `SPointerEffectsHost` (measures the pointer on its own untransformed box, springs everything, arranges the content with the lean and lift / press scale about its middle) -> `SShapeBox` (the tilt: Angle + Perspective; flat - no offscreen cost - at rest) -> overlay of the widget and `SPointerEffectsSurface` (lights and ripples, tilted with it, hit-test invisible). Lights are per-vertex colors on the widget's rounded outline (fill or inside rim) cut into a grid (4..16 units) and cached by size and corners.
- **Pointer**: `WidgetAnimInput::ReadPointer` (desktop or Diegetic Screen), `IsPointerOver` / `IsPointUnder`; focus (descendants) = hover at the middle; Set Virtual Pointer overrides; pressed = a left press that started over it, still held. Velocity eased (12/s). Runtime only: in the designer it is posed once at Designer Pointer, hovering.
- **Outputs** (`FPointerEffectsOutputs`, every change): materials (Image / Border `GetDynamicMaterial`, a shape's material paint - the widget and, with Include Children, through panels and User Widgets 6 deep): vector Pointer (U, V, hover, pressed), scalar Pointer Hover, vector Pointer Velocity (x, y, speed, proximity); shapes' Radial Gradient centers (fill and stroke); a Widget Animation Vector 2D value (-1..1) and a hover value (`SetWidgetAnimationValue*`); state machine inputs (Vector 2D, hover Number, proximity Number, any machine); `On Pointer Effects Updated`; `Get Pointer State`.
- Channels (`EPointerEffectsChannel`: Intensity, Tilt Angle, Magnet Pull, Light Strength) - Set / Get Effect Value, the **Pointer Effects** track.

### Also for animations
- **Pointer Distance and Motion** value input (`FWidgetAnimInput_PointerMotion`, offered on every value track): Distance, Proximity (Range), Direction (degrees), Speed, Velocity X / Y - beside the existing Pointer Position, Hover / Press / Focus, Drag and Scroll inputs.

### Beyond the request (production expectations added)
- Gamepad / keyboard focus and a virtual pointer (stick) so console UIs get the same polish; Diegetic Screen pointers; proximity effects that start before the pointer arrives; presets; designer preview; outputs to materials / shapes / animations / state machines with no Blueprint; press ripples; animatable intensity.

### Decisions made without asking
- One component with several effects (and a light list) rather than a component per effect.
- Tilt through Shape Box (as Flip Card) rather than a render transform shear: real perspective; offscreen only while tilted.
- Lights drawn as vector meshes (no material needed); a material that wants more reads the Pointer parameters.

### Still open
- A drop shadow that slides opposite the tilt / follows a light at the pointer (Drop Shadow integration).
- Cursor trails / particles following the pointer (the Niagara widget can be driven by the outputs; no built-in trail).
- Additive blending for lights (Slate custom verts draw alpha-blended: lights brighten toward their color rather than add).

### Unverified (no engine here: written, not compiled or run)
- Shape Box as the tilt layer around arbitrary content inside a UI component wrapper; ripples / lights invalidating under global invalidation.
- `UImage::GetDynamicMaterial` / `UBorder::GetDynamicMaterial` returning null for non-material brushes (as documented).
- `HasFocusedDescendants` on the host for focus-as-hover.

## Interaction tracks (Widget Animations)
- **Constraint** (`FWidgetAnimConstraintParams`: Constraint name - None the first -, Channel, Start / End Value) and **Pointer Effects** (`FWidgetAnimPointerEffectsParams`: Channel, Start / End Value), listed under **Interaction**. Target: the Track Group's widget's component, or the first widget inside it with one. Baselines carry the name and channel and are put back by value. Like every registered type they can be keyed, eased, sprung, value-driven (fed by a Value Input - the pointer) and played by state machines.
- Unverified: the player treating a component (not a widget) as a track's resolved target throughout (baselines, previews).
