

#include "UI/Cards/InfoCardWidget_Base.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Kismet/KismetMathLibrary.h"


FVector2D UInfoCardWidgetBase::GetCardDesiredSize() const
{
	// Mirrors the "Get Desired Size" node used in the BP graph on Self.
	return GetDesiredSize();
}

FVector2D UInfoCardWidgetBase::PositionToIcon(EDirection2D TryPosition, float OffsetX, float OffsetY) const
{
	const FVector2D CardSize = GetCardDesiredSize();
	const FVector2D IconCenter = IconPositon + (IconFrameSize * 0.5f);

	FVector2D Result = FVector2D::ZeroVector;

	switch (TryPosition)
	{
	case EDirection2D::Up:
		// Centered above the icon; OffsetY extends the gap, OffsetX shifts horizontally.
		Result.X = IconCenter.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = IconPositon.Y - CardSize.Y - AnchorPadding - OffsetY;
		break;

	case EDirection2D::Down:
		// Centered below the icon; OffsetY extends the gap, OffsetX shifts horizontally.
		Result.X = IconCenter.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = IconPositon.Y + IconFrameSize.Y + AnchorPadding + OffsetY;
		break;

	case EDirection2D::Left:
		// Centered left of the icon; OffsetX extends the gap, OffsetY shifts vertically.
		Result.X = IconPositon.X - CardSize.X - AnchorPadding - OffsetX;
		Result.Y = IconCenter.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EDirection2D::Right:
		// Centered right of the icon; OffsetX extends the gap, OffsetY shifts vertically.
		Result.X = IconPositon.X + IconFrameSize.X + AnchorPadding + OffsetX;
		Result.Y = IconCenter.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;
	}

	if (bClampToViewport)
	{
		if (UWorld* World = GetWorld())
		{
			const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(World);
			Result.X = FMath::Clamp(Result.X, 0.0, FMath::Max(0.0, ViewportSize.X - CardSize.X));
			Result.Y = FMath::Clamp(Result.Y, 0.0, FMath::Max(0.0, ViewportSize.Y - CardSize.Y));
		}
	}

	return Result;
}

FVector2D UInfoCardWidgetBase::PositionToCursor(EInfoCardCursorPosition Position, float OffsetX, float OffsetY) const
{
	const FVector2D CardSize = GetCardDesiredSize();

	FVector2D CursorPos = FVector2D::ZeroVector;
	if (UWorld* World = GetWorld())
	{
		CursorPos = UWidgetLayoutLibrary::GetMousePositionOnViewport(World);
	}

	FVector2D Result = CursorPos;

	switch (Position)
	{
	case EInfoCardCursorPosition::UpRight:
		// Card sits above and to the right of the cursor.
		Result.X = CursorPos.X + OffsetX;
		Result.Y = CursorPos.Y - CardSize.Y - OffsetY;
		break;

	case EInfoCardCursorPosition::UpLeft:
		// Card sits above and to the left of the cursor.
		Result.X = CursorPos.X - CardSize.X - OffsetX;
		Result.Y = CursorPos.Y - CardSize.Y - OffsetY;
		break;

	case EInfoCardCursorPosition::DownRight:
		// Card sits below and to the right of the cursor.
		Result.X = CursorPos.X + OffsetX;
		Result.Y = CursorPos.Y + OffsetY;
		break;

	case EInfoCardCursorPosition::DownLeft:
		// Card sits below and to the left of the cursor.
		Result.X = CursorPos.X - CardSize.X - OffsetX;
		Result.Y = CursorPos.Y + OffsetY;
		break;

	case EInfoCardCursorPosition::UpCenter:
		// Card sits directly above the cursor, horizontally centered on it.
		Result.X = CursorPos.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = CursorPos.Y - CardSize.Y - OffsetY;
		break;

	case EInfoCardCursorPosition::DownCenter:
		// Card sits directly below the cursor, horizontally centered on it.
		Result.X = CursorPos.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = CursorPos.Y + OffsetY;
		break;

	case EInfoCardCursorPosition::LeftCenter:
		// Card sits directly left of the cursor, vertically centered on it.
		Result.X = CursorPos.X - CardSize.X - OffsetX;
		Result.Y = CursorPos.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EInfoCardCursorPosition::RightCenter:
		// Card sits directly right of the cursor, vertically centered on it.
		Result.X = CursorPos.X + OffsetX;
		Result.Y = CursorPos.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;
	}

	return Result;
}