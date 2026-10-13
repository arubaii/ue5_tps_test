# Diegetic UI (UI in the world)

## Setting one up
Add a Diegetic Screen (component, or the Diegetic Screen Actor from Place Actors) and pick its Widget Class - any Widget Blueprint. Then set what it needs:
- Size: Sizing Physical Size (centimetres) + Pixel Density (8-12 px/cm reads well from a step away).
- Display: its own Panel, or Mesh Surface (a monitor or TV model's screen material slot; clicking needs Project Settings > Physics > Support UV From Hit Results). Keep Mesh Material keeps the mesh's own material everywhere and draws the screen only inside UV Region (a display set into a casing).
- Placement: As Placed, Face Camera (Upright) for holograms, In Front Of Player for a menu projected in front of the player.
- Use: Point And Click, Focus View (the camera moves in - a terminal; Move Camera off for a projected menu), Not Usable (signs).
- Start Shown off: nothing of it shows in game until Open Diegetic Menu / Set Screen Shown (a menu the player opens).
- Presence (Wake Distance) and Updates (When Changed for signs and readouts).
Every local player's pawn gets a Diegetic Interactor (Project Settings > Plugins > Diegetic UI): look at a button and click (left mouse, gamepad left face button).

## Rendering and glass
Material Automatic (default) draws the widget exactly as UMG does: lowering a widget's opacity only makes it see-through. Solid parts write depth, so particles and other translucency behind a screen stay behind it (Blend Mode Opaque for a fully opaque screen is cheapest and certain).
Glass is a separate effect: Glass > Preset No Glass (default), Clear, Frosted, Heavy Frost, Smoked, Sci-Fi Pane, or Custom - then Blur, Frost, Refraction + Bevel Width, Ripple, Chromatic Aberration, Tint, Brightness, Sheen, Edge Highlight, Corner Radius. The world behind the widget's see-through parts is seen through the glass (smooth with TSR / TAA on). Background Blur widgets work on screens (Glass > Background Blur Widgets): the screen's material blurs the world behind their area.
Screen Look > Preset (Clean, Terminal Green / Amber, Hologram, LCD Panel) are sample looks for testing; a game's own look (hologram, CRT) is its own material - Copy To Project in the Details makes an editable copy of the plugin's to start from (Material Custom).

## Data and devices
Inside the widget: Get Diegetic Screen Context (the object the screen shows - Set Screen Context, Context Actor, else the screen's actor) and Get Diegetic Screen Owner: an ammo counter reads its weapon with no wiring. Screens on one actor with the same Focus Group are used together (a desk of monitors). Terminal Text (palette NameTBD) types lines with a blinking caret and key clicks - a boot sequence written in its Text types itself out.

## Cost
Update Mode: By Distance (default), When Changed (signs, readouts), Every Frame (hero screens). A per-frame budget spreads redraws; screens pointed at redraw every frame. Console: stat NameTBDDiegetic, NameTBD.Diegetic.Debug 1.

## The widget hears the screen
State machine inputs ScreenAwake, ScreenAimedAt, ScreenInUse (Bool), ScreenDistance (Number); or the Diegetic Screen Widget interface (Wake, Sleep, Aim Changed, Use Changed, Back, Context Changed). Pointer-driven animation inputs and UI sounds work on screens (sounds play from the screen).

## Not yet
Info Cards from an anchor on a screen still open on the HUD. The tonemapper still slightly shifts colours (Tone Curve Amount 0 in the post process for an exact match). Particles behind glass aren't seen through it.
