# Shapes and Shape Box

## Shapes
RectangleShape, EllipseShape, TriangleShape, StarShape, PolygonShape and CustomShape are vector widgets: crisp at any size, with a Fill and a Stroke. Fill and Stroke.Paint are FWidgetShapePaint: Type Color (one Color), LinearGradient (GradientStops, GradientAngle: 0 left to right, 90 top to bottom, -90 bottom to top), RadialGradient (GradientCenter, GradientRadius) or Image (a texture or material brush; Image Mapping Along Outline runs a material along a stroke for edge glows). Stroke: bEnabled, Width, Align Inside / Center / Outside, Join, Cap, DashLength, DashGap. Trim (Start, End, Offset) draws part of the outline - progress rings. Fit, ShapeScale, ShapeRotation, DesiredSize.

## Shapes hold a child
Every shape holds one widget drawn on top, like a Border: a label in a pill, a count in a badge, an icon in a circle. Content Area: Inscribed (largest rectangle inside the outline - default), Bounds, Inside Stroke. Size To Content (default on) grows the shape around its child. The child's slot (Shape Slot) has Padding and alignment. Hit Test Outline makes only the outline's inside clickable.

## Rectangle
CornerRounding (px), bFullyRounded (pill), bIndividualCorners + CornerRadii [top-left, top-right, bottom-right, bottom-left].

## Shape Box
ShapeBox bends its content into a shape (warps and clips it) - different from a shape, which draws a vector background under its content.
