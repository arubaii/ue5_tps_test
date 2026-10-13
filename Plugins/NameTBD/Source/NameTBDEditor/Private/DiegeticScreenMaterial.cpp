// Copyright AR GameSystems 2026. All rights reserved.

#include "DiegeticScreenMaterial.h"

#include "AssetToolsModule.h"
#include "Containers/Ticker.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Interfaces/IPluginManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionEyeAdaptation.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneColor.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionViewSize.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Modules/ModuleManager.h"
#include "NameTBD/Diegetic/DiegeticUISettings.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogDiegeticScreenMaterial, Log, All);

#define LOCTEXT_NAMESPACE "DiegeticScreenMaterial"

namespace DiegeticScreenMaterialPrivate
{
	/** The version the materials are built at; one built at an older version is rebuilt in place. Raise it with any change below. */
	constexpr float BuildVersion = 2.f;
	const FName VersionParam(TEXT("DiegeticMaterialVersion"));

	const TCHAR* PluginFolder = TEXT("/NameTBD/Diegetic");
	const TCHAR* ProjectFolder = TEXT("/Game/NameTBD/Diegetic");

	const TCHAR* AssetName(EDiegeticMaterialVariant Variant)
	{
		switch (Variant)
		{
		case EDiegeticMaterialVariant::Glass:  return TEXT("M_DiegeticScreen_Glass");
		case EDiegeticMaterialVariant::Opaque: return TEXT("M_DiegeticScreen_Opaque");
		default:                               return TEXT("M_DiegeticScreen");
		}
	}

	const FName LookGroup(TEXT("Screen Look"));
	const FName GlassGroup(TEXT("Glass"));
	const FName ScreenGroup(TEXT("Screen (set by the component)"));

	/**
	 * Everything but the blur, from the widget's render target (Tex, premultiplied: Slate draws over transparent black). Returns
	 * the colour (premultiplied, the scene's exposure undone by IgnoreExposure, the blurred world behind added under it) and the
	 * coverage (AlphaComposite's opacity).
	 */
	const TCHAR* ScreenCode = TEXT(R"HLSL(
float2 P = (MeshUV - Region.xy) / max(float2(Region.z, RegionMaxY) - Region.xy, 1e-4);
float2 Area = P;

// Glitch: rows jump sideways now and then.
float Row = floor(P.y * 48.0);
float Seed = frac(sin(Row * 91.7 + floor(Seconds * 12.0) * 13.3) * 43758.5453);
float Jump = step(1.0 - saturate(Glitch) * 0.25, Seed);
P.x += (Seed - 0.5) * 0.08 * Jump * saturate(Glitch);

// Curvature, as on a CRT: black past the bulged picture's edges.
float2 C = P * 2.0 - 1.0;
C *= 1.0 + Curvature * dot(C, C);
P = C * 0.5 + 0.5;
float Inside = step(0.0, P.x) * step(P.x, 1.0) * step(0.0, P.y) * step(P.y, 1.0) * step(0.0, Area.x) * step(Area.x, 1.0) * step(0.0, Area.y) * step(Area.y, 1.0);
float2 Safe = saturate(P);
float2 Px = max(Pixels.xy, float2(1.0, 1.0));

// The picture, red and blue pulled apart toward the edges.
float2 D = Safe - 0.5;
float4 Picture = Tex.SampleLevel(TexSampler, Safe, 0);
float Red = Tex.SampleLevel(TexSampler, saturate(Safe + D * Chromatic), 0).r;
float Blue = Tex.SampleLevel(TexSampler, saturate(Safe - D * Chromatic), 0).b;
float3 Color = float3(Red, Picture.g, Blue);
float Alpha = Picture.a;

// Glow: what is bright spreads around it.
if (Glow > 0.0)
{
	float2 Step = GlowRadius / Px;
	float3 Halo = float3(0.0, 0.0, 0.0);
	[unroll] for (int i = 0; i < 8; i++)
	{
		float Angle = i * 0.7853982;
		float2 O = float2(cos(Angle), sin(Angle)) * Step;
		Halo += Tex.SampleLevel(TexSampler, saturate(Safe + O), 0).rgb + Tex.SampleLevel(TexSampler, saturate(Safe + O * 0.5), 0).rgb;
	}
	Halo *= Glow / 16.0;
	Color += Halo;
	Alpha = saturate(Alpha + dot(Halo, float3(0.333, 0.333, 0.333)));
}

// One phosphor colour, by brightness.
float Luma = dot(Color, float3(0.299, 0.587, 0.114));
Color = lerp(Color, Phosphor * Luma, saturate(Monochrome));

// Scan lines (crawling at ScanlineSpeed lines a second), a rolling bar, an LCD's sub-pixels.
float Lines = 1.0 - saturate(ScanlineIntensity) * (0.5 + 0.5 * cos((P.y * max(ScanlineCount, 1.0) + Seconds * ScanlineSpeed) * 6.2831853));
float BarAt = saturate(1.0 - abs(frac(P.y - Seconds * 0.15) - 0.5) * 8.0);
float Bar = 1.0 + RollingBar * BarAt * BarAt;
float2 Cell = frac(Safe * Px);
float3 Stripe = float3(step(Cell.x, 0.333), step(0.333, Cell.x) * step(Cell.x, 0.666), step(0.666, Cell.x));
float Gap = step(0.12, Cell.y) * step(Cell.y, 0.88);
float3 Grid = lerp(float3(1.0, 1.0, 1.0), Stripe * 2.2 * lerp(0.6, 1.0, Gap), saturate(PixelGrid));

// Vignette, flicker, grain.
float2 Edge = Safe * (1.0 - Safe);
float Shade = lerp(1.0, sqrt(saturate(Edge.x * Edge.y * 16.0)), saturate(Vignette));
float Frame = floor(Seconds * 24.0);
float Flick = 1.0 - saturate(Flicker) * frac(sin(Frame * 12.9898) * 43758.5453);
float Grain = (frac(sin(dot(floor(Safe * Px) + Frame, float2(12.9898, 78.233))) * 43758.5453) - 0.5) * NoiseAmount;
Color = max(Color + Grain * Alpha, 0.0) * Lines * Bar * Grid * Shade * Flick * max(Brightness, 0.0);

// Hologram: fading as it is seen edge-on, light added rather than covering.
float Facing = abs(dot(normalize(CameraDir), normalize(SurfaceNormal)));
float Fade = lerp(1.0, saturate(Facing * 1.6), saturate(EdgeFade));
Color *= Fade;
Alpha *= Fade * (1.0 - saturate(Additive));

// The widget's tint, the back face (shown only on a Two Sided screen), the picture's area.
float Coverage = Inside * ((BackSign > 0.0 || ShowBack > 0.5) ? 1.0 : 0.0);
Color *= TintRGB * TintA * Coverage;
Alpha = lerp(1.0, Alpha, OpacityFromTexture) * TintA * Coverage;

// As bright as authored, whatever the camera's auto exposure does - as UMG draws it.
Color *= lerp(1.0, 1.0 / max(Exposure, 1e-4), saturate(IgnoreExposure));

// Frosted glass: the world behind, blurred and tinted, under the widget's transparent parts.
float Glass = saturate(BlurMask) * Coverage;
float BehindLuma = max(dot(Blurred.rgb, float3(0.3, 0.59, 0.11)), 0.0);
float3 Behind = lerp(Blurred.rgb, GlassTintRGB * BehindLuma * 1.5, saturate(GlassTintA)) * max(BlurBrightness, 0.0);
Color += Behind * (1.0 - Alpha) * Glass;
Alpha += (1.0 - Alpha) * Glass;

return float4(Color + Version * 0.0, saturate(Alpha));
)HLSL");

	/**
	 * Glass: the blur's radius in the view's UV (xy) and where it blurs (z): the whole screen when Background Blur is set, and the
	 * Background Blur widgets' rounded rectangles, each with its own radius. Radii are in widget pixels, turned into screen pixels
	 * by how many widget pixels one screen pixel spans here.
	 */
	const TCHAR* BlurSetupCode = TEXT(R"HLSL(
float2 P = (MeshUV - Region.xy) / max(float2(Region.z, RegionMaxY) - Region.xy, 1e-4);
float2 Px = max(Pixels.xy, float2(1.0, 1.0));
float2 At = P * Px;
float Span = max(max(length(ddx(At)), length(ddy(At))), 1e-4);
float Radius = BackgroundBlur;
float Mask = BackgroundBlur > 0.0 ? 1.0 : 0.0;
float4 Regions[8] = { float4(R0, RA0), float4(R1, RA1), float4(R2, RA2), float4(R3, RA3), float4(R4, RA4), float4(R5, RA5), float4(R6, RA6), float4(R7, RA7) };
float3 Shapes[8] = { S0, S1, S2, S3, S4, S5, S6, S7 };
for (int i = 0; i < 8; i++)
{
	if (i < (int)Count)
	{
		float2 Low = Regions[i].xy * Px;
		float2 High = Regions[i].zw * Px;
		float2 Middle = (Low + High) * 0.5;
		float2 Half = (High - Low) * 0.5;
		float Corner = min(Shapes[i].x, min(Half.x, Half.y));
		float2 Q = abs(At - Middle) - Half + Corner;
		float Distance = length(max(Q, float2(0.0, 0.0))) + min(max(Q.x, Q.y), 0.0) - Corner;
		float In = saturate(0.5 - Distance);
		Mask = max(Mask, In);
		Radius = In > 0.0 ? max(Radius, Shapes[i].y) : Radius;
	}
}
float2 RadiusUV = (Radius / Span) / max(ViewPixels, float2(1.0, 1.0));
return float3(RadiusUV, Mask);
)HLSL");

	/** The 12 taps' average. */
	const TCHAR* BlurAverageCode = TEXT(R"HLSL(
return (T0.rgb + T1.rgb + T2.rgb + T3.rgb + T4.rgb + T5.rgb + T6.rgb + T7.rgb + T8.rgb + T9.rgb + T10.rgb + T11.rgb) / 12.0;
)HLSL");

	constexpr int32 BlurTaps = 12;

	/** A material graph under construction, its nodes laid out left to right in columns. */
	struct FBuilder
	{
		/** Columns 0 (parameters) to 8 (the material outputs' inputs). */
		static constexpr int32 NumColumns = 9;

		UMaterial* Material = nullptr;
		int32 Rows[NumColumns] = {};

		template <typename T>
		T* Make(int32 Column)
		{
			check(Column >= 0 && Column < NumColumns);
			T* Made = Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(Material, T::StaticClass(), -350 * (NumColumns - 1 - Column), 130 * Rows[Column]++));
			check(Made);
			return Made;
		}

		UMaterialExpressionScalarParameter* Scalar(FName ParamName, float Default, FName Group)
		{
			UMaterialExpressionScalarParameter* Param = Make<UMaterialExpressionScalarParameter>(0);
			Param->ParameterName = ParamName;
			Param->DefaultValue = Default;
			Param->Group = Group;
			return Param;
		}

		UMaterialExpressionVectorParameter* Vector(FName ParamName, const FLinearColor& Default, FName Group)
		{
			UMaterialExpressionVectorParameter* Param = Make<UMaterialExpressionVectorParameter>(0);
			Param->ParameterName = ParamName;
			Param->DefaultValue = Default;
			Param->Group = Group;
			return Param;
		}

		UMaterialExpressionComponentMask* Mask(int32 Column, UMaterialExpression* From, int32 FromOutput, bool bR, bool bG, bool bB, bool bA)
		{
			UMaterialExpressionComponentMask* Masked = Make<UMaterialExpressionComponentMask>(Column);
			Masked->R = bR ? 1 : 0;
			Masked->G = bG ? 1 : 0;
			Masked->B = bB ? 1 : 0;
			Masked->A = bA ? 1 : 0;
			Masked->Input.Connect(FromOutput, From);
			return Masked;
		}

		UMaterialExpressionCustom* Custom(int32 Column, const TCHAR* Description, const TCHAR* Code, ECustomMaterialOutputType Output)
		{
			UMaterialExpressionCustom* Node = Make<UMaterialExpressionCustom>(Column);
			Node->Description = Description;
			Node->Code = Code;
			Node->OutputType = Output;
			Node->Inputs.Reset();
			return Node;
		}

		/** Adds an input to a Custom node, wired from output FromOutput of From. */
		static void In(UMaterialExpressionCustom* Node, const TCHAR* InputName, UMaterialExpression* From, int32 FromOutput = 0)
		{
			FCustomInput& Input = Node->Inputs.AddDefaulted_GetRef();
			Input.InputName = FName(InputName);
			Input.Input.Connect(FromOutput, From);
		}
	};

	/** Output indices the graph reads (the engine's own, stable for years): a vector parameter's RGB and A. */
	constexpr int32 RGBOutput = 0;
	constexpr int32 AlphaOutput = 4;

	/** Builds the whole graph into Material for Variant (its expressions already empty). */
	void Build(UMaterial* Material, EDiegeticMaterialVariant Variant)
	{
		const bool bGlass = Variant == EDiegeticMaterialVariant::Glass;
		const bool bOpaque = Variant == EDiegeticMaterialVariant::Opaque;

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_Unlit);
		Material->BlendMode = bOpaque ? BLEND_Opaque : BLEND_AlphaComposite;
		// Two-sided always: the back face is hidden in the graph unless the screen is Two Sided (ShowBackFace) - one material for both.
		Material->TwoSided = !bOpaque;
		Material->bEnableResponsiveAA = true;
		Material->bUseTranslucencyVertexFog = false;
		// After depth of field (no blur smear on a screen's text); the glass variant before it, where the scene behind is read.
		Material->TranslucencyPass = bGlass ? MTP_BeforeDOF : MTP_AfterDOF;

		FBuilder Graph;
		Graph.Material = Material;

		UTexture* DefaultTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		UMaterialExpressionTextureObjectParameter* SlateUI = Graph.Make<UMaterialExpressionTextureObjectParameter>(0);
		SlateUI->ParameterName = TEXT("SlateUI");
		SlateUI->Texture = DefaultTexture;
		SlateUI->SamplerType = SAMPLERTYPE_Color;
		SlateUI->Group = ScreenGroup;

		UMaterialExpressionTextureCoordinate* MeshUV = Graph.Make<UMaterialExpressionTextureCoordinate>(0);
		UMaterialExpressionTime* Seconds = Graph.Make<UMaterialExpressionTime>(0);
		UMaterialExpressionCameraVectorWS* CameraDir = Graph.Make<UMaterialExpressionCameraVectorWS>(0);
		UMaterialExpressionVertexNormalWS* SurfaceNormal = Graph.Make<UMaterialExpressionVertexNormalWS>(0);
		UMaterialExpressionTwoSidedSign* BackSign = Graph.Make<UMaterialExpressionTwoSidedSign>(0);
		UMaterialExpressionEyeAdaptation* Exposure = Graph.Make<UMaterialExpressionEyeAdaptation>(0);

		// Set by the Widget Component (as on its own materials) and by the Diegetic Screen.
		UMaterialExpressionVectorParameter* Tint = Graph.Vector(TEXT("TintColorAndOpacity"), FLinearColor::White, ScreenGroup);
		UMaterialExpressionScalarParameter* OpacityFromTexture = Graph.Scalar(TEXT("OpacityFromTexture"), 1.f, ScreenGroup);
		UMaterialExpressionVectorParameter* Pixels = Graph.Vector(TEXT("ScreenPixels"), FLinearColor(1920.f, 1080.f, 0.f, 0.f), ScreenGroup);
		UMaterialExpressionVectorParameter* Region = Graph.Vector(TEXT("UVRegion"), FLinearColor(0.f, 0.f, 1.f, 1.f), ScreenGroup);
		UMaterialExpressionScalarParameter* ShowBack = Graph.Scalar(TEXT("ShowBackFace"), 0.f, ScreenGroup);
		UMaterialExpressionScalarParameter* Version = Graph.Scalar(VersionParam, BuildVersion, ScreenGroup);

		// Screen Look.
		UMaterialExpressionScalarParameter* Brightness = Graph.Scalar(TEXT("Brightness"), 1.f, LookGroup);
		UMaterialExpressionScalarParameter* IgnoreExposure = Graph.Scalar(TEXT("IgnoreSceneExposure"), 1.f, LookGroup);
		UMaterialExpressionVectorParameter* Phosphor = Graph.Vector(TEXT("PhosphorColor"), FLinearColor::White, LookGroup);
		UMaterialExpressionScalarParameter* Monochrome = Graph.Scalar(TEXT("Monochrome"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* Glow = Graph.Scalar(TEXT("Glow"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* GlowRadius = Graph.Scalar(TEXT("GlowRadius"), 4.f, LookGroup);
		UMaterialExpressionScalarParameter* ScanlineIntensity = Graph.Scalar(TEXT("ScanlineIntensity"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* ScanlineCount = Graph.Scalar(TEXT("ScanlineCount"), 360.f, LookGroup);
		UMaterialExpressionScalarParameter* ScanlineSpeed = Graph.Scalar(TEXT("ScanlineSpeed"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* RollingBar = Graph.Scalar(TEXT("RollingBar"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* Curvature = Graph.Scalar(TEXT("Curvature"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* Chromatic = Graph.Scalar(TEXT("ChromaticAberration"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* Vignette = Graph.Scalar(TEXT("Vignette"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* Flicker = Graph.Scalar(TEXT("Flicker"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* Noise = Graph.Scalar(TEXT("Noise"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* PixelGrid = Graph.Scalar(TEXT("PixelGrid"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* Additive = Graph.Scalar(TEXT("Additive"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* EdgeFade = Graph.Scalar(TEXT("EdgeFade"), 0.f, LookGroup);
		UMaterialExpressionScalarParameter* Glitch = Graph.Scalar(TEXT("Glitch"), 0.f, LookGroup);

		// The blurred world behind (glass), or nothing.
		UMaterialExpression* Blurred = nullptr;
		UMaterialExpression* BlurMask = nullptr;
		int32 BlurMaskOutput = 0;
		UMaterialExpressionScalarParameter* BackgroundBlur = Graph.Scalar(TEXT("BackgroundBlur"), 0.f, GlassGroup);
		UMaterialExpressionVectorParameter* GlassTint = Graph.Vector(TEXT("GlassTint"), FLinearColor(1.f, 1.f, 1.f, 0.f), GlassGroup);
		UMaterialExpressionScalarParameter* BlurBrightness = Graph.Scalar(TEXT("BlurBrightness"), 1.f, GlassGroup);
		if (bGlass)
		{
			UMaterialExpressionCustom* Setup = Graph.Custom(2, TEXT("Blur Setup"), BlurSetupCode, CMOT_Float3);
			FBuilder::In(Setup, TEXT("MeshUV"), MeshUV);
			FBuilder::In(Setup, TEXT("Region"), Region, RGBOutput);
			FBuilder::In(Setup, TEXT("RegionMaxY"), Region, AlphaOutput);
			FBuilder::In(Setup, TEXT("Pixels"), Graph.Mask(1, Pixels, RGBOutput, true, true, false, false));
			FBuilder::In(Setup, TEXT("BackgroundBlur"), BackgroundBlur);
			FBuilder::In(Setup, TEXT("ViewPixels"), Graph.Make<UMaterialExpressionViewSize>(1));
			FBuilder::In(Setup, TEXT("Count"), Graph.Scalar(TEXT("BlurRegionCount"), 0.f, GlassGroup));
			// Each region's left, top and right as RGB, its bottom as A (a vector parameter's RGB and A outputs).
			for (int32 Index = 0; Index < 8; ++Index)
			{
				UMaterialExpressionVectorParameter* BlurRegion = Graph.Vector(*FString::Printf(TEXT("BlurRegion%d"), Index), FLinearColor::Transparent, GlassGroup);
				FBuilder::In(Setup, *FString::Printf(TEXT("R%d"), Index), BlurRegion, RGBOutput);
				FBuilder::In(Setup, *FString::Printf(TEXT("RA%d"), Index), BlurRegion, AlphaOutput);
			}
			for (int32 Index = 0; Index < 8; ++Index)
			{
				FBuilder::In(Setup, *FString::Printf(TEXT("S%d"), Index), Graph.Vector(*FString::Printf(TEXT("BlurShape%d"), Index), FLinearColor::Transparent, GlassGroup), RGBOutput);
			}

			// The taps: the view's UV here, plus a spiral of offsets scaled by the radius.
			UMaterialExpressionComponentMask* RadiusUV = Graph.Mask(3, Setup, 0, true, true, false, false);
			UMaterialExpressionScreenPosition* ViewUV = Graph.Make<UMaterialExpressionScreenPosition>(3);
			UMaterialExpressionCustom* Average = Graph.Custom(6, TEXT("Blur Average"), BlurAverageCode, CMOT_Float3);
			for (int32 Tap = 0; Tap < BlurTaps; ++Tap)
			{
				const float Distance = FMath::Sqrt((Tap + 0.5f) / BlurTaps);
				const float Angle = Tap * 2.39996323f;
				UMaterialExpressionConstant2Vector* Offset = Graph.Make<UMaterialExpressionConstant2Vector>(3);
				Offset->R = Distance * FMath::Cos(Angle);
				Offset->G = Distance * FMath::Sin(Angle);
				UMaterialExpressionMultiply* Scaled = Graph.Make<UMaterialExpressionMultiply>(4);
				Scaled->A.Connect(0, Offset);
				Scaled->B.Connect(0, RadiusUV);
				UMaterialExpressionAdd* At = Graph.Make<UMaterialExpressionAdd>(4);
				At->A.Connect(0, ViewUV);
				At->B.Connect(0, Scaled);
				UMaterialExpressionSceneColor* Scene = Graph.Make<UMaterialExpressionSceneColor>(5);
				Scene->InputMode = EMaterialSceneAttributeInputMode::Coordinates;
				Scene->Input.Connect(0, At);
				FBuilder::In(Average, *FString::Printf(TEXT("T%d"), Tap), Scene);
			}
			Blurred = Average;
			BlurMask = Graph.Mask(3, Setup, 0, false, false, true, false);
		}
		else
		{
			UMaterialExpressionConstant3Vector* Nothing = Graph.Make<UMaterialExpressionConstant3Vector>(5);
			Nothing->Constant = FLinearColor::Black;
			Blurred = Nothing;
			UMaterialExpressionConstant* NoMask = Graph.Make<UMaterialExpressionConstant>(5);
			NoMask->R = 0.f;
			BlurMask = NoMask;
		}

		UMaterialExpressionCustom* Screen = Graph.Custom(7, TEXT("Diegetic Screen"), ScreenCode, CMOT_Float4);
		FBuilder::In(Screen, TEXT("Tex"), SlateUI);
		FBuilder::In(Screen, TEXT("MeshUV"), MeshUV);
		FBuilder::In(Screen, TEXT("Region"), Region, RGBOutput);
		FBuilder::In(Screen, TEXT("RegionMaxY"), Region, AlphaOutput);
		FBuilder::In(Screen, TEXT("Pixels"), Graph.Mask(6, Pixels, RGBOutput, true, true, false, false));
		FBuilder::In(Screen, TEXT("Seconds"), Seconds);
		FBuilder::In(Screen, TEXT("CameraDir"), CameraDir);
		FBuilder::In(Screen, TEXT("SurfaceNormal"), SurfaceNormal);
		FBuilder::In(Screen, TEXT("BackSign"), BackSign);
		FBuilder::In(Screen, TEXT("ShowBack"), ShowBack);
		FBuilder::In(Screen, TEXT("Exposure"), Exposure);
		FBuilder::In(Screen, TEXT("IgnoreExposure"), IgnoreExposure);
		FBuilder::In(Screen, TEXT("TintRGB"), Tint, RGBOutput);
		FBuilder::In(Screen, TEXT("TintA"), Tint, AlphaOutput);
		FBuilder::In(Screen, TEXT("OpacityFromTexture"), OpacityFromTexture);
		FBuilder::In(Screen, TEXT("Brightness"), Brightness);
		FBuilder::In(Screen, TEXT("Phosphor"), Phosphor, RGBOutput);
		FBuilder::In(Screen, TEXT("Monochrome"), Monochrome);
		FBuilder::In(Screen, TEXT("Glow"), Glow);
		FBuilder::In(Screen, TEXT("GlowRadius"), GlowRadius);
		FBuilder::In(Screen, TEXT("ScanlineIntensity"), ScanlineIntensity);
		FBuilder::In(Screen, TEXT("ScanlineCount"), ScanlineCount);
		FBuilder::In(Screen, TEXT("ScanlineSpeed"), ScanlineSpeed);
		FBuilder::In(Screen, TEXT("RollingBar"), RollingBar);
		FBuilder::In(Screen, TEXT("Curvature"), Curvature);
		FBuilder::In(Screen, TEXT("Chromatic"), Chromatic);
		FBuilder::In(Screen, TEXT("Vignette"), Vignette);
		FBuilder::In(Screen, TEXT("Flicker"), Flicker);
		FBuilder::In(Screen, TEXT("NoiseAmount"), Noise);
		FBuilder::In(Screen, TEXT("PixelGrid"), PixelGrid);
		FBuilder::In(Screen, TEXT("Additive"), Additive);
		FBuilder::In(Screen, TEXT("EdgeFade"), EdgeFade);
		FBuilder::In(Screen, TEXT("Glitch"), Glitch);
		FBuilder::In(Screen, TEXT("Blurred"), Blurred);
		FBuilder::In(Screen, TEXT("BlurMask"), BlurMask, BlurMaskOutput);
		FBuilder::In(Screen, TEXT("GlassTintRGB"), GlassTint, RGBOutput);
		FBuilder::In(Screen, TEXT("GlassTintA"), GlassTint, AlphaOutput);
		FBuilder::In(Screen, TEXT("BlurBrightness"), BlurBrightness);
		FBuilder::In(Screen, TEXT("Version"), Version);

		UMaterialExpressionComponentMask* ColorOut = Graph.Mask(8, Screen, 0, true, true, true, false);
		UMaterialEditingLibrary::ConnectMaterialProperty(ColorOut, FString(), MP_EmissiveColor);
		if (!bOpaque)
		{
			UMaterialExpressionComponentMask* AlphaOut = Graph.Mask(8, Screen, 0, false, false, false, true);
			UMaterialEditingLibrary::ConnectMaterialProperty(AlphaOut, FString(), MP_Opacity);
		}
	}

	/**
	 * Whether a material was built at the current version: its DiegeticMaterialVersion parameter node, read off the graph itself
	 * (not the compiled parameter data, which lags a fresh build - and a material that never reads as current would be rebuilt
	 * over and over).
	 */
	bool IsCurrent(const UMaterial* Material)
	{
		if (!Material)
		{
			return false;
		}
		for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
		{
			const UMaterialExpressionScalarParameter* Param = Cast<UMaterialExpressionScalarParameter>(Expression.Get());
			if (Param && Param->ParameterName == VersionParam)
			{
				return Param->DefaultValue >= BuildVersion;
			}
		}
		return false;
	}

	/** Variants looked for on disk and not there: not looked for again (a screen asks every time it registers) until made. */
	bool bSearchedMissing[3] = {};

	/** Variants made or rebuilt this session: never again in it, whatever they read as. */
	bool bMadeThisSession[3] = {};

	/** The material for a variant where it is (the plugin's folder, else the project's), current or not. */
	UMaterial* Find(EDiegeticMaterialVariant Variant)
	{
		const int32 Index = static_cast<int32>(Variant);
		for (const TCHAR* Folder : { PluginFolder, ProjectFolder })
		{
			const FString Path = FString::Printf(TEXT("%s/%s.%s"), Folder, AssetName(Variant), AssetName(Variant));
			if (UMaterial* Loaded = FindObject<UMaterial>(nullptr, *Path))
			{
				return Loaded;
			}
		}
		if (bSearchedMissing[Index])
		{
			return nullptr;
		}
		for (const TCHAR* Folder : { PluginFolder, ProjectFolder })
		{
			const FString PackageName = FString::Printf(TEXT("%s/%s"), Folder, AssetName(Variant));
			if (FPackageName::DoesPackageExist(PackageName))
			{
				const FString Path = FString::Printf(TEXT("%s.%s"), *PackageName, AssetName(Variant));
				if (UMaterial* Loaded = LoadObject<UMaterial>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					return Loaded;
				}
			}
		}
		bSearchedMissing[Index] = true;
		return nullptr;
	}

	/** Where new materials go: the plugin's content when it is the project's own and can be written, else the project's. */
	const TCHAR* ChooseFolder()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD"));
		if (Plugin.IsValid() && Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project)
		{
			const FString ContentDir = Plugin->GetContentDir();
			if (!IFileManager::Get().IsReadOnly(*ContentDir))
			{
				return PluginFolder;
			}
		}
		return ProjectFolder;
	}

	/**
	 * Whether the editor is somewhere materials can be made, compiled and saved: not while anything is being dragged (an actor
	 * from Place Actors - a dialog or slow task then freezes the editor), a transaction is open, a slow task runs, packages load,
	 * garbage is collected, or a game plays in the editor.
	 */
	bool CanMakeNow()
	{
		if (!GEditor || GEditor->PlayWorld || GEditor->IsTransactionActive() || GIsSlowTask || IsGarbageCollecting() || IsAsyncLoading())
		{
			return false;
		}
		return !FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsDragDropping();
	}

	/** Saves a package straight to disk: no dialogs, no source control prompts. Warns when it can't. */
	void SaveQuietly(UMaterial* Material)
	{
		UPackage* Package = Material->GetOutermost();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Material, *Filename, Args))
		{
			UE_LOG(LogDiegeticScreenMaterial, Warning, TEXT("Couldn't save %s (read-only or checked in?): save it by hand, or Diegetic Screens set to Material Automatic in a packaged game use the engine's material."), *Filename);
		}
	}

	bool bMakeQueued = false;
	bool bMaking = false;
}

UMaterialInterface* DiegeticScreenMaterial::Get(EDiegeticMaterialVariant Variant)
{
	using namespace DiegeticScreenMaterialPrivate;

	UMaterial* Found = Find(Variant);
	if (bMadeThisSession[static_cast<int32>(Variant)] || IsCurrent(Found))
	{
		return Found;
	}

	// Missing or older: made later, once the editor is somewhere it can be (CanMakeNow) - never inside whatever asked (a screen
	// registering as an actor is dragged in, a level loading).
	if (!bMakeQueued && !bMaking)
	{
		bMakeQueued = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			if (!CanMakeNow())
			{
				return true;
			}
			bMakeQueued = false;
			MakeMissing();
			return false;
		}), 0.25f);
	}
	// An older one still draws meanwhile; a missing one, the engine's material.
	return Found;
}

void DiegeticScreenMaterial::MakeMissing()
{
	using namespace DiegeticScreenMaterialPrivate;
	if (bMaking)
	{
		return;
	}
	TGuardValue<bool> Making(bMaking, true);

	bool bMadeAny = false;
	FString MadeFolder;
	for (const EDiegeticMaterialVariant Variant : { EDiegeticMaterialVariant::Panel, EDiegeticMaterialVariant::Glass, EDiegeticMaterialVariant::Opaque })
	{
		const int32 Index = static_cast<int32>(Variant);
		UMaterial* Material = Find(Variant);
		if (bMadeThisSession[Index] || IsCurrent(Material))
		{
			continue;
		}
		// Once a session, whatever happens: a failure is reported, never retried in a loop.
		bMadeThisSession[Index] = true;

		if (Material)
		{
			// Older: rebuilt in place, so everything using it follows.
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		}
		else
		{
			const FString Folder = ChooseFolder();
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			Material = Cast<UMaterial>(AssetTools.CreateAsset(AssetName(Variant), Folder, UMaterial::StaticClass(), Factory));
			if (!Material)
			{
				UE_LOG(LogDiegeticScreenMaterial, Error, TEXT("Couldn't make %s in %s: Diegetic Screens set to Material Automatic use the engine's material."), AssetName(Variant), *Folder);
				continue;
			}
			bSearchedMissing[Index] = false;
			MadeFolder = Folder;
		}

		Build(Material, Variant);
		// Compiled the way an edit in the material editor is (its shaders in the background) - no blocking slow task.
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();
		SaveQuietly(Material);
		bMadeAny = true;
		UE_LOG(LogDiegeticScreenMaterial, Log, TEXT("Made %s (version %.0f)."), *Material->GetPathName(), BuildVersion);
	}

	// Made in the project's folder: a packaged game loads them from there - and has to cook them (the plugin's DefaultGame.ini
	// only names its own folder).
	if (MadeFolder == ProjectFolder)
	{
		UE_LOG(LogDiegeticScreenMaterial, Warning, TEXT("The plugin's folder can't be written: the Diegetic Screen materials were made in %s. Add it to Project Settings > Packaging > Additional Asset Directories to Cook."), ProjectFolder);
		UDiegeticUISettings* Settings = GetMutableDefault<UDiegeticUISettings>();
		auto PathOf = [](EDiegeticMaterialVariant Variant)
		{
			return TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(FString::Printf(TEXT("%s/%s.%s"), ProjectFolder, AssetName(Variant), AssetName(Variant))));
		};
		Settings->ScreenMaterial = PathOf(EDiegeticMaterialVariant::Panel);
		Settings->GlassScreenMaterial = PathOf(EDiegeticMaterialVariant::Glass);
		Settings->OpaqueScreenMaterial = PathOf(EDiegeticMaterialVariant::Opaque);
		Settings->TryUpdateDefaultConfigFile();
	}

	// Every screen picks them up.
	if (bMadeAny)
	{
		for (TObjectIterator<UDiegeticScreenComponent> It; It; ++It)
		{
			if (It->IsRegistered() && !It->IsTemplate())
			{
				It->RefreshMaterial();
			}
		}
	}
}

UMaterial* DiegeticScreenMaterial::CopyToProject(EDiegeticMaterialVariant Variant, const FString& Folder, FText& OutError)
{
	using namespace DiegeticScreenMaterialPrivate;

	MakeMissing();
	UMaterial* Original = Find(Variant);
	if (!Original)
	{
		OutError = LOCTEXT("NoOriginal", "the plugin's screen material couldn't be made.");
		return nullptr;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	FString PackageName;
	FString NewName;
	AssetTools.CreateUniqueAssetName(Folder / AssetName(Variant), FString(), PackageName, NewName);
	UMaterial* Copy = Cast<UMaterial>(AssetTools.DuplicateAsset(NewName, FPackageName::GetLongPackagePath(PackageName), Original));
	if (!Copy)
	{
		OutError = FText::Format(LOCTEXT("CouldNotCopy", "couldn't copy it to {0}."), FText::FromString(Folder));
	}
	return Copy;
}

#undef LOCTEXT_NAMESPACE
