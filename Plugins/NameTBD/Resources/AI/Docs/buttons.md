# Buttons

## Ready-made buttons
Palette "NameTBD Buttons"; in add_widgets use "button": generic (label), pill_switch (on/off switch), checkbox, radio (exclusive in a group), menu_item (accent bar, label steps aside), hold (fires after a held press, ring fills), tile (icon, name, rarity stripe), choice ("< value >" options or numbers), segmented (options side by side, highlight slides), glass (a Glass Panel behind the label: GlassPreset - Liquid by default, Frosted, Acrylic, Tinted... - lights up on hover / focus / press; HoverGlow). Their look is Style: AccentColor, Motion (Smooth, Snappy, Bouncy, Gentle, Instant) with EnterDuration / ExitDuration (seconds the motion takes in and back out; 0 = Motion's pace) and a few per kind (GenericButton: Corners, CornerRounding, ContentPadding, FillColor, OutlineColor, TextColor, PressScale). Changing a Style field rewrites the button's parts; Use Style off keeps hand edits. Label: the spec's label, or set_widget text.

## Context Menu Button
The base of every button: states Idle, Hover, Pressed, Enabled (toggles), Disabled. Shape Targets (a shape, Shape Box or Self) and Design Targets (tint, outline tint, brush, material parameters) give each state a look, blended with springs. Background Shape draws the background as any vector shape, its Background Look holding the shape's settings. Click Only Enables, Repeat While Held, Progress Widget for hold buttons.

## Choices from an enum
Choice Button (Options mode) and Segmented Choice: OptionsEnum = an enum's path (any C++ or Blueprint enum) fills Options with its display names. Blueprint: Set Options From Enum, Get / Set Selected Enum Value, Get Selected Enum Name.

## Changing targets at runtime
Give Shape / Design Targets a Target ID (and Active on/off). Set Target Variant(ID) switches on the targets with that ID and off the other ID'd targets on the same widgets - e.g. three Design Targets on one Image with IDs Full / Half / Muted, each a Brush SVG, picked from the volume. Also Set Target Active, Set Design Target Image (SVG or texture in every state), Set Shape Target Transform, Get / Set Shape Target By ID, Get / Set Design Target By ID. Exit Interpolation Speed on a target: its own speed back to Normal.

## Materials on a Context Menu Button
A material on the Background Look's fill (set_shape_paint material=... on the button) shows in every state: a state Look made earlier with a flat fill takes the authored paint instead of covering it, and later edits of the Background Look reach the states that hadn't changed that setting. A Design Target's Material (and Brush Texture) on a shape is put back whenever a Look blend writes over it, and its parameters go to the instance actually drawn.

## Engine Buttons in a Widget Blueprint
A spec {class: "Button", text, icon, variant} builds a themed engine Button (primary: accent filled; secondary: raised, outlined; ghost: no fill until hovered) with its label and / or icon inside in a colour that reads on it. Use it (or a ready-made button) where a parent class binds a Button part by name.
