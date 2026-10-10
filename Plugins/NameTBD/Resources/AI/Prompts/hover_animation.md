# Make a hover animation
> Give the selected widget hover and press feedback.
Arguments: style - the feel (snappy, bouncy, subtle...)
Give the widget selected in the open Widget Blueprint ({{style}}) hover and press feedback. Call get_context first. If it is a button (a Context Menu Button or a ready-made one), set its Shape / Design Targets or Style Motion; otherwise wrap the idea in a ready-made generic button or use a Widget Animation Component with a state machine on Hovered / Pressed. Keep it one or two undo steps.
