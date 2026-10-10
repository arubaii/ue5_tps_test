# Recipes

## Greeting card with aurora and bubbles
create_widget_blueprint {name: GreetingCard, size: [600, 360]} gives a Size Box "Frame" holding an Overlay "Content". add_widgets into Content, in this order (later = on top): {role: card}, {role: aurora} (a gradient rising from the bottom edge, rounded to the card's bottom corners), {role: particles, props: {NiagaraSystem: <path from create_niagara_ui_system template RisingBubbles>}} and {role: display, text: "Hello User", slot: {align: top_left, padding: "@space.5"}}. For a living aurora: create_material_instance {master: Aurora} and set_shape_paint {widget: Aurora, material: <it>}. animate_widget {widget: Content, recipe: pop_in}.

## A button row
add_widgets {widgets: [{role: row, name: Actions, slot: {align: bottom_right, padding: "@space.5"}, children: [{button: generic, label: "Not now", props: {FillColor: "@raised"}}, {button: generic, label: "Continue", slot: {padding: [12, 0, 0, 0]}}]}]}. Ready-made buttons take the theme's accent and motion by themselves.

## Settings menu
A panel (role panel) holding a VerticalBox of rows; each row a HorizontalBox with a body label (fill) and a control: button pill_switch for on/off, choice for options ("< Medium >"), segmented for 2-4 options side by side. Menu entries: button menu_item.

## Leaderboard (data driven)
View Model with an array Rows (struct: PlayerId, Name, Score, bIsLocalPlayer). Screen: a FlowList (StylePreset a vertical list, snapping off) and on the screen's root a Widget Data Binder: add_list_binding {list_bindings: [{ViewModelField: Rows, ListWidget: <FlowList name>, EntryWidgetClass: <row WBP>, KeyField: PlayerId, PositionField: Rank}]}. The row Widget Blueprint has its own binder with triggers: Rank Went Down -> Play "Promote", Score Count Into Field DisplayScore. add_sample_set rows like "PlayerId=1, Name=Ann, Score=1200" so the designer's Data Preview plays it.

## Toast / notification
A pill (role pill) holding a row: an icon and a body text. animate_widget recipe slide_up_in when on_visible, and another animate_widget with when out (the exit plays the entrance backwards).

## Hover and press feedback
Use a ready-made button (it already animates its states). For any widget inside a Context Menu Button, its Shape Targets and Design Targets set looks per state (Hover, Pressed, Enabled) with springs.
