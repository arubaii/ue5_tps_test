// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/Widgets/Shapes/ShapeWidget.h"

/**
 * The ready-made buttons' theme: dark surfaces, a faint light outline, a blue accent, light text. Every value lands in a
 * button's Background Look, Shape Targets and Design Targets when it is made, so a project restyles a button there - or
 * changes these and makes its buttons again.
 */
namespace StyledButtonTheme
{
	inline FLinearColor Rgb(uint8 R, uint8 G, uint8 B, uint8 A = 255)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B, A));
	}

	inline FLinearColor Surface() { return Rgb(30, 33, 40, 235); }
	inline FLinearColor SurfaceHover() { return Rgb(46, 51, 61, 245); }
	inline FLinearColor SurfacePressed() { return Rgb(22, 24, 29, 255); }
	inline FLinearColor Line() { return Rgb(255, 255, 255, 40); }
	inline FLinearColor LineHover() { return Rgb(255, 255, 255, 120); }
	inline FLinearColor Accent() { return Rgb(61, 139, 255); }
	inline FLinearColor Text() { return Rgb(242, 244, 248); }
	inline FLinearColor TextDim() { return Rgb(150, 160, 176); }
	inline FLinearColor RowHover() { return FLinearColor(1.f, 1.f, 1.f, 0.06f); }
	inline FLinearColor Clear() { return FLinearColor(1.f, 1.f, 1.f, 0.f); }

	/** A shape painted with a flat Fill and no outline. */
	inline void Solid(UShapeWidget& Shape, const FLinearColor& Fill)
	{
		Shape.bFill = true;
		Shape.Fill.Type = EWidgetShapePaintType::Color;
		Shape.Fill.Color = Fill;
		Shape.Stroke.bEnabled = false;
	}

	/** An outline of Width, inside the shape, colored Color. */
	inline void Outline(UShapeWidget& Shape, float Width, const FLinearColor& Color, EWidgetShapeStrokeAlign Align = EWidgetShapeStrokeAlign::Inside)
	{
		Shape.Stroke.bEnabled = true;
		Shape.Stroke.Width = Width;
		Shape.Stroke.Align = Align;
		Shape.Stroke.Paint.Type = EWidgetShapePaintType::Color;
		Shape.Stroke.Paint.Color = Color;
	}

	/** Only an outline: a ring, a frame, a line. */
	inline void Hollow(UShapeWidget& Shape, float Width, const FLinearColor& Color, EWidgetShapeStrokeAlign Align = EWidgetShapeStrokeAlign::Inside)
	{
		Shape.bFill = false;
		Outline(Shape, Width, Color, Align);
	}
}
