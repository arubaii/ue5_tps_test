# Troubleshooting

## A field didn't take
Read the warning: it lists the fields the object has. Use list_options fields <Class> for types and meanings; paths go through structs ("Fill.Color") and lists ("Keys[1].Value"); "List+" appends. Enums take their name or display name.

## Nothing shows
Is it behind something? Overlay children draw in order - move_widget with an index. Is it collapsed or zero-sized? A shape without a child uses Desired Size. Colors with alpha 00 are invisible.

## Particles don't draw
Only CPU emitters with sprite or ribbon renderers draw in UI; the Widget Blueprint's compile lists what can't. The renderer's material must be a UI material (or remapped). Check Origin and Particle Scale.

## Material compile errors
build_ui_material checks a graph before building it, so most mistakes come back at once as "errors" tied to node ids, with nothing built - fix them all and call again (validate_material checks without building). Compile errors after that name the node (a macro's inner node reads "g/d (inside g)") and carry a hint; the usual one is a width mismatch (a float3 into a float2 input - ComponentMask it). Rebuild in place with build_ui_material's material set. When a master can do the look, an instance (create_material_instance) never fails.
"the NTB_ helpers aren't available": the plugin's NameTBDShaders module (it registers the helper shader file at editor start) didn't load - the user rebuilds the plugin and restarts the editor; until then write the maths inline (no NTB_ calls).

## The chat says the model has no tool support
Local models must support function calling (tools). Pick a model that does, or use another provider.
