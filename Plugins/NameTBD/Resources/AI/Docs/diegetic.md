# Diegetic UI (UI in the world)

## Setting one up
Add a Diegetic Screen (component, or the Diegetic Screen Actor from Place Actors), pick its Widget Class - any Widget Blueprint - and pick Quick Setup for the kind of UI it is; everything else is set for it and can be changed after:
- Wall Terminal (Fallout computer): green CRT look, the camera moves in to use it.
- Monitor (a TV or monitor model): shown on the mesh's screen material slot (Display > Mesh; clicking needs Project Settings > Physics > Support UV From Hit Results).
- Hologram (Dead Space store): additive, scan lines, glitch, turns toward the player, appears as the player comes near.
- Hologram Menu (Dead Space inventory): floats in front of the player, hidden until Open Diegetic Menu; Back closes it; the game keeps running.
- Device Display (Pip-Boy, ammo counter, suit readout): small LCD, not clickable, redraws only when it changes - attach it (or the mesh it shows on) to a socket.
- Sign: plain, not clickable, cheap.
- Glass Panel: frosted glass that blurs the world behind.
Every local player's pawn gets a Diegetic Interactor (Project Settings > Plugins > Diegetic UI): look at a button and click (left mouse, gamepad left face button).

## Looks
Screen Look > Preset: Clean (exactly as UMG), Terminal (Green / Amber), Hologram, Frosted Glass, LCD Panel; then any value (glow, scan lines, curvature, flicker, grain, pixel grid, additive, edge fade, glitch, background blur and glass tint). Material Automatic (default) draws them with the plugin's own screen materials, made by the editor the first time. Background Blur widgets work on screens: the screen's material blurs the world behind their area. Copy To Project in the Details makes an editable copy.

## Data and devices
Inside the widget: Get Diegetic Screen Context (the object the screen shows - Set Screen Context, Context Actor, else the screen's actor) and Get Diegetic Screen Owner: an ammo counter reads its weapon with no wiring. Screens on one actor with the same Focus Group are used together (a desk of monitors). Terminal Text (palette NameTBD) types lines with a blinking caret and key clicks - a boot sequence written in its Text types itself out.

## Cost
Update Mode: By Distance (default), When Changed (signs, readouts), Every Frame (hero screens). A per-frame budget spreads redraws; screens pointed at redraw every frame. Console: stat NameTBDDiegetic, NameTBD.Diegetic.Debug 1.

## The widget hears the screen
State machine inputs ScreenAwake, ScreenAimedAt, ScreenInUse (Bool), ScreenDistance (Number); or the Diegetic Screen Widget interface (Wake, Sleep, Aim Changed, Use Changed, Back, Context Changed). Pointer-driven animation inputs and UI sounds work on screens (sounds play from the screen).

## Not yet
Info Cards from an anchor on a screen still open on the HUD. The tonemapper still slightly shifts colours (Tone Curve Amount 0 in the post process for an exact match).
