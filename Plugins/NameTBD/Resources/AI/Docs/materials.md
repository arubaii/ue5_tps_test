# UI materials

## Masters (instant)
The library's master UI materials are built from definitions in Resources/AI/Materials the first time they're needed (or shipped built in /NameTBD/AI/Materials): Aurora (bands of light rising from the bottom, animated), SoftGlow (round glow, can breathe), Shimmer (sweeping light band), Gradient (three colors at an angle, can drift), NoiseFog (drifting fog), Vignette (edges darkened), RingPulse (expanding rings), Dissolve (noisy reveal by Progress), SoftDot and Bubble (particles). create_material_instance makes an instance with parameters; set_material_parameters changes them later. Instances can't break and need no shader compile.

## New effects
build_ui_material builds a whole material from a node graph: domain UI, blend Translucent / Additive / Masked, nodes by type with props and inputs, outputs FinalColor and Opacity. Multiply by VertexColor (RGB and A) so widget tints and particle colors apply. Library Material Functions keep graphs short: SoftCircle (UV, Radius, Softness -> Result), RoundedRectSDF (UV, HalfSize, Radius, Softness -> Mask, Distance), PanningNoise (UV, Scale, Speed -> Result), ColorRamp (T, ColorA, ColorB, ColorC -> Color). Compile errors come back; fix the graph and rebuild with 'material' set.

## Where materials go
A shape's fill or stroke (set_shape_paint material), an Image's brush, a button's Design Target, a Niagara sprite renderer. Animate parameters with Material Scalar / Vector tracks or Animation Parameters.
