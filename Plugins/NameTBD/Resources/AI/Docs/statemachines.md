# State machines

## What they are
A Widget Animation State Machine (WASM_ asset) picks which animation plays from inputs: Inputs (Bool, Float, Integer, Choice, Vector 2D, Trigger, Press And Hold; built-ins Hovered, Pressed, Focused, Selected, Disabled), Layers (Override / Additive, Weight, masks), States (Plays: Single Animation / Blend 1D / Blend 2D, Playback Once / Loop / Ping Pong / Hold End Pose, Speed Input, Parameters From Inputs, On Enter / On Exit actions) and Transitions (Conditions, Blend Tween / Spring / Cut, Exit Time, priority, random).

## Playing one
A Widget Animation Component's State Machines entries play machines on widgets (Target Widgets, input overrides, Start On Construct). Set inputs from Blueprint (Set State Machine Bool / Number / Trigger...) or drive them from a Flow List or a View Model.

## Building with tools
create_asset {class: WidgetAnimStateMachineAsset} then set_asset with Inputs, Layers[0].States (+ appends) and Layers[0].Transitions; list_options fields FWidgetAnimMachineState / FWidgetAnimMachineTransition / FWidgetAnimMachineCondition for the exact fields. The State Machine editor (double-click the asset) shows the graph and previews it.
