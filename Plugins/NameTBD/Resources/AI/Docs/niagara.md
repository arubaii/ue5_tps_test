# Niagara particles in UI

## The widget
NiagaraUIWidget ("Niagara Particles") plays a Niagara System in UMG and draws its CPU sprite and ribbon emitters as Slate meshes - clipped, sorted and tinted like any widget, no render target. Origin places the system's origin in the widget (0.5, 1 = bottom center); Particle Scale = Slate units per Niagara unit; Plane XZ (front; Z up). Color And Opacity tints everything. User Parameters lists the system's User parameters to set per widget. Auto Play, Prewarm Time, Pause When Hidden.

## Materials
A renderer's material is drawn if it is a User Interface material; else Material Remap maps it; else the widget's Particle Image or a soft dot. The library's SoftDot and Bubble masters are UI materials made for particles.

## Templates
create_niagara_ui_system makes a copy of a library template (RisingBubbles, Sparkles, Confetti, Embers, GlowRing, Trail, RisingSparkleLoop), CPU and local space, with friendly params: rate, lifetime, size, color, opacity, scale, and per template rise_speed, spread, count. Templates ship built in /NameTBD/AI/Niagara when the release includes them; otherwise they are made once from the engine's emitter templates into the Library Folder (approximate - author the template once for the exact look; its intended module stack is in Resources/AI/Niagara/Templates.json).

## Bursts and cues
Burst At Origin / Play At play one-shots (confetti on a win). In a Widget Animation, the Particles cue fires bursts or plays systems on a Particles widget.
