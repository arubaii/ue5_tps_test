# UI materials

## Choosing: instance or HLSL
1. An instance of a library master: create_material_instance {master, name, params}. Masters: Aurora (bands of light rising from the bottom, animated), SoftGlow (round glow, can breathe), Shimmer (sweeping light band), Gradient (three colors at an angle, can drift), NoiseFog (drifting fog), Vignette (edges darkened), RingPulse (expanding rings), Dissolve (noisy reveal by Progress), SoftDot and Bubble (particles). Instant, can't break, no shader compile. Later changes: set_material_parameters.
2. Anything else: a new material in HLSL - build_ui_material {name, hlsl: {code, params, blend}} (below). Every library master is written this way: list_options material_masters <Name> shows its code and params, a good start for a variation.
3. Node graphs (below) still work - from macro nodes, or on top of a master (from_master) - but HLSL is the direct way.

## Writing a material in HLSL
hlsl: {code, params, blend, tint}. code is the body of a function returning float4(color, alpha) - a string, or a list of lines. It can use uv (float2: 0..1, (0,0) top left), time (seconds) and every param by its name. params: {Name: value} or {Name: {value, group, min, max}} - a number becomes a scalar parameter (a float in the code), a color ("#RRGGBB" / "#RRGGBBAA" / "@accent") a vector parameter (a float4 RGBA in the code: Color.rgb, Color.a), [x, y] a float2. Expose every color, size and speed as a param: later changes are then set_material_parameters on an instance, no rebuild. blend: Translucent (default), Additive (light, glows), Masked. The widget's tint and opacity (and particle colors) are multiplied in and the alpha kept in 0..1 for you ("tint": false to leave the tint out).
Rules that keep it compiling: end statements with ;, declare every variable with its type (float, float2, float3, float4), write numbers with a digit before and after the dot (0.5, 1.0), no function definitions, no textures, no # lines (#define: write static const float), built-ins (sin, cos, tan, atan2, frac, floor, abs, min, max, pow, sqrt, exp, lerp, saturate, clamp, step, smoothstep, distance, length, dot, normalize) plus the helpers below. sin(6.2832 * time * Speed) waves Speed times a second. Param names: capitalized (Speed, GlowColor) - not uv, time or a built-in's name.
Portable for you: pow / sqrt / log / smoothstep run as safe versions - smoothstep works either way (smoothstep(0.5, 0.2, x) is 1 below 0.2, 0 above 0.5) and with lo == hi; pow of a negative base is 0 (square a value that can be negative as x * x). half is float. All maths is full precision, so the material looks the same on PC, console and mobile. Refused, with what to write instead: textures declared in the code, tex2D, discard / clip, Wave / Quad intrinsics (Shader Model 6 only).
Helpers (call them in code like built-ins): NTB_Noise(p) smooth value noise 0..1; NTB_Fbm(p, octaves) layered noise 0..1 (1-8 octaves: clouds, fog, smoke); NTB_Hash(p) 0..1 per point; NTB_SdCircle(uv, center, radius) and NTB_SdRoundRect(uv, center, halfSize, cornerRadius) - signed distances (< 0 inside, UV units); NTB_Fill(distance, softness) a shape's 0..1 fill with a soft edge; NTB_Outline(distance, width, softness) a ring on its outline; NTB_Ramp3(t, colorA, colorB, colorC) three colors along t (float3s: Color.rgb); NTB_Wave(time, speed) 0..1 waving speed times a second; NTB_SafeDiv(a, b) never divides by 0.
The built material shows its code in the graph (the Custom node's comment bubble, titled with the material's name); the node's Code setting is what compiles.
Compile errors come back as "compile: the hlsl code: ..." with a hint; fix the code and call build_ui_material again with material set to rebuild it in place.

## Graph format
{blend: Translucent (default) | Additive (glows, light) | Masked (hard cut-outs), nodes: [{id, type, inputs: {Pin: ref}, props: {Setting: value}}], outputs: {FinalColor: ref, Opacity: ref}}.
A ref is "id" (the node's first output), "id.Output" ("g.Mask", "c.Color", "tex.A", "vc.RGB"), a number, [x, y], "#RRGGBB", "$uv" (the widget's UV, 0..1, (0,0) top left), "$time" (seconds), or {"param": "Name", "value": v} - a parameter an instance can change (a number makes a scalar, "#hex" a color, [x, y] a float2).
Widths: every value is float1-4. A scalar mixes with any width; a float4 meeting a float3 is masked to RGB for you; anything else must agree (ComponentMask with props {channels: "rg"} narrows, AppendVector widens). FinalColor is a color (float3); Opacity a 0..1 scalar.
The widget's tint and opacity (and particle colors) come in through VertexColor - it is multiplied in for you unless the graph uses VertexColor itself or says "tint": false.
Exact nodes, pins and output widths: list_options material_nodes (all), list_options material_nodes <Name> (one in detail).

## Macro nodes
One node each, expanded to a tested subgraph. Every input is optional (defaults below) and takes any ref. UV inputs default to the widget's UV.
- glow {UV, Center [0.5,0.5], Radius 0.5, Falloff 2} -> Mask: round glow, 1 at the center.
- soft_circle {UV, Center, Radius 0.4, Softness 0.1} -> Mask: a disc with a soft edge.
- ring {UV, Center, Radius 0.35, Width 0.04, Softness 0.02} -> Mask.
- rounded_rect {UV, Size [0.9,0.9], Radius 0.1, Softness 0.01} -> Mask, Distance.
- linear_gradient {UV, Angle 90, ColorA, ColorB} -> Color, T. Angle 0 = left to right, 90 = top to bottom, -90 = bottom to top.
- radial_gradient {UV, Center, Radius 0.5, ColorA (center), ColorB (edge), Falloff 1} -> Color, T.
- color_ramp {T, ColorA, ColorB, ColorC} -> Color: three colors along T 0..1.
- panning_noise {UV, Scale 4, Speed [0.05,0.02]} -> Value 0..1: drifting smooth noise.
- time_wave {Speed 1, Min 0, Max 1, Phase 0} -> Value: waves Speed times a second (pulses, breathing).
- uv_tile {UV, Tiling [1,1], Offset [0,0], Speed [0,0]} -> UV: tiled / scrolling coordinates for other macros.
- band {UV, Angle 30, Position 0.5, Width 0.15, Softness 0.1, Speed 0} -> Mask: a straight soft band; Speed > 0 sweeps it (shimmer).
- stripes {UV, Count 20, Angle 90, Width 0.5, Softness 0.1, Speed 0} -> Mask: repeating stripes (scanlines, hazard bars).
- remap {Value, InMin 0, InMax 1, OutMin 0, OutMax 1} -> Value, clamped.

## Example: card with a soft edge and drifting fog (HLSL, helpers)
{name: "FogCard", hlsl: {params: {Fill: "#1B1036", Fog: "#7C5CFF", Corner: 0.08, Softness: 0.004, FogSpeed: 0.05}, code: ["float d = NTB_SdRoundRect(uv, float2(0.5, 0.5), float2(0.5, 0.5), Corner);", "float fog = NTB_Fbm(uv * 3.0 + float2(time * FogSpeed, 0.0), 4);", "float3 c = lerp(Fill.rgb, Fog.rgb, fog * 0.6);", "return float4(c, NTB_Fill(d, Softness) * Fill.a);"]}}

## Example: pulsing ring (HLSL)
{name: "PulseRing", hlsl: {blend: "Additive", params: {Color: "#7C5CFF", Radius: 0.35, Width: 0.03, Speed: 1.5}, code: ["float d = distance(uv, float2(0.5, 0.5));", "float ring = 1.0 - smoothstep(0.0, Width, abs(d - Radius));", "float pulse = 0.6 + 0.4 * sin(6.2832 * time * Speed);", "return float4(Color.rgb, ring * pulse * Color.a);"]}}

## Example: scanlines over a gradient (HLSL)
{name: "ScanPanel", hlsl: {params: {Top: "#1B1036", Bottom: "#3A2A7A", Lines: 120.0, LineStrength: 0.15, Scroll: 0.5}, code: ["float3 c = lerp(Top.rgb, Bottom.rgb, uv.y);", "float s = 0.5 + 0.5 * sin(6.2832 * (uv.y * Lines + time * Scroll));", "c = c * (1.0 - LineStrength * s);", "return float4(c, 1.0);"]}}

## Example: soft glowing badge (node graph)
{name: "BadgeGlow", graph: {blend: "Additive", nodes: [{id: "g", type: "glow", inputs: {Radius: {param: "Size", value: 0.48}, Falloff: {param: "Falloff", value: 2.5}}}, {id: "pulse", type: "time_wave", inputs: {Speed: {param: "PulseSpeed", value: 0.6}, Min: 0.7, Max: 1}}, {id: "a", type: "Multiply", inputs: {A: "g.Mask", B: "pulse"}}, {id: "col", type: "Multiply", inputs: {A: {param: "Color", value: "#7C5CFF"}, B: "a"}}], outputs: {FinalColor: "col", Opacity: "a"}}}

## Example: scanline overlay (node graph)
{name: "Scanlines", graph: {nodes: [{id: "s", type: "stripes", inputs: {Count: {param: "Lines", value: 120}, Width: 0.35, Speed: {param: "Scroll", value: 0.5}}}, {id: "a", type: "Multiply", inputs: {A: "s.Mask", B: {param: "Strength", value: 0.15}}}], outputs: {FinalColor: {param: "Color", value: "#9EF0FF"}, Opacity: "a"}}}

## Example: gradient card with a sweeping shine (node graph)
{name: "ShineCard", graph: {nodes: [{id: "bg", type: "linear_gradient", inputs: {Angle: -90, ColorA: {param: "Bottom", value: "#1B1036"}, ColorB: {param: "Top", value: "#3A2A7A"}}}, {id: "shape", type: "rounded_rect", inputs: {Size: [1, 1], Radius: 0.08, Softness: 0.004}}, {id: "shine", type: "band", inputs: {Angle: 25, Width: 0.12, Speed: {param: "ShineSpeed", value: 0.25}}}, {id: "hi", type: "Multiply", inputs: {A: "shine", B: {param: "ShineStrength", value: 0.35}}}, {id: "rgb", type: "Add", inputs: {A: "bg.Color", B: "hi"}}], outputs: {FinalColor: "rgb", Opacity: "shape.Mask"}}}

## Example: a master plus specks (node graph on a master)
{name: "AuroraSpecks", from_master: "Aurora", graph: {nodes: [{id: "n", type: "panning_noise", inputs: {Scale: 40, Speed: [0, -0.03]}}, {id: "specks", type: "SmoothStep", inputs: {Min: 0.78, Max: 0.82, Value: "n"}}, {id: "add", type: "Add", inputs: {A: "master.FinalColor", B: "specks"}}], outputs: {FinalColor: "add"}}}

## What gets fixed for you
Reported under "fixed" (read it, and write it that way next time): node names models often use (Lerp is LinearInterpolate, Mul is Multiply, TexCoord is TextureCoordinate), pin names (Edge0 / Edge1 / X on SmoothStep, X / Y on A / B nodes, T on a Lerp), a float4 meeting a float3, a float2 going where a float3 is needed (appended with 0), a SmoothStep with a constant Min above Max (it only rises in Unreal: swapped, OneMinus after), a channel picked from a one-output node ("n.R" - masked out), a color going into Opacity, Opacity outside 0..1 (Saturate), BaseColor in a UI material (it is FinalColor), Opacity in a Masked material (OpacityMask), a Noise with no Position, a library function's UV left unwired, a Custom node's code without return, the same parameter made twice, and unused nodes. Listed under "added" (not slips): pow / sqrt / log / smoothstep made safe, half made float, the tint, Saturate on Opacity.

## When a build fails
Graph problems: nothing was built - fix every listed problem and call again. Compile problems: the material was made; each error names the node (inside a macro: "g/d (inside g)") with a hint - fix and call build_ui_material again with material set to rebuild it in place. A Custom node (HLSL) is the escape hatch for dense maths: {id, type: "Custom", code: "return saturate(1 - length(uv - 0.5) * 2);", output: "float1", inputs: {uv: "$uv"}} - the inputs are the code's variable names.

## Where materials go
A shape's fill or stroke (set_shape_paint material), an Image's brush, a button's Design Target, a Niagara sprite renderer. Animate parameters with Material Scalar / Vector tracks or Animation Parameters. Library Material Functions (SoftCircle, RoundedRectSDF, PanningNoise, ColorRamp) can also be called: {type: "MaterialFunctionCall", function: "SoftCircle", inputs: {Radius: 0.4}} - outputs by name ("c.Result").
