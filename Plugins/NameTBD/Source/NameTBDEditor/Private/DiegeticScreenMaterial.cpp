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
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionEyeAdaptation.h"
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
	constexpr float BuildVersion = 4.f;
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
	 * Pixels at least this opaque write depth (Output Depth and Velocity - the opacity mask clip value is its threshold): the
	 * world's particles and other translucency behind a screen's solid parts stay behind them whatever order translucency is
	 * drawn in, and temporal anti-aliasing moves those parts with the screen rather than with the world behind (no smearing).
	 * Fainter parts are translucency as any other: sorted, and blended over what is behind them.
	 */
	constexpr float SolidOpacity = 0.9f;

	/**
	 * Everything but the glass, from the widget's render target (Tex, premultiplied: Slate draws over transparent black): the
	 * colour (premultiplied, the scene's exposure undone by IgnoreExposure) and the coverage (AlphaComposite's opacity). The
	 * widget's own alpha is only ever opacity - nothing here turns a see-through widget into glass.
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

// How many screen pixels a widget pixel and a scan line cover here: patterns finer than the screen's pixels fade to their average
// instead of shimmering (moire).
float2 WidgetAt = Safe * Px;
float CellOnScreen = 1.0 / max(max(length(ddx(WidgetAt)), length(ddy(WidgetAt))), 1e-4);
float GridFade = saturate((CellOnScreen - 1.5) / 2.5);
float LinesPerPixel = max(ScanlineCount, 1.0) * max(abs(ddx(P.y)), abs(ddy(P.y)));
float LineFade = saturate((0.5 - LinesPerPixel) / 0.25);

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
float Wave = 0.5 + 0.5 * cos((P.y * max(ScanlineCount, 1.0) + Seconds * ScanlineSpeed) * 6.2831853);
float Lines = 1.0 - saturate(ScanlineIntensity) * lerp(0.5, Wave, LineFade);
float BarAt = saturate(1.0 - abs(frac(P.y - Seconds * 0.15) - 0.5) * 8.0);
float Bar = 1.0 + RollingBar * BarAt * BarAt;
float2 Cell = frac(WidgetAt);
float3 Stripe = float3(step(Cell.x, 0.333), step(0.333, Cell.x) * step(Cell.x, 0.666), step(0.666, Cell.x));
float Gap = step(0.12, Cell.y) * step(Cell.y, 0.88);
float3 Grid = lerp(float3(1.0, 1.0, 1.0), Stripe * 2.2 * lerp(0.6, 1.0, Gap), saturate(PixelGrid) * GridFade);

// Vignette, flicker, grain.
float2 Edge = Safe * (1.0 - Safe);
float Shade = lerp(1.0, sqrt(saturate(Edge.x * Edge.y * 16.0)), saturate(Vignette));
float Frame = floor(Seconds * 24.0);
float Flick = 1.0 - saturate(Flicker) * frac(sin(Frame * 12.9898) * 43758.5453);
float Grain = (frac(sin(dot(floor(WidgetAt) + Frame, float2(12.9898, 78.233))) * 43758.5453) - 0.5) * NoiseAmount;
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
float ExposureFix = lerp(1.0, 1.0 / max(Exposure, 1e-4), saturate(IgnoreExposure));
Color *= ExposureFix;
)HLSL");

	/**
	 * Glass, after ScreenCode: the world behind the pane as seen through it (Behind - already bent, blurred, scattered and split by
	 * the taps), tinted, brighter at grazing angles, with light along its rim - under the widget's see-through parts. The world
	 * behind comes from Scene Color, which has no pre-exposure in it: it needs no exposure fix; the rim light is light of the
	 * screen's own, and has the widget's.
	 */
	const TCHAR* GlassComposeCode = TEXT(R"HLSL(
float GlassMask = saturate(GlassArea.x) * Coverage;
float3 Through = max(Behind, 0.0) * lerp(float3(1.0, 1.0, 1.0), max(GlassTintRGB, 0.0), saturate(GlassTintA)) * max(GlassBrightness, 0.0);
float Grazing = 1.0 - Facing;
Through *= 1.0 + saturate(GlassSheen) * Grazing * Grazing * 2.0;
float Bevel = saturate(GlassArea.z);
float3 Rim = max(GlassEdgeRGB, 0.0) * max(GlassEdgeA, 0.0) * (pow(Bevel, 6.0) + 0.15 * Bevel * Bevel) * ExposureFix;
Color += (Through + Rim) * (1.0 - Alpha) * GlassMask;
Alpha += (1.0 - Alpha) * GlassMask;
)HLSL");

	const TCHAR* ReturnCode = TEXT(R"HLSL(
return float4(Color + Version * 0.0, saturate(Alpha));
)HLSL");

	/**
	 * Where a pixel is on the widget, in its pixels (At); how many widget pixels one screen pixel spans there (Span); and screen
	 * pixels from widget pixels (ToScreen). Begins each glass setup node.
	 */
	const TCHAR* GlassPositionCode = TEXT(R"HLSL(
float2 P = (MeshUV - Region.xy) / max(float2(Region.z, RegionMaxY) - Region.xy, 1e-4);
float2 Px = max(Pixels.xy, float2(1.0, 1.0));
float2 At = P * Px;
float2 DX = ddx(At);
float2 DY = ddy(At);
// Screen pixels from widget pixels: the inverse of how the widget's pixels move across the screen here - so the glass is blurred
// and bent in its own plane, seen at an angle as much as face on.
float Det = DX.x * DY.y - DY.x * DX.y;
float InverseDet = abs(Det) > 1e-8 ? 1.0 / Det : 0.0;
float2x2 ToScreen = float2x2(DY.y, -DY.x, -DX.y, DX.x) * InverseDet;
float Span = max(max(length(DX), length(DY)), 1e-4);
)HLSL");

	/**
	 * Glass Area: where the glass is (x, 0 to 1, smoothed over a screen pixel), how far it blurs there (y, widget pixels), the bevel
	 * (z: 1 at the pane's rim, 0 a Bevel Width in) and the signed distance to the pane's edge (w, widget pixels, negative inside).
	 * The pane is the whole screen with its corners rounded (GlassPane 1); each Background Blur widget adds its rounded rectangle
	 * with its own radius.
	 */
	FString GlassAreaCode()
	{
		return FString(GlassPositionCode) + TEXT(R"HLSL(
float2 PaneHalf = Px * 0.5;
float PaneCorner = min(max(CornerRadius, 0.0), min(PaneHalf.x, PaneHalf.y));
float2 PaneQ = abs(At - PaneHalf) - PaneHalf + PaneCorner;
float PaneDistance = length(max(PaneQ, float2(0.0, 0.0))) + min(max(PaneQ.x, PaneQ.y), 0.0) - PaneCorner;
float PaneOn = saturate(Pane);
// Smoothed over the screen pixel just outside its edge (on a square pane that is off the panel: every pixel of it is glass).
float Mask = PaneOn * saturate(1.0 - PaneDistance / Span);
float Radius = PaneOn * max(PaneBlur, 0.0);
float Bevel = PaneOn * saturate(1.0 + PaneDistance / max(BevelWidth, 1.0)) * step(PaneDistance, 0.0);

float4 Regions[8] = { float4(R0, RA0), float4(R1, RA1), float4(R2, RA2), float4(R3, RA3), float4(R4, RA4), float4(R5, RA5), float4(R6, RA6), float4(R7, RA7) };
float3 Shapes[8] = { S0, S1, S2, S3, S4, S5, S6, S7 };
[unroll] for (int i = 0; i < 8; i++)
{
	float Used = step((float)i + 0.5, Count);
	float2 Low = Regions[i].xy * Px;
	float2 High = Regions[i].zw * Px;
	float2 Middle = (Low + High) * 0.5;
	float2 Half = max((High - Low) * 0.5, float2(0.0, 0.0));
	float Corner = min(max(Shapes[i].x, 0.0), min(Half.x, Half.y));
	float2 Q = abs(At - Middle) - Half + Corner;
	float Distance = length(max(Q, float2(0.0, 0.0))) + min(max(Q.x, Q.y), 0.0) - Corner;
	float In = Used * saturate(0.5 - Distance / Span);
	Mask = max(Mask, In);
	Radius = In > 0.0 ? max(Radius, Shapes[i].y) : Radius;
}
return float4(Mask, Radius, Bevel, PaneDistance);
)HLSL");
	}

	/**
	 * Glass Bend: where the pane takes the world behind from, in the view's UV - the bevel's bend (the world from just outside the
	 * pane drawn in along its rim), ripples, frost's scatter (xy) - and how far red and blue are pulled apart there (zw: red at
	 * +zw, blue at -zw). Nothing for Background Blur widgets alone (GlassPane 0): they only blur.
	 */
	FString GlassBendCode()
	{
		return FString(GlassPositionCode) + TEXT(R"HLSL(
float PaneOn = saturate(Pane);
float2 PaneHalf = Px * 0.5;
float PaneCorner = min(max(CornerRadius, 0.0), min(PaneHalf.x, PaneHalf.y));
float2 FromMiddle = At - PaneHalf;
float2 Side = float2(FromMiddle.x < 0.0 ? -1.0 : 1.0, FromMiddle.y < 0.0 ? -1.0 : 1.0);
float2 PaneQ = abs(FromMiddle) - PaneHalf + PaneCorner;
float2 Outward = max(PaneQ.x, PaneQ.y) > 0.0 ? normalize(max(PaneQ, float2(1e-4, 1e-4))) : (PaneQ.x > PaneQ.y ? float2(1.0, 0.0) : float2(0.0, 1.0));
Outward *= Side;
float Bevel = saturate(GlassArea.z);

float2 BendW = Outward * max(Refraction, 0.0) * Bevel * Bevel;
float2 Wave = At / max(Px.y, 1.0) * 6.0;
BendW += max(Ripple, 0.0) * float2(sin(Wave.x * 2.1 + Wave.y * 1.3 + Seconds * 0.6) + 0.5 * sin(Wave.y * 4.7 - Seconds * 0.9),
	cos(Wave.y * 1.7 - Wave.x * 2.3 + Seconds * 0.5) + 0.5 * cos(Wave.x * 5.1 + Seconds * 0.8)) / 1.5;
float2 Cell = floor(At / 1.5);
float2 Scatter = frac(sin(float2(dot(Cell, float2(127.1, 311.7)), dot(Cell, float2(269.5, 183.3)))) * 43758.5453) - 0.5;
BendW += Scatter * 2.0 * max(Frost, 0.0);
float2 SplitW = FromMiddle / max(max(PaneHalf.x, PaneHalf.y), 1.0) * max(Aberration, 0.0) * 0.5;

float2 ViewSpan = max(ViewPixels, float2(1.0, 1.0));
float2 Bend = mul(ToScreen, BendW * PaneOn) / ViewSpan;
float2 Split = mul(ToScreen, SplitW * PaneOn) / ViewSpan;
// Never farther than a tenth of the view (a pane seen edge-on).
Bend *= min(1.0, 0.1 / max(length(Bend), 1e-6));
Split *= min(1.0, 0.03 / max(length(Split), 1e-6));
return float4(Bend, Split);
)HLSL");
	}

	/**
	 * Glass Frame: the blur's spread from a unit disk to the view's UV, as a 2 x 2 matrix (rows xy and zw) - the blur radius in
	 * the pane's plane, turned by a different angle for each pixel and each frame (interleaved gradient noise). The few taps a pixel
	 * takes then average into a smooth blur across neighbouring pixels and frames (temporal anti-aliasing) instead of showing the
	 * world behind as a handful of ghost copies.
	 */
	FString GlassFrameCode()
	{
		return FString(GlassPositionCode) + TEXT(R"HLSL(
float FrameIndex = fmod(floor(Seconds * 60.0), 64.0);
float2 Pixel = PixelPos + 5.588238 * FrameIndex;
float Turn = 6.2831853 * frac(52.9829189 * frac(dot(Pixel, float2(0.06711056, 0.00583715))));
float Cosine = cos(Turn);
float Sine = sin(Turn);
float2x2 M = mul(ToScreen, float2x2(Cosine, -Sine, Sine, Cosine)) * max(GlassArea.y, 0.0);
M[0] /= max(ViewPixels.x, 1.0);
M[1] /= max(ViewPixels.y, 1.0);
// Never wider than 8% of the view (a pane seen edge-on).
float Widest = max(length(float2(M[0][0], M[1][0])), length(float2(M[0][1], M[1][1])));
M *= min(1.0, 0.08 / max(Widest, 1e-6));
return float4(M[0], M[1]);
)HLSL");
	}

	/**
	 * The glass's taps: TapsPerChannel for each of red, green and blue, on a golden-angle (Vogel) disk turned a third of a turn
	 * per channel, weighted toward the middle (a Gaussian). Red's taps are taken from the world bent a little farther than green's,
	 * blue's a little less (chromatic aberration).
	 */
	constexpr int32 TapsPerChannel = 8;
	constexpr int32 GlassTaps = 3 * TapsPerChannel;

	struct FGlassTap
	{
		FVector2f Disk;
		float Weight = 0.f;
		int32 Channel = 0;
	};

	FGlassTap MakeGlassTap(int32 Tap)
	{
		FGlassTap Made;
		Made.Channel = Tap % 3;
		const int32 Index = Tap / 3;
		const float Distance = FMath::Sqrt((Index + 0.5f) / TapsPerChannel);
		const float Angle = Index * 2.39996323f + Made.Channel * 2.0943951f;
		Made.Disk = FVector2f(Distance * FMath::Cos(Angle), Distance * FMath::Sin(Angle));
		Made.Weight = FMath::Exp(-2.f * Distance * Distance);
		return Made;
	}

	/** One tap's place in the view's UV (clamped inside the view: never the render target's unused border). */
	FString GlassTapCode(const FGlassTap& Tap)
	{
		const float Side = Tap.Channel == 0 ? 1.f : Tap.Channel == 2 ? -1.f : 0.f;
		return FString::Printf(TEXT("float2 D = float2(%.6f, %.6f);\nfloat2 O = float2(dot(Frame.xy, D), dot(Frame.zw, D));\nreturn clamp(ViewUV + Bend.xy + Bend.zw * (%.1f) + O, 0.0005, 0.9995);\n"),
			Tap.Disk.X, Tap.Disk.Y, Side);
	}

	/** The taps' weighted average, each channel from its own taps. */
	FString GlassAverageCode()
	{
		FString Code = TEXT("float3 Sum = float3(0.0, 0.0, 0.0);\n");
		float Total[3] = {};
		static const TCHAR* Channels[3] = { TEXT("r"), TEXT("g"), TEXT("b") };
		for (int32 Tap = 0; Tap < GlassTaps; ++Tap)
		{
			const FGlassTap Made = MakeGlassTap(Tap);
			Code += FString::Printf(TEXT("Sum.%s += T%d.%s * %.6f;\n"), Channels[Made.Channel], Tap, Channels[Made.Channel], Made.Weight);
			Total[Made.Channel] += Made.Weight;
		}
		Code += FString::Printf(TEXT("return Sum / float3(%.6f, %.6f, %.6f);\n"), Total[0], Total[1], Total[2]);
		return Code;
	}

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

		UMaterialExpressionCustom* Custom(int32 Column, const TCHAR* Description, const FString& Code, ECustomMaterialOutputType Output)
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

	/** Output indices the graph reads (the engine's own, stable for years): a vector parameter's RGB and A; Screen Position's two. */
	constexpr int32 RGBOutput = 0;
	constexpr int32 AlphaOutput = 4;
	constexpr int32 ViewportUVOutput = 0;
	constexpr int32 PixelPositionOutput = 1;

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
		// Responsive anti-aliasing keeps a plain screen's text from ghosting; the glass leaves temporal anti-aliasing its full history,
		// which turns its per-pixel turned taps into a smooth blur.
		Material->bEnableResponsiveAA = !bGlass;
		Material->bUseTranslucencyVertexFog = false;
		// Every screen in the translucency pass most of the world's translucency (particles) is drawn in, sorted with it - and the
		// one Scene Color is read in. Its depth (below) puts depth of field on it at its own distance.
		Material->TranslucencyPass = MTP_BeforeDOF;
		// On a skeletal mesh as a Mesh Surface too (a static mesh needs no flag): a cooked game has no shaders for an unflagged use.
		Material->bUsedWithSkeletalMesh = true;
		if (!bOpaque)
		{
			Material->bOutputTranslucentVelocity = true;
			Material->OpacityMaskClipValue = SolidOpacity;
		}

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

		UMaterialExpressionCustom* Screen = Graph.Custom(7, TEXT("Diegetic Screen"),
			FString(ScreenCode) + (bGlass ? GlassComposeCode : TEXT("")) + ReturnCode, CMOT_Float4);
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
		FBuilder::In(Screen, TEXT("Version"), Version);

		if (bGlass)
		{
			// Glass.
			UMaterialExpressionScalarParameter* Pane = Graph.Scalar(TEXT("GlassPane"), 0.f, GlassGroup);
			UMaterialExpressionScalarParameter* PaneBlur = Graph.Scalar(TEXT("GlassBlur"), 0.f, GlassGroup);
			UMaterialExpressionScalarParameter* Frost = Graph.Scalar(TEXT("GlassFrost"), 0.f, GlassGroup);
			UMaterialExpressionScalarParameter* Refraction = Graph.Scalar(TEXT("GlassRefraction"), 0.f, GlassGroup);
			UMaterialExpressionScalarParameter* BevelWidth = Graph.Scalar(TEXT("GlassBevelWidth"), 24.f, GlassGroup);
			UMaterialExpressionScalarParameter* Ripple = Graph.Scalar(TEXT("GlassRipple"), 0.f, GlassGroup);
			UMaterialExpressionScalarParameter* Aberration = Graph.Scalar(TEXT("GlassChromaticAberration"), 0.f, GlassGroup);
			UMaterialExpressionVectorParameter* GlassTint = Graph.Vector(TEXT("GlassTint"), FLinearColor(1.f, 1.f, 1.f, 0.f), GlassGroup);
			UMaterialExpressionScalarParameter* GlassBrightness = Graph.Scalar(TEXT("GlassBrightness"), 1.f, GlassGroup);
			UMaterialExpressionScalarParameter* Sheen = Graph.Scalar(TEXT("GlassSheen"), 0.f, GlassGroup);
			UMaterialExpressionVectorParameter* EdgeHighlight = Graph.Vector(TEXT("GlassEdgeHighlight"), FLinearColor(1.f, 1.f, 1.f, 0.f), GlassGroup);
			UMaterialExpressionScalarParameter* CornerRadius = Graph.Scalar(TEXT("GlassCornerRadius"), 0.f, GlassGroup);

			UMaterialExpressionComponentMask* SetupPixels = Graph.Mask(1, Pixels, RGBOutput, true, true, false, false);
			UMaterialExpressionViewSize* ViewPixels = Graph.Make<UMaterialExpressionViewSize>(1);
			UMaterialExpressionScreenPosition* ViewPosition = Graph.Make<UMaterialExpressionScreenPosition>(1);
			auto WidgetPosition = [&](UMaterialExpressionCustom* Node)
			{
				FBuilder::In(Node, TEXT("MeshUV"), MeshUV);
				FBuilder::In(Node, TEXT("Region"), Region, RGBOutput);
				FBuilder::In(Node, TEXT("RegionMaxY"), Region, AlphaOutput);
				FBuilder::In(Node, TEXT("Pixels"), SetupPixels);
			};

			// Where the glass is and how far it blurs: the pane, and the Background Blur widgets' areas - each region's left, top and
			// right as RGB, its bottom as A (a vector parameter's RGB and A outputs); its shape's corner radius and blur radius.
			UMaterialExpressionCustom* Area = Graph.Custom(2, TEXT("Glass Area"), GlassAreaCode(), CMOT_Float4);
			WidgetPosition(Area);
			FBuilder::In(Area, TEXT("Pane"), Pane);
			FBuilder::In(Area, TEXT("PaneBlur"), PaneBlur);
			FBuilder::In(Area, TEXT("CornerRadius"), CornerRadius);
			FBuilder::In(Area, TEXT("BevelWidth"), BevelWidth);
			FBuilder::In(Area, TEXT("Count"), Graph.Scalar(TEXT("BlurRegionCount"), 0.f, GlassGroup));
			for (int32 Index = 0; Index < 8; ++Index)
			{
				UMaterialExpressionVectorParameter* BlurRegion = Graph.Vector(*FString::Printf(TEXT("BlurRegion%d"), Index), FLinearColor::Transparent, GlassGroup);
				FBuilder::In(Area, *FString::Printf(TEXT("R%d"), Index), BlurRegion, RGBOutput);
				FBuilder::In(Area, *FString::Printf(TEXT("RA%d"), Index), BlurRegion, AlphaOutput);
			}
			for (int32 Index = 0; Index < 8; ++Index)
			{
				FBuilder::In(Area, *FString::Printf(TEXT("S%d"), Index), Graph.Vector(*FString::Printf(TEXT("BlurShape%d"), Index), FLinearColor::Transparent, GlassGroup), RGBOutput);
			}

			UMaterialExpressionCustom* Bend = Graph.Custom(3, TEXT("Glass Bend"), GlassBendCode(), CMOT_Float4);
			WidgetPosition(Bend);
			FBuilder::In(Bend, TEXT("GlassArea"), Area);
			FBuilder::In(Bend, TEXT("Pane"), Pane);
			FBuilder::In(Bend, TEXT("CornerRadius"), CornerRadius);
			FBuilder::In(Bend, TEXT("Refraction"), Refraction);
			FBuilder::In(Bend, TEXT("Ripple"), Ripple);
			FBuilder::In(Bend, TEXT("Frost"), Frost);
			FBuilder::In(Bend, TEXT("Aberration"), Aberration);
			FBuilder::In(Bend, TEXT("Seconds"), Seconds);
			FBuilder::In(Bend, TEXT("ViewPixels"), ViewPixels);

			UMaterialExpressionCustom* Frame = Graph.Custom(3, TEXT("Glass Frame"), GlassFrameCode(), CMOT_Float4);
			WidgetPosition(Frame);
			FBuilder::In(Frame, TEXT("GlassArea"), Area);
			FBuilder::In(Frame, TEXT("PixelPos"), ViewPosition, PixelPositionOutput);
			FBuilder::In(Frame, TEXT("Seconds"), Seconds);
			FBuilder::In(Frame, TEXT("ViewPixels"), ViewPixels);

			// The taps: the world behind (Scene Color - the opaque scene, before translucency) where each one lands.
			UMaterialExpressionCustom* Average = Graph.Custom(6, TEXT("Glass Average"), GlassAverageCode(), CMOT_Float3);
			for (int32 Tap = 0; Tap < GlassTaps; ++Tap)
			{
				UMaterialExpressionCustom* Place = Graph.Custom(4, TEXT("Glass Tap"), GlassTapCode(MakeGlassTap(Tap)), CMOT_Float2);
				FBuilder::In(Place, TEXT("ViewUV"), ViewPosition, ViewportUVOutput);
				FBuilder::In(Place, TEXT("Frame"), Frame);
				FBuilder::In(Place, TEXT("Bend"), Bend);
				UMaterialExpressionSceneColor* Scene = Graph.Make<UMaterialExpressionSceneColor>(5);
				Scene->InputMode = EMaterialSceneAttributeInputMode::Coordinates;
				Scene->Input.Connect(0, Place);
				FBuilder::In(Average, *FString::Printf(TEXT("T%d"), Tap), Scene);
			}

			FBuilder::In(Screen, TEXT("Behind"), Average);
			FBuilder::In(Screen, TEXT("GlassArea"), Area);
			FBuilder::In(Screen, TEXT("GlassTintRGB"), GlassTint, RGBOutput);
			FBuilder::In(Screen, TEXT("GlassTintA"), GlassTint, AlphaOutput);
			FBuilder::In(Screen, TEXT("GlassBrightness"), GlassBrightness);
			FBuilder::In(Screen, TEXT("GlassSheen"), Sheen);
			FBuilder::In(Screen, TEXT("GlassEdgeRGB"), EdgeHighlight, RGBOutput);
			FBuilder::In(Screen, TEXT("GlassEdgeA"), EdgeHighlight, AlphaOutput);
		}

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
	 * garbage is collected, Blueprints are being reinstanced, or a game plays in the editor (asked for, starting, running or
	 * simulating - rebuilding a material live screens draw with, during a Widget Blueprint recompile in PIE, stops on an engine
	 * check).
	 */
	bool CanMakeNow()
	{
		if (!GEditor || GEditor->PlayWorld || GEditor->IsPlaySessionInProgress() || GEditor->bIsSimulatingInEditor || GIsPlayInEditorWorld
			|| GIsReinstancing || GEditor->IsTransactionActive() || GIsSlowTask || IsGarbageCollecting() || IsAsyncLoading())
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
	// Never while a game plays in the editor, whoever asks: the next idle tick makes them (Get queues it).
	if (!CanMakeNow())
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
