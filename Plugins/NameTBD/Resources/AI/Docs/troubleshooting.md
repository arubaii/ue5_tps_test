# Troubleshooting

## A field didn't take
Read the warning: it lists the fields the object has. Use list_options fields <Class> for types and meanings; paths go through structs ("Fill.Color") and lists ("Keys[1].Value"); "List+" appends. Enums take their name or display name.

## Nothing shows
Is it behind something? Overlay children draw in order - move_widget with an index. Is it collapsed or zero-sized? A shape without a child uses Desired Size. Colors with alpha 00 are invisible.

## Particles don't draw
Only CPU emitters with sprite or ribbon renderers draw in UI; the Widget Blueprint's compile lists what can't. The renderer's material must be a UI material (or remapped). Check Origin and Particle Scale.

## Material compile errors
Each error names the node; a common one is a type mismatch (float3 into a float2 input - add a ComponentMask). UI materials must output FinalColor; translucency needs Opacity.

## The chat says the model has no tool support
Local models must support function calling (tools). Pick a model that does, or use another provider.
