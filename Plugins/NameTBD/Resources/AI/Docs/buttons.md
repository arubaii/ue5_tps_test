# Buttons

## Ready-made buttons
Palette "NameTBD Buttons"; in add_widgets use "button": generic (label), pill_switch (on/off switch), checkbox, radio (exclusive in a group), menu_item (accent bar, label steps aside), hold (fires after a held press, ring fills), tile (icon, name, rarity stripe), choice ("< value >" options or numbers), segmented (options side by side, highlight slides). Their look is Style: AccentColor, Motion (Smooth, Snappy, Bouncy, Gentle, Instant) and a few per kind (GenericButton: Corners, CornerRounding, ContentPadding, FillColor, OutlineColor, TextColor, PressScale). Changing a Style field rewrites the button's parts; Use Style off keeps hand edits. Label: the spec's label, or set_widget text.

## Context Menu Button
The base of every button: states Idle, Hover, Pressed, Enabled (toggles), Disabled. Shape Targets (a shape, Shape Box or Self) and Design Targets (tint, outline tint, brush, material parameters) give each state a look, blended with springs. Background Shape draws the background as any vector shape, its Background Look holding the shape's settings. Click Only Enables, Repeat While Held, Progress Widget for hold buttons.
