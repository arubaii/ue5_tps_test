// Copyright AR GameSystems 2026. All rights reserved.

#include "DiegeticScreenComponent.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "CollisionQueryParams.h"
#include "Components/BackgroundBlur.h"
#include "Components/MeshComponent.h"
#include "Components/Widget.h"
#include "DiegeticInteractorComponent.h"
#include "DiegeticUISettings.h"
#include "DiegeticUISubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Layout/Geometry.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "Serialization/CustomVersion.h"
#include "UObject/ObjectKey.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

#if WITH_EDITOR
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "PhysicsEngine/PhysicsSettings.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogDiegeticScreen, Log, All);

#define LOCTEXT_NAMESPACE "DiegeticScreenComponent"

namespace DiegeticScreenPrivate
{
	/**
	 * Which screen shows which widget: by the User Widget a screen shows, and by the window it draws that widget in (for widgets
	 * whose outer chain doesn't lead to it - made at runtime and added to a panel on it). Game thread only.
	 */
	struct FLookups
	{
		TMap<TObjectKey<UUserWidget>, TWeakObjectPtr<UDiegeticScreenComponent>> ByWidget;
		TMap<const SWidget*, TPair<TWeakPtr<SWindow>, TWeakObjectPtr<UDiegeticScreenComponent>>> ByWindow;
		int32 NumScreens = 0;
	};

	FLookups& GetLookups()
	{
		static FLookups Lookups;
		return Lookups;
	}

	/** The state machine inputs a screen sets on its widget - see bDriveStateMachineInputs. */
	const FName ScreenAwakeInput(TEXT("ScreenAwake"));
	const FName ScreenAimedAtInput(TEXT("ScreenAimedAt"));
	const FName ScreenInUseInput(TEXT("ScreenInUse"));
	const FName ScreenDistanceInput(TEXT("ScreenDistance"));

	/**
	 * Versions of a saved Diegetic Screen. AutomaticMaterial: Material Mode came in, Automatic for new screens - one saved before
	 * keeps the engine's material it was drawn with (Engine Default).
	 */
	struct FScreenVersion
	{
		enum Type
		{
			Initial = 0,
			AutomaticMaterial = 1,

			LatestPlusOne,
			Latest = LatestPlusOne - 1
		};
		static const FGuid Guid;
	};
	const FGuid FScreenVersion::Guid(0x6C1E2A3B, 0x4D5F4E21, 0x9A8B7C6D, 0x13572468);
	FCustomVersionRegistration GRegisterScreenVersion(FScreenVersion::Guid, FScreenVersion::Latest, TEXT("DiegeticScreenVer"));

	/** Screen Look's material parameters, and the ones the screen sets itself. */
	const FName BrightnessParam(TEXT("Brightness"));
	const FName IgnoreSceneExposureParam(TEXT("IgnoreSceneExposure"));
	const FName PhosphorColorParam(TEXT("PhosphorColor"));
	const FName MonochromeParam(TEXT("Monochrome"));
	const FName GlowParam(TEXT("Glow"));
	const FName GlowRadiusParam(TEXT("GlowRadius"));
	const FName ScanlineIntensityParam(TEXT("ScanlineIntensity"));
	const FName ScanlineCountParam(TEXT("ScanlineCount"));
	const FName ScanlineSpeedParam(TEXT("ScanlineSpeed"));
	const FName RollingBarParam(TEXT("RollingBar"));
	const FName CurvatureParam(TEXT("Curvature"));
	const FName ChromaticAberrationParam(TEXT("ChromaticAberration"));
	const FName VignetteParam(TEXT("Vignette"));
	const FName FlickerParam(TEXT("Flicker"));
	const FName NoiseParam(TEXT("Noise"));
	const FName PixelGridParam(TEXT("PixelGrid"));
	const FName AdditiveParam(TEXT("Additive"));
	const FName EdgeFadeParam(TEXT("EdgeFade"));
	const FName GlitchParam(TEXT("Glitch"));

	/** Glass's material parameters (the glass material's). */
	const FName GlassPaneParam(TEXT("GlassPane"));
	const FName GlassBlurParam(TEXT("GlassBlur"));
	const FName GlassFrostParam(TEXT("GlassFrost"));
	const FName GlassRefractionParam(TEXT("GlassRefraction"));
	const FName GlassBevelWidthParam(TEXT("GlassBevelWidth"));
	const FName GlassRippleParam(TEXT("GlassRipple"));
	const FName GlassChromaticAberrationParam(TEXT("GlassChromaticAberration"));
	const FName GlassTintParam(TEXT("GlassTint"));
	const FName GlassBrightnessParam(TEXT("GlassBrightness"));
	const FName GlassSheenParam(TEXT("GlassSheen"));
	const FName GlassEdgeHighlightParam(TEXT("GlassEdgeHighlight"));
	const FName GlassCornerRadiusParam(TEXT("GlassCornerRadius"));

	const FName ScreenPixelsParam(TEXT("ScreenPixels"));
	const FName ShowBackFaceParam(TEXT("ShowBackFace"));
	const FName UVRegionParam(TEXT("UVRegion"));
	const FName BlurRegionCountParam(TEXT("BlurRegionCount"));

	/** The parameters the Widget Component sets on its own material, set on a Mesh Surface's too. */
	const FName SlateUIParam(TEXT("SlateUI"));
	const FName TintColorAndOpacityParam(TEXT("TintColorAndOpacity"));
	const FName OpacityFromTextureParam(TEXT("OpacityFromTexture"));

	/** Background Blur widgets a screen's material blurs behind, at most. */
	constexpr int32 MaxBlurRegions = 8;

	/** BlurRegion0..7 (left, top, right, bottom as 0..1 of the screen) and BlurShape0..7 (corner radius, blur radius - widget pixels). */
	const FName& BlurRegionParam(int32 Index)
	{
		static const FName Names[MaxBlurRegions] = { TEXT("BlurRegion0"), TEXT("BlurRegion1"), TEXT("BlurRegion2"), TEXT("BlurRegion3"),
			TEXT("BlurRegion4"), TEXT("BlurRegion5"), TEXT("BlurRegion6"), TEXT("BlurRegion7") };
		return Names[Index];
	}
	const FName& BlurShapeParam(int32 Index)
	{
		static const FName Names[MaxBlurRegions] = { TEXT("BlurShape0"), TEXT("BlurShape1"), TEXT("BlurShape2"), TEXT("BlurShape3"),
			TEXT("BlurShape4"), TEXT("BlurShape5"), TEXT("BlurShape6"), TEXT("BlurShape7") };
		return Names[Index];
	}

	/**
	 * A Background Blur's Blur Strength in Slate units, to a blur radius in the widget's pixels: Slate's kernel spreads about
	 * this far for a given strength.
	 */
	constexpr float BlurStrengthToRadius = 2.f;

	/** How often the Background Blur widgets on a screen are looked for again - ones added at runtime (seconds). */
	constexpr double BlurCollectInterval = 2.0;

	/** A Background Blur's Blur Strength and corner radius, by reflection (the getters differ between engine versions). */
	float ReadBlurStrength(const UBackgroundBlur* Blur)
	{
		static const FFloatProperty* Property = FindFProperty<FFloatProperty>(UBackgroundBlur::StaticClass(), TEXT("BlurStrength"));
		return Blur && Property ? Property->GetPropertyValue_InContainer(Blur) : 0.f;
	}
	float ReadBlurCorner(const UBackgroundBlur* Blur)
	{
		static const FStructProperty* Property = FindFProperty<FStructProperty>(UBackgroundBlur::StaticClass(), TEXT("CornerRadius"));
		if (!Blur || !Property || Property->Struct != TBaseStructure<FVector4>::Get())
		{
			return 0.f;
		}
		return static_cast<float>(Property->ContainerPtrToValuePtr<FVector4>(Blur)->X);
	}

	/** What makes the plugin's screen materials in the editor - see UDiegeticScreenComponent::SetEditorMaterialProvider. */
	TFunction<UMaterialInterface*(EDiegeticMaterialVariant)>& GetEditorMaterialProvider()
	{
		static TFunction<UMaterialInterface*(EDiegeticMaterialVariant)> Provider;
		return Provider;
	}

	/** Whether a material is one of the plugin's screen materials (wherever they were made). */
	bool IsPluginScreenMaterial(const UMaterialInterface* Material)
	{
		return Material && Material->GetPathName().Contains(TEXT("/Diegetic/M_DiegeticScreen"));
	}

	/** ScreenDistance is only set again once it has moved this far (cm): it is read by machines, not drawn. */
	constexpr float DistanceStep = 5.f;

	/** Seconds a screen falling asleep keeps redrawing before its widget's exit must have shown itself to be playing. */
	constexpr double SleepGrace = 0.25;

	/** Texture sizes past which the map check warns. */
	constexpr double LargeTexturePixels = 4096.0 * 4096.0;
}

UDiegeticScreenComponent::UDiegeticScreenComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A screen gives light; it doesn't cast shadow or catch overlaps.
	CastShadow = false;
	SetGenerateOverlapEvents(false);
}

void UDiegeticScreenComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(DiegeticScreenPrivate::FScreenVersion::Guid);
}

void UDiegeticScreenComponent::PostLoad()
{
	Super::PostLoad();

	// Saved before Material Mode: it keeps the engine's material it was drawn with.
	if (GetLinkerCustomVersion(DiegeticScreenPrivate::FScreenVersion::Guid) < DiegeticScreenPrivate::FScreenVersion::AutomaticMaterial)
	{
		MaterialMode = EDiegeticMaterialMode::EngineDefault;
	}
}

// =====================================================================================================
// Materials
// =====================================================================================================

void UDiegeticScreenComponent::SetEditorMaterialProvider(TFunction<UMaterialInterface*(EDiegeticMaterialVariant)> InProvider)
{
	DiegeticScreenPrivate::GetEditorMaterialProvider() = MoveTemp(InProvider);
}

UMaterialInterface* UDiegeticScreenComponent::GetAutomaticMaterial(EDiegeticMaterialVariant Variant)
{
	// The editor: its own (made when first needed - null until then, and the engine's material draws meanwhile). Nothing is
	// loaded by path here: a screen asks every time it registers, which a dragged-in actor does every frame.
	if (const TFunction<UMaterialInterface*(EDiegeticMaterialVariant)>& Provider = DiegeticScreenPrivate::GetEditorMaterialProvider())
	{
		return Provider(Variant);
	}

	// A packaged game: the materials Project Settings names (cooked with /NameTBD/Diegetic - see the plugin's DefaultGame.ini),
	// looked for once.
	static TWeakObjectPtr<UMaterialInterface> Loaded[3];
	static bool bTried[3] = {};
	TWeakObjectPtr<UMaterialInterface>& Cached = Loaded[static_cast<int32>(Variant)];
	if (!Cached.IsValid() && !bTried[static_cast<int32>(Variant)])
	{
		bTried[static_cast<int32>(Variant)] = true;
		const TSoftObjectPtr<UMaterialInterface>& Path = UDiegeticUISettings::Get().GetScreenMaterial(Variant);
		Cached = Path.IsNull() ? nullptr : Path.LoadSynchronous();
		if (!Cached.IsValid())
		{
			static bool bWarned = false;
			if (!bWarned)
			{
				bWarned = true;
				UE_LOG(LogDiegeticScreen, Warning, TEXT("Diegetic Screen: the plugin's screen material %s isn't there - screens set to Material Automatic use the engine's. Open the project in the editor once (it makes them), and cook /NameTBD/Diegetic."), *Path.ToString());
			}
		}
	}
	return Cached.Get();
}

EDiegeticMaterialVariant UDiegeticScreenComponent::GetWantedVariant() const
{
	// Glass only when asked for - a pane of glass, or Background Blur widgets to blur behind. A see-through widget alone is drawn
	// over the world as it is.
	if (Glass.IsEnabled() || (Glass.bBlurBackgroundBlurWidgets && bHasBlurWidgets))
	{
		return EDiegeticMaterialVariant::Glass;
	}
	return GetBlendMode() == EWidgetBlendMode::Opaque ? EDiegeticMaterialVariant::Opaque : EDiegeticMaterialVariant::Panel;
}

void UDiegeticScreenComponent::ApplyMaterialSetup()
{
	AppliedVariant = GetWantedVariant();

	if (MaterialMode == EDiegeticMaterialMode::Automatic)
	{
		if (UMaterialInterface* Automatic = GetAutomaticMaterial(AppliedVariant))
		{
			if (GetMaterial(0) != Automatic)
			{
				SetMaterial(0, Automatic);
			}
		}
	}
	else if (MaterialMode == EDiegeticMaterialMode::EngineDefault)
	{
		// A plugin material an earlier mode left in Materials goes: the engine picks its own by Blend Mode again.
		if (OverrideMaterials.IsValidIndex(0) && DiegeticScreenPrivate::IsPluginScreenMaterial(OverrideMaterials[0]))
		{
			SetMaterial(0, nullptr);
		}
	}

	// A Mesh Surface only in a game world: the editor's mesh keeps its own material (a material instance made here mustn't be
	// saved with the level).
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld() && DisplaySettings.Display == EDiegeticDisplay::MeshSurface)
	{
		SetUpMeshSurface();
	}
	else
	{
		TearDownMeshSurface();
		// Nor is a Mesh Surface's own panel drawn in the editor: it sits on the mesh's display face and fights it for depth (the
		// face flickers). The mesh shows its own slot until play. Game worlds: set by SetUpMeshSurface / TearDownMeshSurface.
		if (World && !World->IsGameWorld())
		{
			const bool bShowPanel = DisplaySettings.Display != EDiegeticDisplay::MeshSurface;
			if (bRenderInMainPass != bShowPanel)
			{
				bRenderInMainPass = bShowPanel;
				bRenderInDepthPass = bShowPanel;
				MarkRenderStateDirty();
			}
		}
	}
	ApplyScreenLook(/*bForce=*/true);
}

// =====================================================================================================
// Mesh Surface
// =====================================================================================================

UMeshComponent* UDiegeticScreenComponent::GetDisplayMesh() const
{
	if (DisplaySettings.Display != EDiegeticDisplay::MeshSurface)
	{
		return nullptr;
	}
	return Cast<UMeshComponent>(DisplaySettings.Mesh.GetComponent(GetOwner()));
}

UPrimitiveComponent* UDiegeticScreenComponent::GetDisplayPrimitive() const
{
	if (UMeshComponent* Mesh = GetDisplayMesh())
	{
		return Mesh;
	}
	return const_cast<UDiegeticScreenComponent*>(this);
}

void UDiegeticScreenComponent::SetUpMeshSurface()
{
	UMeshComponent* Mesh = GetDisplayMesh();
	// Keep Mesh Material: drawn over the whole mesh (its overlay material), its own materials left alone - on one slot when one is
	// named (only its faces are clicked), else on any. Otherwise the slot (the first, when none is named) shows the screen alone.
	const bool bOverlay = DisplaySettings.bKeepMeshMaterial;
	const bool bAnySlot = bOverlay && DisplaySettings.MaterialSlot.IsNone();
	const int32 Slot = !Mesh || bAnySlot ? INDEX_NONE : DisplaySettings.MaterialSlot.IsNone() ? 0 : Mesh->GetMaterialIndex(DisplaySettings.MaterialSlot);
	if (!Mesh || (!bAnySlot && (Slot == INDEX_NONE || Slot >= Mesh->GetNumMaterials())))
	{
		TearDownMeshSurface();
		UE_LOG(LogDiegeticScreen, Warning, TEXT("Diegetic Screen %s: Mesh Surface has no mesh, or the mesh has no slot %s - nothing is shown."),
			*GetPathName(), *DisplaySettings.MaterialSlot.ToString());
		return;
	}

	// What it draws with: Material Mode's (Automatic's, Custom's element 0, or the engine's by Blend Mode).
	UMaterialInterface* Base = MaterialMode == EDiegeticMaterialMode::Automatic ? GetAutomaticMaterial(AppliedVariant) : nullptr;
	if (!Base)
	{
		Base = GetMaterial(0);
	}
	if (!Base)
	{
		return;
	}

	if (SurfaceMesh.Get() != Mesh || SurfaceSlot != Slot || bSurfaceOverlay != bOverlay)
	{
		TearDownMeshSurface();
		SurfaceMesh = Mesh;
		SurfaceSlot = Slot;
		bSurfaceOverlay = bOverlay;
		SurfaceOriginalMaterial = bOverlay ? Mesh->GetOverlayMaterial() : Mesh->GetMaterial(Slot);
	}
	if (!MeshMaterial || MeshMaterial->Parent != Base)
	{
		MeshMaterial = UMaterialInstanceDynamic::Create(Base, this);
		SurfaceTarget.Reset();
	}
	if (bOverlay)
	{
		// Outside UV Region the screen material covers nothing (no colour, no opacity, no depth): the mesh's own material shows.
		if (Mesh->GetOverlayMaterial() != MeshMaterial)
		{
			Mesh->SetOverlayMaterial(MeshMaterial);
		}
	}
	else if (Mesh->GetMaterial(Slot) != MeshMaterial)
	{
		Mesh->SetMaterial(Slot, MeshMaterial);
	}

	// Its own panel draws nothing and catches nothing: the mesh shows the picture, the interactor hits the mesh. Never "seen"
	// itself, so it may draw off screen - the scheduler watches the mesh instead. Flat, for the hit on it the interactor hands on.
	bRenderInMainPass = false;
	bRenderInDepthPass = false;
	TickWhenOffscreen = true;
	GeometryMode = EWidgetGeometryMode::Plane;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkRenderStateDirty();

	ApplyLookTo(MeshMaterial);
	UpdateSurfaceParameters();
}

void UDiegeticScreenComponent::TearDownMeshSurface()
{
	UMeshComponent* Mesh = SurfaceMesh.Get();
	if (Mesh && bSurfaceOverlay && Mesh->GetOverlayMaterial() == MeshMaterial)
	{
		Mesh->SetOverlayMaterial(SurfaceOriginalMaterial);
	}
	else if (Mesh && !bSurfaceOverlay && SurfaceSlot != INDEX_NONE && Mesh->GetMaterial(SurfaceSlot) == MeshMaterial)
	{
		Mesh->SetMaterial(SurfaceSlot, SurfaceOriginalMaterial);
	}
	if (SurfaceMesh.IsValid())
	{
		bRenderInMainPass = true;
		bRenderInDepthPass = true;
		MarkRenderStateDirty();
	}
	SurfaceMesh.Reset();
	SurfaceSlot = INDEX_NONE;
	bSurfaceOverlay = false;
	SurfaceOriginalMaterial = nullptr;
	MeshMaterial = nullptr;
	SurfaceTarget.Reset();
}

void UDiegeticScreenComponent::UpdateSurfaceParameters()
{
	if (!MeshMaterial)
	{
		return;
	}
	using namespace DiegeticScreenPrivate;
	UTextureRenderTarget2D* Target = GetRenderTarget();
	if (Target && SurfaceTarget.Get() != Target)
	{
		MeshMaterial->SetTextureParameterValue(SlateUIParam, Target);
		SurfaceTarget = Target;
	}
	MeshMaterial->SetVectorParameterValue(TintColorAndOpacityParam, TintColorAndOpacity);
	MeshMaterial->SetScalarParameterValue(OpacityFromTextureParam, OpacityFromTexture);
	MeshMaterial->SetVectorParameterValue(UVRegionParam, FLinearColor(
		static_cast<float>(DisplaySettings.UVMin.X), static_cast<float>(DisplaySettings.UVMin.Y),
		static_cast<float>(DisplaySettings.UVMax.X), static_cast<float>(DisplaySettings.UVMax.Y)));
}

bool UDiegeticScreenComponent::TraceMeshSurface(const FVector& Start, const FVector& End, FVector& OutPanelPoint, FVector& OutHitPoint, FVector& OutNormal, double& OutDistance) const
{
	const UMeshComponent* Mesh = SurfaceMesh.Get();
	if (!Mesh || !MeshMaterial)
	{
		return false;
	}

	// Its complex collision, for the face hit and the UV there (Project Settings > Physics > Support UV From Hit Results).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DiegeticMeshSurface), /*bTraceComplex=*/true);
	Params.bReturnFaceIndex = true;
	FHitResult Hit;
	if (!const_cast<UMeshComponent*>(Mesh)->LineTraceComponent(Hit, Start, End, Params))
	{
		return false;
	}
	// Only on the screen's slot - not the monitor's frame. Keep Mesh Material: the named slot's own material, or any face.
	int32 Section = 0;
	if (Hit.FaceIndex != INDEX_NONE && (!bSurfaceOverlay || SurfaceSlot != INDEX_NONE))
	{
		const UMaterialInterface* Wanted = bSurfaceOverlay ? Mesh->GetMaterial(SurfaceSlot) : MeshMaterial.Get();
		if (Mesh->GetMaterialFromCollisionFaceIndex(Hit.FaceIndex, Section) != Wanted)
		{
			return false;
		}
	}
	FVector2D UV;
	if (!UGameplayStatics::FindCollisionUV(Hit, 0, UV))
	{
		return false;
	}
	const FVector2D Span = DisplaySettings.UVMax - DisplaySettings.UVMin;
	if (FMath::Abs(Span.X) < UE_KINDA_SMALL_NUMBER || FMath::Abs(Span.Y) < UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const FVector2D OnPicture = (UV - DisplaySettings.UVMin) / Span;
	if (OnPicture.X < 0.0 || OnPicture.Y < 0.0 || OnPicture.X > 1.0 || OnPicture.Y > 1.0)
	{
		return false;
	}

	// The same place on its own (undrawn, flat) panel: what the engine's interaction maps to widget pixels.
	OutPanelPoint = WidgetToWorld(OnPicture * GetCurrentDrawSize());
	OutHitPoint = Hit.ImpactPoint;
	OutNormal = Hit.ImpactNormal;
	OutDistance = Hit.Distance;
	return true;
}

// =====================================================================================================
// Background Blur widgets
// =====================================================================================================

void UDiegeticScreenComponent::CollectBlurWidgets()
{
	UUserWidget* Root = GetSafeWidget();
	if (!Root)
	{
		return;
	}

	// Every Background Blur on it, into nested User Widgets.
	TArray<UBackgroundBlur*, TInlineAllocator<8>> Found;
	TFunction<void(UUserWidget*, int32)> Gather = [&Found, &Gather](UUserWidget* Within, int32 Depth)
	{
		if (!Within || !Within->WidgetTree || Depth > 16)
		{
			return;
		}
		Within->WidgetTree->ForEachWidget([&Found, &Gather, Depth](UWidget* Each)
		{
			if (UBackgroundBlur* AsBlur = Cast<UBackgroundBlur>(Each))
			{
				Found.Add(AsBlur);
			}
			else if (UUserWidget* Nested = Cast<UUserWidget>(Each))
			{
				Gather(Nested, Depth + 1);
			}
		});
	};
	Gather(Root, 0);

	// Gone: given their own blur back. New: their own drawing off (it is opaque on a screen - UE-41001), the material blurs instead.
	for (auto It = BlurWidgets.CreateIterator(); It; ++It)
	{
		UBackgroundBlur* Known = It.Key().Get();
		if (!Known || !Found.Contains(Known))
		{
			if (Known && DiegeticScreenPrivate::ReadBlurStrength(Known) <= 0.f)
			{
				Known->SetBlurStrength(It.Value());
			}
			It.RemoveCurrent();
		}
	}
	for (UBackgroundBlur* Blur : Found)
	{
		const TWeakObjectPtr<UBackgroundBlur> Key(Blur);
		if (BlurWidgets.Contains(Key))
		{
			continue;
		}
		const float Strength = DiegeticScreenPrivate::ReadBlurStrength(Blur);
		BlurWidgets.Add(Key, Strength);
		if (Glass.bBlurBackgroundBlurWidgets && Strength > 0.f)
		{
			Blur->SetBlurStrength(0.f);
		}
	}

	const bool bHad = bHasBlurWidgets;
	bHasBlurWidgets = BlurWidgets.Num() > 0;
	if (bHad != bHasBlurWidgets)
	{
		ApplyMaterialSetup();
	}
}

void UDiegeticScreenComponent::RestoreBlurWidgets()
{
	for (const TPair<TWeakObjectPtr<UBackgroundBlur>, float>& Pair : BlurWidgets)
	{
		UBackgroundBlur* Blur = Pair.Key.Get();
		if (IsValid(Blur) && DiegeticScreenPrivate::ReadBlurStrength(Blur) <= 0.f && Pair.Value > 0.f)
		{
			Blur->SetBlurStrength(Pair.Value);
		}
	}
	BlurWidgets.Reset();
	bHasBlurWidgets = false;
}

void UDiegeticScreenComponent::CollectBlurWidgetsInEditor()
{
	// As FlushWidgetState does in game: when the widget changes, and every so often after.
	UUserWidget* Current = GetUserWidgetObject();
	const double Now = FPlatformTime::Seconds();
	if (EditorBlurWidget.Get() != Current)
	{
		EditorBlurWidget = Current;
		bCollectBlur = true;
	}
	if (bCollectBlur || Now >= NextBlurCollectTime)
	{
		bCollectBlur = false;
		NextBlurCollectTime = Now + DiegeticScreenPrivate::BlurCollectInterval;
		CollectBlurWidgets();
	}
}

void UDiegeticScreenComponent::ApplyBlurRegions()
{
	using namespace DiegeticScreenPrivate;
	if (AppliedVariant != EDiegeticMaterialVariant::Glass)
	{
		return;
	}

	// Each region as the screen sees it now: its drawn box over the screen's pixels, its corner radius and its blur radius.
	TArray<FLinearColor> Values;
	Values.Add(FLinearColor::Black);
	const FVector2D Pixels = GetCurrentDrawSize();
	if (Glass.bBlurBackgroundBlurWidgets && Pixels.X > 0.0 && Pixels.Y > 0.0)
	{
		for (TPair<TWeakObjectPtr<UBackgroundBlur>, float>& Pair : BlurWidgets)
		{
			UBackgroundBlur* Blur = Pair.Key.Get();
			if (!Blur || !Blur->IsVisible() || Values.Num() >= 1 + 2 * MaxBlurRegions)
			{
				continue;
			}
			// Its strength set again since (from Blueprint): taken, and its own drawing turned off again.
			const float Now = ReadBlurStrength(Blur);
			if (Now > 0.f)
			{
				Pair.Value = Now;
				Blur->SetBlurStrength(0.f);
			}
			const TSharedPtr<SWidget> Slate = Blur->GetCachedWidget();
			if (!Slate.IsValid() || Pair.Value <= 0.f)
			{
				continue;
			}
			const FSlateRect Box = Slate->GetTickSpaceGeometry().GetRenderBoundingRect();
			Values.Add(FLinearColor(static_cast<float>(Box.Left / Pixels.X), static_cast<float>(Box.Top / Pixels.Y),
				static_cast<float>(Box.Right / Pixels.X), static_cast<float>(Box.Bottom / Pixels.Y)));
			Values.Add(FLinearColor(ReadBlurCorner(Blur), Pair.Value * BlurStrengthToRadius, 0.f, 0.f));
		}
	}
	Values[0].R = static_cast<float>((Values.Num() - 1) / 2);

	if (AppliedBlurRegions == Values)
	{
		return;
	}
	AppliedBlurRegions = Values;

	const int32 Count = (Values.Num() - 1) / 2;
	for (UMaterialInstanceDynamic* Instance : { GetMaterialInstance(), MeshMaterial.Get() })
	{
		if (!Instance)
		{
			continue;
		}
		Instance->SetScalarParameterValue(BlurRegionCountParam, static_cast<float>(Count));
		for (int32 Index = 0; Index < MaxBlurRegions; ++Index)
		{
			const bool bUsed = Index < Count;
			Instance->SetVectorParameterValue(BlurRegionParam(Index), bUsed ? Values[1 + Index * 2] : FLinearColor::Transparent);
			Instance->SetVectorParameterValue(BlurShapeParam(Index), bUsed ? Values[2 + Index * 2] : FLinearColor::Transparent);
		}
	}
}

// =====================================================================================================
// Registration
// =====================================================================================================

void UDiegeticScreenComponent::OnRegister()
{
	// Before the Widget Component makes its window and body: they are made at Draw Size.
	ApplySizing();

	Super::OnRegister();

	UpdateTextureInfo();
	ApplyCullDistance();

	UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		if (UDiegeticUISubsystem* Subsystem = World->GetSubsystem<UDiegeticUISubsystem>())
		{
			Subsystem->RegisterScreen(this);
		}
		// It moves itself each frame: it has to be movable.
		if (PlacementSettings.Placement != EDiegeticPlacement::AsPlaced && Mobility != EComponentMobility::Movable)
		{
			SetMobility(EComponentMobility::Movable);
		}
	}
	SyncRegistration();
	ApplyMaterialSetup();
}

void UDiegeticScreenComponent::OnUnregister()
{
	EndUse();
	for (const TWeakObjectPtr<UDiegeticInteractorComponent>& Aimer : Aimers)
	{
		if (UDiegeticInteractorComponent* Interactor = Aimer.Get())
		{
			Interactor->ForgetScreen(this);
		}
	}
	Aimers.Reset();

	if (UWorld* World = GetWorld())
	{
		if (UDiegeticUISubsystem* Subsystem = World->GetSubsystem<UDiegeticUISubsystem>())
		{
			Subsystem->UnregisterScreen(this);
		}
	}
	bScheduled = false;
	Unregister();
	TearDownMeshSurface();
	RestoreBlurWidgets();

	Super::OnUnregister();
}

void UDiegeticScreenComponent::BeginPlay()
{
	bShownByUser = bStartShown;
	bHasPlacement = false;
	Super::BeginPlay();
	SyncRegistration();
	ApplyMaterialSetup();
}

void UDiegeticScreenComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndUse();
	Super::EndPlay(EndPlayReason);
}

void UDiegeticScreenComponent::InitWidget()
{
	Super::InitWidget();
	SyncRegistration();
}

void UDiegeticScreenComponent::SetWidget(UUserWidget* NewWidget)
{
	Super::SetWidget(NewWidget);
	SyncRegistration();
}

void UDiegeticScreenComponent::SyncRegistration()
{
	UUserWidget* Current = GetUserWidgetObject();
	const TSharedPtr<SWindow> Window = GetSlateWindow();
	if (bInLookups && RegisteredWidget.Get() == Current && RegisteredWindow.Pin() == Window)
	{
		return;
	}

	const bool bWidgetChanged = RegisteredWidget.Get() != Current || RegisteredWindow.Pin() != Window;
	Unregister();

	// Game worlds only: an editor world's screen is a plain Widget Component, and nothing there asks which screen a widget is on.
	const UWorld* World = GetWorld();
	if (!IsRegistered() || !World || !World->IsGameWorld())
	{
		return;
	}

	DiegeticScreenPrivate::FLookups& Lookups = DiegeticScreenPrivate::GetLookups();
	if (Current)
	{
		Lookups.ByWidget.Add(TObjectKey<UUserWidget>(Current), this);
	}
	if (Window.IsValid())
	{
		Lookups.ByWindow.Add(Window.Get(), TPair<TWeakPtr<SWindow>, TWeakObjectPtr<UDiegeticScreenComponent>>(Window, this));
	}
	RegisteredWidget = Current;
	RegisteredWindow = Window;
	bInLookups = true;
	++Lookups.NumScreens;

	// A new widget - or the same one just constructed into its window, its state machines started - hears the screen's state.
	// Not from here: this runs inside SetWidget / InitWidget, which a Blueprint recompile during PIE calls while it is replacing
	// the widget. The subsystem's next tick passes it on (FlushWidgetState), once the widget is safe to touch.
	if (bWidgetChanged && Current)
	{
		bPushWidgetState = true;
		bPushContext = true;
		// The old widget's Background Blur widgets are handed back by the next collection - not here, where the widget may be in
		// the middle of being replaced.
		bCollectBlur = true;
	}
	bContentChanged = true;
}

UUserWidget* UDiegeticScreenComponent::GetSafeWidget() const
{
	UUserWidget* Root = GetUserWidgetObject();
	// Never a widget a recompile is replacing (its class has a newer version, or it is on its way out), and never one not yet
	// built into the screen's window: its components and state machines aren't there to talk to.
	if (!IsValid(Root) || IsGarbageCollecting() || Root->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
		|| Root->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists) || !Root->GetCachedWidget().IsValid())
	{
		return nullptr;
	}
	return Root;
}

void UDiegeticScreenComponent::FlushWidgetState()
{
	UUserWidget* Root = GetSafeWidget();
	if (!Root)
	{
		return;
	}
	if (bPushWidgetState && bPresenceKnown)
	{
		bPushWidgetState = false;
		SetWidgetBool(DiegeticScreenPrivate::ScreenAwakeInput, bAwake);
		SetWidgetBool(DiegeticScreenPrivate::ScreenAimedAtInput, IsAimedAt());
		SetWidgetBool(DiegeticScreenPrivate::ScreenInUseInput, IsInUse());
		LastSentDistance = -1.f;
	}
	if (bPushContext)
	{
		bPushContext = false;
		if (Root->Implements<UDiegeticScreenWidget>())
		{
			IDiegeticScreenWidget::Execute_OnScreenContextChanged(Root, this, GetScreenContext());
		}
	}

	// The Background Blur widgets: as the widget comes on, and every so often after (ones added at runtime).
	const double Now = FPlatformTime::Seconds();
	if (bCollectBlur || Now >= NextBlurCollectTime)
	{
		bCollectBlur = false;
		NextBlurCollectTime = Now + DiegeticScreenPrivate::BlurCollectInterval;
		CollectBlurWidgets();
	}
}

void UDiegeticScreenComponent::Unregister()
{
	if (!bInLookups)
	{
		return;
	}
	bInLookups = false;

	// By value, not by the keys kept: a widget or window that has gone away leaves an entry that only its value still names.
	DiegeticScreenPrivate::FLookups& Lookups = DiegeticScreenPrivate::GetLookups();
	for (auto It = Lookups.ByWidget.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid() || It.Value().Get() == this)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = Lookups.ByWindow.CreateIterator(); It; ++It)
	{
		if (!It.Value().Value.IsValid() || It.Value().Value.Get() == this || !It.Value().Key.IsValid())
		{
			It.RemoveCurrent();
		}
	}
	Lookups.NumScreens = FMath::Max(0, Lookups.NumScreens - 1);
	RegisteredWidget.Reset();
	RegisteredWindow.Reset();
}

bool UDiegeticScreenComponent::AnyScreensExist()
{
	return IsInGameThread() && DiegeticScreenPrivate::GetLookups().NumScreens > 0;
}

UDiegeticScreenComponent* UDiegeticScreenComponent::FindScreenForWidget(const UWidget* ForWidget)
{
	if (!ForWidget || !AnyScreensExist())
	{
		return nullptr;
	}
	DiegeticScreenPrivate::FLookups& Lookups = DiegeticScreenPrivate::GetLookups();

	// The outermost User Widget around it: the one a screen shows, when the widget is in that widget's tree (nested User
	// Widgets included - each is outered to the tree it sits in).
	const UUserWidget* Outermost = nullptr;
	for (const UObject* Outer = ForWidget; Outer; Outer = Outer->GetOuter())
	{
		if (const UUserWidget* AsUserWidget = Cast<UUserWidget>(Outer))
		{
			Outermost = AsUserWidget;
		}
	}
	if (Outermost)
	{
		if (const TWeakObjectPtr<UDiegeticScreenComponent>* Found = Lookups.ByWidget.Find(TObjectKey<UUserWidget>(Outermost)))
		{
			if (UDiegeticScreenComponent* Screen = Found->Get())
			{
				return Screen;
			}
		}
	}

	// A widget made at runtime and added to a panel on a screen is outered to whoever made it: where it is in Slate says where
	// it is drawn - up to the window, the screen's own.
	if (Lookups.ByWindow.Num() == 0)
	{
		return nullptr;
	}
	const UWidget* From = Outermost ? static_cast<const UWidget*>(Outermost) : ForWidget;
	TSharedPtr<SWidget> Walk = From->GetCachedWidget();
	static constexpr int32 MaxDepth = 256;
	for (int32 Depth = 0; Walk.IsValid() && Depth < MaxDepth; ++Depth)
	{
		if (const TPair<TWeakPtr<SWindow>, TWeakObjectPtr<UDiegeticScreenComponent>>* Found = Lookups.ByWindow.Find(Walk.Get()))
		{
			if (Found->Key.Pin().Get() == Walk.Get())
			{
				return Found->Value.Get();
			}
		}
		Walk = Walk->GetParentWidget();
	}
	return nullptr;
}

UDiegeticScreenComponent* UDiegeticScreenComponent::FindScreenForObject(const UObject* Object)
{
	const UWidget* AsWidget = Cast<UWidget>(Object);
	return AsWidget ? FindScreenForWidget(AsWidget) : nullptr;
}

// =====================================================================================================
// Drawing
// =====================================================================================================

bool UDiegeticScreenComponent::ShouldDrawWidget() const
{
	// The engine's checks first - shown, seen recently (or Tick When Offscreen), Redraw Time, Manually Redraw - then, in a game
	// world, the scheduler's word for this frame.
	if (!Super::ShouldDrawWidget())
	{
		return false;
	}
	return !bScheduled || bDrawScheduled;
}

void UDiegeticScreenComponent::DrawWidgetToRenderTarget(float DeltaTime)
{
	// An editor world has no subsystem to take over the Background Blur widgets: done here, before the widget is drawn, so the
	// editor shows the screen as play does (a Background Blur left to itself draws opaque on a screen).
	const UWorld* World = GetWorld();
	if (!bScheduled && World && !World->IsGameWorld())
	{
		CollectBlurWidgetsInEditor();
	}

	Super::DrawWidgetToRenderTarget(DeltaTime);

	bDrawScheduled = false;
	const double Now = FPlatformTime::Seconds();
	LastDrawTime = Now;
	RecentDraws.Add(Now);
	while (RecentDraws.Num() > 0 && Now - RecentDraws[0] > 1.0)
	{
		RecentDraws.RemoveAt(0);
	}

	// The material instance is made (and remade) by the Widget Component as it needs one: the look goes onto whichever it has,
	// and onto the mesh's for a Mesh Surface (with the render target, which the Widget Component may have remade).
	UpdateSurfaceParameters();
	ApplyScreenLook(/*bForce=*/false);
	ApplyBlurRegions();
}

float UDiegeticScreenComponent::GetRedrawsPerSecond() const
{
	const double Now = FPlatformTime::Seconds();
	int32 Count = 0;
	for (const double Time : RecentDraws)
	{
		Count += Now - Time <= 1.0 ? 1 : 0;
	}
	return static_cast<float>(Count);
}

void UDiegeticScreenComponent::MarkContentChanged()
{
	bContentChanged = true;
}

void UDiegeticScreenComponent::NoteAnimating()
{
	LastAnimatingFrame = GFrameCounter;
}

bool UDiegeticScreenComponent::IsWidgetAnimating() const
{
	// A Widget Animation moved something on it this frame or the last (they report from the core ticker, before the world ticks).
	if (LastAnimatingFrame != 0 && LastAnimatingFrame + 1 >= GFrameCounter)
	{
		return true;
	}
	const UUserWidget* Root = GetSafeWidget();
	return Root && Root->IsAnyAnimationPlaying();
}

bool UDiegeticScreenComponent::ConsumeChange(double Now)
{
	if (bContentChanged || IsWidgetAnimating() || IsAimedAt() || IsInUse())
	{
		bContentChanged = false;
		LastChangeTime = Now;
	}
	return Now - LastChangeTime <= FMath::Max(static_cast<double>(UpdateSettings.SettleTime), 0.0);
}

bool UDiegeticScreenComponent::WantsRedrawWhileAsleep(double Now) const
{
	return PresenceSettings.SleepBehavior == EDiegeticSleepBehavior::KeepUpdating
		|| Now - SleepStartTime <= DiegeticScreenPrivate::SleepGrace
		|| IsWidgetAnimating();
}

void UDiegeticScreenComponent::SetUpdateMode(EDiegeticUpdateMode NewMode)
{
	UpdateSettings.UpdateMode = NewMode;
	bContentChanged = true;
}

// =====================================================================================================
// Look
// =====================================================================================================

void UDiegeticScreenComponent::SetScreenLook(const FDiegeticScreenLook& NewLook)
{
	ScreenLook = NewLook;
	ApplyScreenLook(/*bForce=*/false);
}

void UDiegeticScreenComponent::SetLookPreset(EDiegeticLookPreset NewPreset)
{
	if (NewPreset == EDiegeticLookPreset::Custom)
	{
		ScreenLook.Preset = NewPreset;
		return;
	}
	SetScreenLook(FDiegeticScreenLook::MakePreset(NewPreset));
}

void UDiegeticScreenComponent::SetGlass(const FDiegeticGlass& NewGlass)
{
	// Background Blur widgets handed back to Slate (or taken from it) when that changes: collected again, the new way.
	if (NewGlass.bBlurBackgroundBlurWidgets != Glass.bBlurBackgroundBlurWidgets)
	{
		RestoreBlurWidgets();
		bCollectBlur = true;
	}
	Glass = NewGlass;
	if (GetWantedVariant() != AppliedVariant)
	{
		ApplyMaterialSetup();
	}
	else
	{
		ApplyScreenLook(/*bForce=*/false);
	}
}

void UDiegeticScreenComponent::SetGlassPreset(EDiegeticGlassPreset NewPreset)
{
	FDiegeticGlass NewGlass = Glass;
	if (NewPreset == EDiegeticGlassPreset::Custom)
	{
		NewGlass.Preset = NewPreset;
	}
	else
	{
		// The pane's shape and the widget's Background Blur widgets aren't the glass's look: kept.
		NewGlass = FDiegeticGlass::MakePreset(NewPreset);
		NewGlass.CornerRadius = Glass.CornerRadius;
		NewGlass.bBlurBackgroundBlurWidgets = Glass.bBlurBackgroundBlurWidgets;
	}
	SetGlass(NewGlass);
}

void UDiegeticScreenComponent::ApplyScreenLook(bool bForce)
{
	UMaterialInstanceDynamic* Instance = GetMaterialInstance();
	const bool bChanged = !FDiegeticScreenLook::StaticStruct()->CompareScriptStruct(&LookApplied, &ScreenLook, PPF_None)
		|| !FDiegeticGlass::StaticStruct()->CompareScriptStruct(&GlassApplied, &Glass, PPF_None);
	if (!bForce && !bChanged && LookAppliedTo.Get() == Instance)
	{
		return;
	}
	ApplyLookTo(Instance);
	if (MeshMaterial)
	{
		ApplyLookTo(MeshMaterial);
	}
	LookAppliedTo = Instance;
	LookApplied = ScreenLook;
	GlassApplied = Glass;
	AppliedBlurRegions.Reset();
}

void UDiegeticScreenComponent::ApplyLookTo(UMaterialInstanceDynamic* Instance) const
{
	if (!Instance)
	{
		return;
	}

	// A material without these parameters ignores them.
	using namespace DiegeticScreenPrivate;
	const FDiegeticScreenLook& Look = ScreenLook;
	Instance->SetScalarParameterValue(BrightnessParam, Look.Brightness);
	Instance->SetScalarParameterValue(IgnoreSceneExposureParam, Look.IgnoreSceneExposure);
	Instance->SetVectorParameterValue(PhosphorColorParam, Look.PhosphorColor);
	Instance->SetScalarParameterValue(MonochromeParam, Look.Monochrome);
	Instance->SetScalarParameterValue(GlowParam, Look.Glow);
	Instance->SetScalarParameterValue(GlowRadiusParam, Look.GlowRadius);
	Instance->SetScalarParameterValue(ScanlineIntensityParam, Look.ScanlineIntensity);
	Instance->SetScalarParameterValue(ScanlineCountParam, Look.ScanlineCount);
	Instance->SetScalarParameterValue(ScanlineSpeedParam, Look.ScanlineSpeed);
	Instance->SetScalarParameterValue(RollingBarParam, Look.RollingBar);
	Instance->SetScalarParameterValue(CurvatureParam, Look.Curvature);
	Instance->SetScalarParameterValue(ChromaticAberrationParam, Look.ChromaticAberration);
	Instance->SetScalarParameterValue(VignetteParam, Look.Vignette);
	Instance->SetScalarParameterValue(FlickerParam, Look.Flicker);
	Instance->SetScalarParameterValue(NoiseParam, Look.Noise);
	Instance->SetScalarParameterValue(PixelGridParam, Look.PixelGrid);
	Instance->SetScalarParameterValue(AdditiveParam, Look.Additive);
	Instance->SetScalarParameterValue(EdgeFadeParam, Look.EdgeFade);
	Instance->SetScalarParameterValue(GlitchParam, Look.Glitch);

	// Glass. Off: a pane that changes nothing (whatever values it keeps), so Background Blur widgets only blur their areas.
	const FDiegeticGlass Pane = Glass.IsEnabled() ? Glass : FDiegeticGlass();
	Instance->SetScalarParameterValue(GlassPaneParam, Glass.IsEnabled() ? 1.f : 0.f);
	Instance->SetScalarParameterValue(GlassBlurParam, Pane.Blur);
	Instance->SetScalarParameterValue(GlassFrostParam, Pane.Frost);
	Instance->SetScalarParameterValue(GlassRefractionParam, Pane.Refraction);
	Instance->SetScalarParameterValue(GlassBevelWidthParam, Pane.BevelWidth);
	Instance->SetScalarParameterValue(GlassRippleParam, Pane.Ripple);
	Instance->SetScalarParameterValue(GlassChromaticAberrationParam, Pane.ChromaticAberration);
	Instance->SetVectorParameterValue(GlassTintParam, Pane.Tint);
	Instance->SetScalarParameterValue(GlassBrightnessParam, Pane.Brightness);
	Instance->SetScalarParameterValue(GlassSheenParam, Pane.Sheen);
	Instance->SetVectorParameterValue(GlassEdgeHighlightParam, Pane.EdgeHighlight);
	Instance->SetScalarParameterValue(GlassCornerRadiusParam, Pane.CornerRadius);

	// What the screen knows of itself: its pixels (glow, pixel grid and blur are in widget pixels) and whether its back shows.
	const FVector2D Pixels = GetCurrentDrawSize();
	Instance->SetVectorParameterValue(ScreenPixelsParam, FLinearColor(static_cast<float>(Pixels.X), static_cast<float>(Pixels.Y), 0.f, 0.f));
	Instance->SetScalarParameterValue(ShowBackFaceParam, GetTwoSided() ? 1.f : 0.f);
}

// =====================================================================================================
// Size and place
// =====================================================================================================

void UDiegeticScreenComponent::ApplySizing()
{
	if (ScreenSizing != EDiegeticScreenSizing::PhysicalSize)
	{
		return;
	}

	const double Density = FMath::Max(static_cast<double>(PixelDensity), 0.01);
	const double Width = FMath::Max(PhysicalSize.X, 1.0);
	const double Height = FMath::Max(PhysicalSize.Y, 1.0);
	const FIntPoint Pixels(FMath::Max(1, FMath::RoundToInt(Width * Density)), FMath::Max(1, FMath::RoundToInt(Height * Density)));

	const FVector2D CurrentPixels = GetDrawSize();
	if (FMath::RoundToInt(CurrentPixels.X) != Pixels.X || FMath::RoundToInt(CurrentPixels.Y) != Pixels.Y)
	{
		SetDrawSize(FVector2D(Pixels.X, Pixels.Y));
	}

	// Exactly the size asked for, whatever the rounding to whole pixels did. X (the screen's depth) is left as it is.
	const FVector Scale = GetRelativeScale3D();
	const FVector Wanted(Scale.X, Width / Pixels.X, Height / Pixels.Y);
	if (!Scale.Equals(Wanted, 1.e-6))
	{
		SetRelativeScale3D(Wanted);
	}
}

void UDiegeticScreenComponent::UpdateTextureInfo()
{
	const FVector2D Pixels = GetDrawSize();
	const double Megabytes = Pixels.X * Pixels.Y * 4.0 / (1024.0 * 1024.0);
	TextureInfo = FString::Printf(TEXT("%d x %d px, %.1f MB"), FMath::RoundToInt(Pixels.X), FMath::RoundToInt(Pixels.Y), Megabytes);
}

void UDiegeticScreenComponent::ApplyCullDistance()
{
	// Only a distance of its own: 0 leaves whatever Max Draw Distance the component was given in Rendering alone.
	if (UpdateSettings.CullDistance > 0.f && LDMaxDrawDistance != UpdateSettings.CullDistance)
	{
		SetCullDistance(UpdateSettings.CullDistance);
	}
}

FVector2D UDiegeticScreenComponent::GetScreenWorldSize() const
{
	const FVector2D Pixels = GetCurrentDrawSize();
	const FVector Scale = GetComponentTransform().GetScale3D();
	return FVector2D(Pixels.X * FMath::Abs(Scale.Y), Pixels.Y * FMath::Abs(Scale.Z));
}

FVector UDiegeticScreenComponent::WidgetToWorld(FVector2D WidgetPosition) const
{
	// The inverse of UWidgetComponent::GetLocalHitLocation: widget X runs along -Y, widget Y along -Z, offset by the pivot.
	const FVector2D Pixels = GetCurrentDrawSize();
	const FVector2D PivotPoint = GetPivot();
	const FVector Local(0.0, Pixels.X * PivotPoint.X - WidgetPosition.X, Pixels.Y * PivotPoint.Y - WidgetPosition.Y);
	return GetComponentTransform().TransformPosition(Local);
}

FVector UDiegeticScreenComponent::GetScreenCenter() const
{
	return WidgetToWorld(GetCurrentDrawSize() * 0.5);
}

FVector UDiegeticScreenComponent::GetDisplayCenter() const
{
	if (const UMeshComponent* Mesh = SurfaceMesh.Get())
	{
		return Mesh->Bounds.Origin;
	}
	return GetScreenCenter();
}

FVector2D UDiegeticScreenComponent::GetDisplayWorldSize() const
{
	const UMeshComponent* Mesh = SurfaceMesh.Get();
	if (!Mesh)
	{
		return GetScreenWorldSize();
	}
	// The mesh's bounds, measured along this component's Y (across) and Z (up).
	const FVector Extent = Mesh->Bounds.BoxExtent;
	auto Across = [&Extent](const FVector& Axis)
	{
		return 2.0 * (FMath::Abs(Axis.X) * Extent.X + FMath::Abs(Axis.Y) * Extent.Y + FMath::Abs(Axis.Z) * Extent.Z);
	};
	return FVector2D(Across(GetRightVector()), Across(GetUpVector()));
}

FVector UDiegeticScreenComponent::GetWorldLocationOfWidget(const UWidget* OnScreen) const
{
	const TSharedPtr<SWidget> Slate = OnScreen ? OnScreen->GetCachedWidget() : nullptr;
	if (!Slate.IsValid() || SurfaceMesh.IsValid())
	{
		return GetDisplayCenter();
	}
	// Its geometry is in its window's space: the screen's widget pixels.
	const FGeometry& Geometry = Slate->GetTickSpaceGeometry();
	const FVector2f LocalSize = Geometry.GetLocalSize();
	const FVector2f Middle = Geometry.LocalToAbsolute(LocalSize * 0.5f);
	return WidgetToWorld(FVector2D(Middle));
}

void UDiegeticScreenComponent::SetMeasurements(float InViewDistance, float InPawnDistance, bool bInAnyViewInFront)
{
	ViewDistance = InViewDistance;
	PawnDistance = InPawnDistance;
	bAnyViewInFront = bInAnyViewInFront;

	if (bDriveStateMachineInputs && (LastSentDistance < 0.f || FMath::Abs(ViewDistance - LastSentDistance) >= DiegeticScreenPrivate::DistanceStep))
	{
		LastSentDistance = ViewDistance;
		SetWidgetNumber(DiegeticScreenPrivate::ScreenDistanceInput, ViewDistance);
	}
}

// =====================================================================================================
// Presence
// =====================================================================================================

void UDiegeticScreenComponent::SetKeepAwake(bool bKeep)
{
	bKeepAwake = bKeep;
}

void UDiegeticScreenComponent::UpdatePresence()
{
	const float Wake = PresenceSettings.WakeDistance;
	bool bWantAwake = true;
	if (!bShownByUser)
	{
		// Hidden by Set Screen Shown, whoever is near.
		bWantAwake = false;
	}
	else if (Wake > 0.f && !bKeepAwake && !IsInUse())
	{
		const float Limit = bPresenceKnown && bAwake ? Wake + FMath::Max(PresenceSettings.SleepMargin, 0.f) : Wake;
		bWantAwake = PawnDistance <= Limit;
	}

	if (!bPresenceKnown)
	{
		bPresenceKnown = true;
		bAwake = bWantAwake;
		if (bAwake)
		{
			BroadcastWake(/*bInitial=*/true);
		}
		else
		{
			BroadcastSleep(/*bInitial=*/true);
		}
		return;
	}

	if (bWantAwake != bAwake)
	{
		bAwake = bWantAwake;
		if (bAwake)
		{
			BroadcastWake(/*bInitial=*/false);
		}
		else
		{
			BroadcastSleep(/*bInitial=*/false);
		}
		return;
	}

	// Asleep, hiding (or hidden by Set Screen Shown): once its widget has played out.
	if (!bAwake && !bHiddenAsleep && (PresenceSettings.SleepBehavior == EDiegeticSleepBehavior::Hide || !bShownByUser)
		&& FPlatformTime::Seconds() - SleepStartTime > DiegeticScreenPrivate::SleepGrace && !IsWidgetAnimating())
	{
		SetHiddenInGame(true);
		bHiddenAsleep = true;
	}
}

void UDiegeticScreenComponent::BroadcastWake(bool bInitial)
{
	if (bHiddenAsleep)
	{
		SetHiddenInGame(false);
		bHiddenAsleep = false;
	}

	UUserWidget* Root = GetSafeWidget();
	if (!bInitial && PresenceSettings.bPlayAnimInOut)
	{
		if (UWidgetAnimationComponent* Player = UWidgetAnimationComponent::FindWidgetAnimationComponent(Root))
		{
			Player->PlayIn();
		}
	}
	SetWidgetBool(DiegeticScreenPrivate::ScreenAwakeInput, true);
	if (Root && Root->Implements<UDiegeticScreenWidget>())
	{
		IDiegeticScreenWidget::Execute_OnScreenWake(Root, this);
	}
	OnScreenWake.Broadcast(this);
	bContentChanged = true;
}

void UDiegeticScreenComponent::BroadcastSleep(bool bInitial)
{
	SleepStartTime = FPlatformTime::Seconds();

	UUserWidget* Root = GetSafeWidget();
	if (PresenceSettings.bPlayAnimInOut)
	{
		if (UWidgetAnimationComponent* Player = UWidgetAnimationComponent::FindWidgetAnimationComponent(Root))
		{
			Player->PlayOut();
		}
	}
	if (bInitial && (PresenceSettings.SleepBehavior == EDiegeticSleepBehavior::Hide || !bShownByUser))
	{
		// Starting asleep: nothing to watch play out.
		SetHiddenInGame(true);
		bHiddenAsleep = true;
	}
	SetWidgetBool(DiegeticScreenPrivate::ScreenAwakeInput, false);
	if (Root && Root->Implements<UDiegeticScreenWidget>())
	{
		IDiegeticScreenWidget::Execute_OnScreenSleep(Root, this);
	}
	OnScreenSleep.Broadcast(this);
	bContentChanged = true;
}

void UDiegeticScreenComponent::SetScreenShown(bool bShown)
{
	if (bShownByUser == bShown)
	{
		return;
	}
	bShownByUser = bShown;
	if (!bShown)
	{
		EndUse();
		// Shown again later, it starts where the view is then rather than easing in from where it was.
		bHasPlacement = false;
	}
	bContentChanged = true;
}

// =====================================================================================================
// Context
// =====================================================================================================

void UDiegeticScreenComponent::SetScreenContext(UObject* NewContext)
{
	if (ScreenContext == NewContext)
	{
		return;
	}
	ScreenContext = NewContext;
	bPushContext = true;
	bContentChanged = true;
}

UObject* UDiegeticScreenComponent::GetScreenContext() const
{
	if (ScreenContext)
	{
		return ScreenContext;
	}
	if (ContextActor)
	{
		return ContextActor;
	}
	return GetOwner();
}

// =====================================================================================================
// Placement
// =====================================================================================================

void UDiegeticScreenComponent::UpdatePlacement(const FVector& ViewLocation, const FRotator& ViewRotation, float DeltaTime)
{
	const FDiegeticPlacementSettings& Place = PlacementSettings;
	switch (Place.Placement)
	{
	case EDiegeticPlacement::FaceCamera:
	case EDiegeticPlacement::FaceCameraYaw:
	{
		// Its face (+X) toward the camera.
		FVector ToCamera = ViewLocation - GetComponentLocation();
		if (Place.Placement == EDiegeticPlacement::FaceCameraYaw)
		{
			ToCamera.Z = 0.0;
		}
		if (!ToCamera.IsNearlyZero())
		{
			SetWorldRotation(ToCamera.Rotation());
		}
		break;
	}
	case EDiegeticPlacement::InFrontOfPlayer:
	{
		const FRotator Facing = Place.bKeepUpright ? FRotator(ViewRotation.Pitch, ViewRotation.Yaw, 0.0) : ViewRotation;
		const FRotationMatrix Axes(Facing);
		const FVector Forward = Axes.GetUnitAxis(EAxis::X);
		const FVector Right = Axes.GetUnitAxis(EAxis::Y);
		const FVector Up = Axes.GetUnitAxis(EAxis::Z);
		const FVector Target = ViewLocation + Forward * Place.FollowDistance + Right * Place.FollowOffset.X + Up * Place.FollowOffset.Y;
		const FQuat TargetRotation = FRotationMatrix::MakeFromXZ(-Forward, Up).ToQuat();

		// Eased toward where it belongs (the first frame there at once), so it trails a quick turn as a projection would.
		const float Alpha = !bHasPlacement || Place.FollowLag <= 0.f ? 1.f : 1.f - FMath::Exp(-FMath::Max(DeltaTime, 0.f) / Place.FollowLag);
		PlacedLocation = bHasPlacement ? FMath::Lerp(PlacedLocation, Target, static_cast<double>(Alpha)) : Target;
		PlacedRotation = bHasPlacement ? FQuat::Slerp(PlacedRotation, TargetRotation, Alpha) : TargetRotation;
		bHasPlacement = true;
		SetWorldLocationAndRotation(PlacedLocation, PlacedRotation);
		break;
	}
	default:
		break;
	}
}

// =====================================================================================================
// State machine inputs
// =====================================================================================================

void UDiegeticScreenComponent::SetWidgetBool(FName Input, bool bValue) const
{
	if (!bDriveStateMachineInputs)
	{
		return;
	}
	// What plays the widget's machines - its own Widget Animation Component, or the one its State Machine Component made for
	// it (found the same way). Never the State Machine Component itself: asking it can make that component, which mustn't
	// happen while a recompile is replacing the widget.
	if (UWidgetAnimationComponent* Player = UWidgetAnimationComponent::FindWidgetAnimationComponent(GetSafeWidget()))
	{
		Player->SetStateMachineBool(Input, bValue);
	}
}

void UDiegeticScreenComponent::SetWidgetNumber(FName Input, float Value) const
{
	if (!bDriveStateMachineInputs)
	{
		return;
	}
	if (UWidgetAnimationComponent* Player = UWidgetAnimationComponent::FindWidgetAnimationComponent(GetSafeWidget()))
	{
		Player->SetStateMachineNumber(Input, Value);
	}
}

// =====================================================================================================
// Aim, pointer and use
// =====================================================================================================

bool UDiegeticScreenComponent::CanBeAimedAt() const
{
	return IsRegistered() && UseSettings.UseMode != EDiegeticUseMode::None && GetWidgetSpace() == EWidgetSpace::World
		&& IsVisible() && bAwake && !IsInUse();
}

void UDiegeticScreenComponent::NotifyAimed(UDiegeticInteractorComponent* Interactor, bool bAimed)
{
	const bool bWas = IsAimedAt();
	const TWeakObjectPtr<UDiegeticInteractorComponent> Key(Interactor);
	if (bAimed)
	{
		Aimers.AddUnique(Key);
	}
	else
	{
		Aimers.Remove(Key);
	}
	Aimers.RemoveAll([](const TWeakObjectPtr<UDiegeticInteractorComponent>& Each) { return !Each.IsValid(); });

	const bool bNow = IsAimedAt();
	if (bNow == bWas)
	{
		return;
	}
	SetWidgetBool(DiegeticScreenPrivate::ScreenAimedAtInput, bNow);
	if (UUserWidget* Root = GetSafeWidget())
	{
		if (Root->Implements<UDiegeticScreenWidget>())
		{
			IDiegeticScreenWidget::Execute_OnScreenAimChanged(Root, this, bNow);
		}
	}
	OnScreenAimChanged.Broadcast(this, bNow);
	bContentChanged = true;
}

void UDiegeticScreenComponent::NotePointerPosition(const FVector2D& WidgetPosition)
{
	PointerPosition = FVector2f(WidgetPosition);
	bHasPointerPosition = true;
}

void UDiegeticScreenComponent::NotePointerButton(const FKey& Button, bool bDown, const FVector2D& WidgetPosition)
{
	const FVector2f At(WidgetPosition);
	if (Button == EKeys::LeftMouseButton)
	{
		bLeftDown = bDown;
		if (bDown)
		{
			++LeftPresses.Count;
			LeftPresses.Position = At;
		}
	}
	else if (Button == EKeys::RightMouseButton)
	{
		bRightDown = bDown;
		WidgetAnimInput::FPointerEventCount& Counted = bDown ? RightPresses : RightReleases;
		++Counted.Count;
		Counted.Position = At;
	}
	bContentChanged = true;
}

void UDiegeticScreenComponent::GetWorldPointer(WidgetAnimInput::FWorldPointer& Out) const
{
	Out.Surface = const_cast<UDiegeticScreenComponent*>(this);
	Out.Position = PointerPosition;
	Out.bHasPosition = bHasPointerPosition;
	Out.bOver = IsAimedAt() || IsInUse();
	Out.bLeftDown = bLeftDown;
	Out.bRightDown = bRightDown;
	Out.LeftPresses = LeftPresses;
	Out.RightPresses = RightPresses;
	Out.RightReleases = RightReleases;
	Out.DoubleClicks = WidgetAnimInput::FPointerEventCount();
}

bool UDiegeticScreenComponent::IsInUse() const
{
	return UserInteractor.IsValid();
}

APlayerController* UDiegeticScreenComponent::GetUser() const
{
	const UDiegeticInteractorComponent* Interactor = UserInteractor.Get();
	return Interactor ? Interactor->GetInteractingPlayer() : nullptr;
}

bool UDiegeticScreenComponent::BeginUse(APlayerController* Player)
{
	if (UseSettings.UseMode == EDiegeticUseMode::None || !Player)
	{
		return false;
	}
	UDiegeticInteractorComponent* Interactor = UDiegeticInteractorComponent::FindInteractor(Player);
	return Interactor && Interactor->BeginUsing(this);
}

void UDiegeticScreenComponent::EndUse()
{
	if (UDiegeticInteractorComponent* Interactor = UserInteractor.Get())
	{
		Interactor->EndUsing();
	}
	UserInteractor.Reset();
}

void UDiegeticScreenComponent::SetUserInteractor(UDiegeticInteractorComponent* Interactor)
{
	UDiegeticInteractorComponent* Previous = UserInteractor.Get();
	if (Previous == Interactor)
	{
		return;
	}
	APlayerController* PreviousUser = Previous ? Previous->GetInteractingPlayer() : nullptr;
	UserInteractor = Interactor;

	UUserWidget* Root = GetSafeWidget();
	const bool bTellWidget = Root && Root->Implements<UDiegeticScreenWidget>();
	if (Previous)
	{
		SetWidgetBool(DiegeticScreenPrivate::ScreenInUseInput, false);
		if (bTellWidget)
		{
			IDiegeticScreenWidget::Execute_OnScreenUseChanged(Root, this, false, PreviousUser);
		}
		OnScreenUseEnd.Broadcast(this, PreviousUser);
		if (bHideWhenUseEnds && !Interactor)
		{
			bHideWhenUseEnds = false;
			SetScreenShown(false);
		}
	}
	if (Interactor)
	{
		APlayerController* NewUser = Interactor->GetInteractingPlayer();
		SetWidgetBool(DiegeticScreenPrivate::ScreenInUseInput, true);
		if (bTellWidget)
		{
			IDiegeticScreenWidget::Execute_OnScreenUseChanged(Root, this, true, NewUser);
		}
		OnScreenUseBegin.Broadcast(this, NewUser);
	}
	bContentChanged = true;
}

// =====================================================================================================
// Debug
// =====================================================================================================

FString UDiegeticScreenComponent::GetDebugText() const
{
	const TCHAR* Mode = UpdateSettings.UpdateMode == EDiegeticUpdateMode::Realtime ? TEXT("Every Frame")
		: UpdateSettings.UpdateMode == EDiegeticUpdateMode::OnDemand ? TEXT("When Changed") : TEXT("By Distance");
	const AActor* OwningActor = GetOwner();
	return FString::Printf(TEXT("%s  %s  %.0f cm  %.0f/s%s%s%s"),
		OwningActor ? *OwningActor->GetActorNameOrLabel() : *GetName(), Mode, ViewDistance, GetRedrawsPerSecond(),
		bAwake ? TEXT("") : TEXT("  asleep"),
		IsAimedAt() ? TEXT("  aimed") : TEXT(""),
		IsInUse() ? TEXT("  in use") : TEXT(""));
}

// =====================================================================================================
// Editor
// =====================================================================================================

#if WITH_EDITOR
void UDiegeticScreenComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName Name = PropertyChangedEvent.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, ScreenSizing)
		|| Name == GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, PhysicalSize)
		|| Name == GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, PixelDensity))
	{
		// Before the Widget Component reacts to the change: it rebuilds its body and texture at the new Draw Size.
		ApplySizing();
	}

	// A Preset sets the other values: before the Widget Component reacts, so it sees them too.
	if (Name == GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, ScreenLook)
		&& PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDiegeticScreenLook, Preset)
		&& ScreenLook.Preset != EDiegeticLookPreset::Custom)
	{
		ScreenLook = FDiegeticScreenLook::MakePreset(ScreenLook.Preset);
	}
	if (Name == GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, Glass))
	{
		const FName Changed = PropertyChangedEvent.GetPropertyName();
		if (Changed == GET_MEMBER_NAME_CHECKED(FDiegeticGlass, Preset) && Glass.Preset != EDiegeticGlassPreset::Custom)
		{
			const float KeptCorner = Glass.CornerRadius;
			const bool bKeptBlurWidgets = Glass.bBlurBackgroundBlurWidgets;
			Glass = FDiegeticGlass::MakePreset(Glass.Preset);
			Glass.CornerRadius = KeptCorner;
			Glass.bBlurBackgroundBlurWidgets = bKeptBlurWidgets;
		}
		if (Changed == GET_MEMBER_NAME_CHECKED(FDiegeticGlass, bBlurBackgroundBlurWidgets))
		{
			// Handed back to Slate (or taken from it): collected again, the new way.
			RestoreBlurWidgets();
			bCollectBlur = true;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	UpdateTextureInfo();
	if (Name == GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, UpdateSettings))
	{
		ApplyCullDistance();
	}
	static const FName BlendModeName(TEXT("BlendMode"));
	static const FName TwoSidedName(TEXT("bIsTwoSided"));
	static const FName MaterialsName(TEXT("OverrideMaterials"));
	if (Name == GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, ScreenLook)
		|| Name == GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, MaterialMode)
		|| Name == GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, DisplaySettings)
		|| Name == GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, Glass)
		|| Name == BlendModeName || Name == TwoSidedName || Name == MaterialsName)
	{
		ApplyMaterialSetup();
	}
}

void UDiegeticScreenComponent::CheckForErrors()
{
	Super::CheckForErrors();

	AActor* OwningActor = GetOwner();
	auto Warn = [OwningActor](const FText& Message)
	{
		FMessageLog(TEXT("MapCheck")).Warning()
			->AddToken(FUObjectToken::Create(OwningActor))
			->AddToken(FTextToken::Create(Message));
	};

	if (GetWidgetSpace() == EWidgetSpace::Screen)
	{
		Warn(LOCTEXT("ScreenSpace", "Diegetic Screen with Space set to Screen: it is drawn on the HUD, not in the world, and its redraws, presence and use do nothing. Set Space to World, or use a Widget Component."));
	}
	if (!GetWidgetClass())
	{
		Warn(LOCTEXT("NoWidgetClass", "Diegetic Screen with no Widget Class: it shows nothing."));
	}
	const FVector2D Pixels = GetDrawSize();
	if (Pixels.X * Pixels.Y > DiegeticScreenPrivate::LargeTexturePixels)
	{
		Warn(FText::Format(LOCTEXT("LargeTexture", "Diegetic Screen draws into a {0} x {1} texture: lower its Draw Size (or Pixel Density)."),
			FText::AsNumber(FMath::RoundToInt(Pixels.X)), FText::AsNumber(FMath::RoundToInt(Pixels.Y))));
	}
	if (bReceiveHardwareInput && UseSettings.UseMode != EDiegeticUseMode::None)
	{
		Warn(LOCTEXT("HardwareInput", "Diegetic Screen with Receive Hardware Input on: the mouse cursor clicks it as well as the Diegetic Interactor. Turn one off."));
	}
	if (DisplaySettings.Display == EDiegeticDisplay::MeshSurface)
	{
		const UMeshComponent* Mesh = GetDisplayMesh();
		if (!Mesh)
		{
			Warn(LOCTEXT("NoSurfaceMesh", "Diegetic Screen with Display Mesh Surface and no Mesh: it shows nothing."));
		}
		else if (!DisplaySettings.MaterialSlot.IsNone() && Mesh->GetMaterialIndex(DisplaySettings.MaterialSlot) == INDEX_NONE)
		{
			Warn(FText::Format(LOCTEXT("NoSurfaceSlot", "Diegetic Screen: its mesh has no material slot {0}."), FText::FromName(DisplaySettings.MaterialSlot)));
		}
		if (UseSettings.UseMode != EDiegeticUseMode::None && !UPhysicsSettings::Get()->bSupportUVFromHitResults)
		{
			Warn(LOCTEXT("NoHitUV", "Diegetic Screen on a Mesh Surface can't be clicked: turn on Project Settings > Physics > Support UV From Hit Results (and give the mesh complex collision)."));
		}
	}
	if (MaterialMode == EDiegeticMaterialMode::EngineDefault && (ScreenLook.Preset != EDiegeticLookPreset::Clean || Glass.IsEnabled()))
	{
		Warn(LOCTEXT("LookWithoutMaterial", "Diegetic Screen with Glass or a Screen Look and Material Engine Default: the engine's material has none of their effects. Set Material to Automatic."));
	}
	if (!bStartShown)
	{
		FMessageLog(TEXT("MapCheck")).Info()
			->AddToken(FUObjectToken::Create(OwningActor))
			->AddToken(FTextToken::Create(LOCTEXT("StartHidden", "Diegetic Screen with Start Shown off: nothing of it shows in game until Set Screen Shown or Open Diegetic Menu is called.")));
	}
}
#endif

#undef LOCTEXT_NAMESPACE
