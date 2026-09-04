#include "W_DragVisualBase.h"
#include "DeveloperSettings/DragVisualSettings.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Math/UnrealMathUtility.h"

void UW_DragVisualBase::InitializeDragPosition(FVector2D IconSize)
{
    if (GetWorld())
    {
        FVector2D MousePos = UWidgetLayoutLibrary::GetMousePositionOnViewport(this);
        CurrentTranslation = MousePos - (IconSize / 2.0f);
        SetRenderTranslation(CurrentTranslation);
        bHasInitializedPosition = true;
    }
}

void UW_DragVisualBase::SetDragVisualPosition(FVector2D IconSize)
{
    if (!GetWorld()) return;

    // Fetch the target position (Mouse Position centered on Icon)
    FVector2D MousePos = UWidgetLayoutLibrary::GetMousePositionOnViewport(this);
    FVector2D TargetTranslation = MousePos - (IconSize / 2.0f);

    if (!bHasInitializedPosition)
    {
        InitializeDragPosition(IconSize);
        return;
    }

    float DeltaTime = GetWorld()->GetDeltaSeconds();
    
    // Fetch global developer settings
    const UDragVisualSettings* Settings = GetDefault<UDragVisualSettings>();
    EDragVisualStyle CurrentStyle = Settings ? Settings->DragStyle : EDragVisualStyle::Default;

    switch (CurrentStyle)
    {
        case EDragVisualStyle::Default:
        {
            CurrentTranslation = TargetTranslation;
            CurrentVelocity = FVector2D::ZeroVector;
            SetRenderTransformAngle(0.f);
            break;
        }
        case EDragVisualStyle::SmoothFollow:
        {
            CurrentTranslation = FMath::Vector2DInterpTo(CurrentTranslation, TargetTranslation, DeltaTime, Settings->InterpSpeed);
            SetRenderTransformAngle(0.f);
            break;
        }
        case EDragVisualStyle::BouncyElastic:
        {
            // Hooke's Law Spring Physics
            FVector2D Displacement = CurrentTranslation - TargetTranslation;
            FVector2D SpringForce = -Settings->SpringStiffness * Displacement;
            FVector2D DampingForce = -Settings->SpringDamping * CurrentVelocity;
            
            CurrentVelocity += (SpringForce + DampingForce) * DeltaTime;
            CurrentTranslation += CurrentVelocity * DeltaTime;
            
            SetRenderTransformAngle(0.f);
            break;
        }
        case EDragVisualStyle::DynamicVelocityTilt:
        {
            // Use Spring physics for positioning
            FVector2D Displacement = CurrentTranslation - TargetTranslation;
            FVector2D SpringForce = -(Settings->SpringStiffness * 0.85f) * Displacement;
            FVector2D DampingForce = -(Settings->SpringDamping * 1.1f) * CurrentVelocity;
            
            CurrentVelocity += (SpringForce + DampingForce) * DeltaTime;
            CurrentTranslation += CurrentVelocity * DeltaTime;

            // Calculate tilt based on Horizontal (X) Velocity
            float TargetAngle = FMath::Clamp(CurrentVelocity.X * Settings->TiltMultiplier, -Settings->MaxTiltAngle, Settings->MaxTiltAngle);
            
            // Smoothly interpolate the rotation to prevent jittering
            float CurrentAngle = GetRenderTransform().Angle;
            float NewAngle = FMath::FInterpTo(CurrentAngle, TargetAngle, DeltaTime, Settings->InterpSpeed);
            
            SetRenderTransformAngle(NewAngle);
            break;
        }
    }

    SetRenderTranslation(CurrentTranslation);
}