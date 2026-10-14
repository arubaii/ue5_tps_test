# Widget Paths (skill trees, talent webs, flowcharts, maps)

> **Standing expectation (the request's own words):** "There could be more use cases that users desire that I haven't thought of, but would be expected in a production-grade application." The section *Beyond the request* lists what was added for it; *Still open* lists what is still expected and not done.

## What it is
- **Widget Path Layer** (`Widgets/Paths/WidgetPathLayer.h/.cpp`; `UWidgetPathLayer`, `SWidgetPathLayer`, palette NameTBD): one widget that draws every line ("connection") between widgets of the same Widget Blueprint (its "nodes"). Put it in the same tree as the nodes - usually behind them, filling the area they are in (an Overlay or Canvas slot set to fill) - and list Connections (From / To are dropdowns of the tree's widgets, `GetOptions = GetWidgetOptions`). Lines follow their nodes wherever they are laid out, animated, constrained or scrolled. Hit Test Invisible by default (never blocks its nodes).
- Data types, routing geometry and presets: `Widgets/Paths/WidgetPathTypes.h/.cpp`. Widget-name dropdowns for any setting: `Widgets/WidgetTreeLookup.h/.cpp` (finds the tree through outers - Widget Tree, User Widget, or an outer with a `WidgetTree` object property, read by reflection so the runtime module needn't know `UWidgetBlueprint`).
- **Designer tool** (`NameTBDEditor/Private/WidgetPathConnectTool.h/.cpp`, `FWidgetPathConnectExtension`): select two or more widgets in a Widget Blueprint that has a layer, and a toolbar over the first offers **Connect** (each widget to the next, ordered along the way they are spread - left to right or top to bottom - so a row of nodes is chained in one click; existing links kept) and **Disconnect** (links among them). One transaction; edits the template and the designer preview's layer. Tooltips `NameTBD::Tooltips::WidgetPathTools`.

## How production tools do it (researched)
- **Node editors / diagram libraries** (React Flow, JointJS, GoJS, Unreal's own graphs): edge types Straight, Bezier ("default"), Step and **Smooth Step** (orthogonal with rounded corners), markers (arrow / arrow closed) at either end, animated (marching) dashes, ports on a node's sides, edges routed from the side facing the target. Taken: Straight / Curve / Elbow (with Round / Sharp / Chamfer corners) / Step, sides with Auto, Start/End markers, marching dashes, spread ports.
- **Game skill trees** (Path of Exile's passive tree, Diablo IV, Last Epoch, Borderlands, Ghost of Tsushima): line style by the state of both ends - locked (dim), allocatable (lit edge from an allocated node), allocated (bright, often with light flowing); hovering a node **previews the route** to it from the allocated set (PoE) and shows the cost; unlocking **draws the line on** from the reached node; radial trees route along **orbits** round a center. Taken: state rules, Derive Available, Hover Highlight Route To, Find Route (for costs), Draw On transitions from the reached end, Orbit route.
- **Transit maps / PCB art**: octilinear (45-degree) runs with round or chamfered bends, a casing (outline) under a colored line, station markers. Taken: Diagonal route, Outline, Dot markers, presets Metro / Circuit.

## Routing (`FWidgetPathRouting`, `WidgetPathGeometry`)
- `EWidgetPathRoute`: **Straight**, **Curve** (cubic leaving each end along its side or the route axis, reach = Curvature x max(|dx along axis|, 0.3 x length, 24)), **Elbow** (Manhattan: out, across at Elbow Position, in; goes round the back with stubs when the target is behind; same-direction sides go out past the further; perpendicular sides make an L, or stub-L-stub), **Step** (one bend: To side picked across the first run; level nodes go straight in), **Diagonal** (straight + 45-degree + straight, the slant at Elbow Position, on the longer axis), **Arc** (circular arc through a sagitta of Bend x half the chord; exact circle, sampled to Tolerance), **Orbit** (round Orbit Center widget or Orbit Point, radius lerped between the ends - a spiral when they differ, a spoke when they share an angle; Long Way), **Waypoints** (through the points, cornered, or Catmull-Rom with Smooth Waypoints).
- `EWidgetPathCorner` Round (circular fillet of Corner Radius - tangent R tan(turn/2), held to half of each run, all of an end run - drawn as a cubic arc approximation, 4/3 tan(turn/4)), Sharp (miter join), Chamfer.
- Orientation Auto: across when the nodes' gap (centers apart minus half sizes) is wider across than down.
- Sides (`EWidgetPathSide`): Auto (Center for Straight / Arc / Orbit / Waypoints; the side facing along the axis otherwise), Center, Left / Right / Top / Bottom. Side ports sit at the side's middle + Gap; Center ends are clipped where the line leaves the node's outline + Gap (`ClipStart` / `ClipEnd`: walk to the first point at Gap, then bisect 24 times).
- **Node outline** (`EWidgetPathNodeShape`): Auto reads the node (`UDropShadowComponent::ReadCaster`: Rectangle / Glass Panel / rounded brushes / pills; `UEllipseShape` = ellipse; through a User Widget's root and Size Box / Scale Box / Overlay first child, 4 levels), Box, Ellipse, Point. Distances: rounded-box SDF (`WidgetEffectsDraw::RoundedRectDistance`), ellipse along the ray.
- **Spread Ports**: lines on the same side of the same node are spread Port Spacing apart, ordered by where their other end is along that side.
- Nodes are measured where they were last painted (`GetPaintSpaceGeometry`, transformed into the layer's space, as an axis-aligned box), so a node moved by anything is followed; a layer painted before its nodes is one frame behind a node that moves (put it after them if that matters - lines end at node edges, so they don't overlap).
- Routes are resampled to 6-unit steps (gradients and pulses are per vertex) and cached per line by a hash of both boxes, the routing, port offsets and tolerance.

## Looks, styles and rules
- **`FWidgetPathLook`**: Width; Paint Color / **Gradient Along Path** (stops from From to To) / **Image** (Image Source Texture 2D or SVG - the image-source rule - laid along: U along (once, or every Image Tile Length), V across; Image Scroll Speed); Cap; Dashes (length, gap, **Dash Speed** marching); Outline (casing); Glow (a wide stroke whose anti-alias fade is the glow: width + Glow Size, fade Glow Size); **Breathe** + Breathe Rate (glow and flow pulse 1 - Breathe .. 1); **Flow** Pulse (bands of light by the vertex's distance along: sin^2 bump Flow Length long every Flow Spacing, moving Flow Speed) / Dots / Arrows (marks every Flow Spacing, fading at the piece's ends, pointing the way of the flow); Start / End Marker (Arrow, Open Arrow, Dot, Diamond, Bar; arrowheads pull the line back so it doesn't poke through); Opacity; **Draw Order** (higher over lower: drawn in groups, each group glow -> outline -> line -> flow -> markers on its own layers).
- **Styles** (`FWidgetPathStyle`: Name + Look) and **Rules** (`FWidgetPathStyleRule`: From State, To State (None = any), Either Way, Highlighted / Hovered / Selected each Any / Yes / No, Style). First rule that holds wins; nothing: the first style. A connection's Style (or Set Connection Style) wins over the rules.
- **Node states** are names (`DefaultNodeState` "Locked", `UnlockedState` "Unlocked", `AvailableState` "Available"): Set Node State(s) > read from the node (`IWidgetPathNode::GetWidgetPathNodeState`, 10 Hz, with Read States From Nodes) > the authored Nodes list > Default Node State. **Derive Available**: a default-state node linked to an unlocked one reads Available (from raw states, so it doesn't spread; with Routes Follow Direction only along From -> To).
- **Highlight**: route to the selected node (Set Selected Node; Preview Selected Node in the designer) from the nearest unlocked node (BFS back from the target; Routes Follow Direction makes it one-way); Hover Highlight None / Connections / Route To (runtime, polled every frame with `WidgetAnimInput::IsPointerOver` - Diegetic Screen pointers too); manual Highlight Route / Highlight Node Connections / Clear Highlight.
- **Presets** (`EWidgetPathPreset`, `WidgetPathPresets::Apply`): Skill Tree (default), Constellation, Circuit, Flowchart, Metro, Organic, Radial; each four styles (Locked, Available, Unlocked, Highlighted) and the same four rules. Editing Routing / Styles / Rules makes it Custom.

## Animation
- **Transition** (`EWidgetPathTransition`) when a line's style changes: **Draw On** (the new look over [0, c] and the old over [c, 1] from the end that changed first - each node's `ChangeSerial`: drawn toward the node that changed last), Fade (cross-fade), Instant; Duration + Motion (`EWidgetMotion`). A line added while shown draws on.
- **Progress**: a connection's Progress (0..1, Set Connection Progress animates) fills it from its From node with Progress Style (None: the style named Unlocked State).
- **Intro** (None / Draw On / Fade): each line from its node's depth from the roots (BFS from nodes nothing leads to) x Intro Stagger; played once when shown at runtime, again with Play Intro.
- The Slate layer's active timer runs while it has lines (it notices nodes moving by a hash of their paint transforms and repaints), and calls the UMG layer's polling at runtime.

## Blueprint
Add / Remove / Clear Connections, Are Connected, Set Connection Style, Set Connection Progress, Get Connection Path (the route in desktop space - what a Follow Path constraint uses); Set Node State(s), Get Node State, Get Nodes In State, Reset Node States, Get Connected Nodes (Outgoing / Incoming / Both), Find Route; Set / Get Selected Node, Get Hovered Node, Highlight Route, Highlight Node Connections, Clear Highlight; Apply Preset, Set Style Look, Set Routing, Play Intro, Refresh Nodes; events On Node State Changed, On Hovered Node Changed.

## Beyond the request (production expectations added)
- Graph queries for game logic (Find Route for "points needed" / buy-route, neighbours, nodes in a state), Derive Available so the game only reports what is unlocked, nodes reporting their own state (interface).
- Spread ports, node-outline-exact endings with gaps, Draw Order, progress fills on a line, intro reveal by depth, breathing "available" glow, flowing energy, arrows for direction, image/material/SVG along lines, Diegetic Screen hover, runtime-built trees (connections by widget reference), designer Connect tool, preview of the selected route in the designer.
- A Follow Path constraint can run a widget along any connection (orbs travelling the tree).

## Decisions made without asking
- One layer drawing every line (not a widget per connection): hundreds of lines cost one widget, rules see the whole graph.
- Node states are names, not an enum, so a game's own states ("Maxed", "Disabled") plug into the rules.
- Nodes measured from their last paint (no layout pass of its own); one frame behind a moving node when the layer paints first.
- Spread ports group by side and order by the far end's position (not by connection order).

## Still open
- Lines that avoid other nodes (obstacle-aware orthogonal routing, as diagram tools' "avoid nodes"); bundling of parallel lines; jump-overs where lines cross.
- Hit-testing a line (hover / click a connection), and a tooltip on it.
- Editing Waypoints by dragging on the canvas (today: the Details list); a designer gizmo for Orbit Center.
- Node outlines for rotated nodes (an axis-aligned box of the rotated node is used).
- A per-state node look (the layer styles lines only - nodes style themselves, e.g. from their state machine).

## Unverified (no engine here: written, not compiled or run)
- `SetVisibility` in the constructor (CDO) for the Hit Test Invisible default; `GetOptions` on fields of structs inside arrays (Connections, Routing) resolving `GetWidgetOptions` on the layer.
- `GetPaintSpaceGeometry` of nodes painted after the layer (one frame old), nodes inside a retainer/Shape Box warped content (different paint space).
- The stroke builder with very large AA widths (the glow), dash offset sign (marching direction), `FWidgetSVGBrush::Get` at tile size along a line.
- The designer tool: `FWidgetReference` order, preview geometry for ordering, the preview layer updating after `SynchronizeProperties`.
