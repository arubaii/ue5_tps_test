// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuSettingsDefaults.h"

#include "MenuSetting.h"
#include "MenuSettingsSchema.h"
#include "MenuSettingTargets.h"
#include "NameTBD/Menu/Graphics/MenuUpscalers.h"
#include "NameTBD/Menu/Input/MenuInputTypes.h"
#include "MenuProjectSettings.h"

#define LOCTEXT_NAMESPACE "MenuSettingsDefaults"

namespace MenuSettingsDefaultsLocal
{
	/** Builds pages and sections of a schema, one setting at a time. */
	struct FBuilder
	{
		UMenuSettingsSchema* Schema = nullptr;
		int32 PageIndex = INDEX_NONE;
		int32 SectionIndex = INDEX_NONE;
		EMenuSettingScope CurrentScope = EMenuSettingScope::User;

		void Page(FName Id, const FText& Name, int32 Order, EMenuSettingScope InScope)
		{
			FMenuSettingsPage& NewPage = Schema->Pages.AddDefaulted_GetRef();
			NewPage.Id = Id;
			NewPage.DisplayName = Name;
			NewPage.Order = Order;
			PageIndex = Schema->Pages.Num() - 1;
			SectionIndex = INDEX_NONE;
			CurrentScope = InScope;
		}

		FMenuSettingsSection& Section(FName Id, const FText& Title)
		{
			FMenuSettingsSection& NewSection = Schema->Pages[PageIndex].Sections.AddDefaulted_GetRef();
			NewSection.Id = Id;
			NewSection.Title = Title;
			SectionIndex = Schema->Pages[PageIndex].Sections.Num() - 1;
			return NewSection;
		}

		template <typename TSetting>
		TSetting* Add(FName Id, const FText& Name, const FText& Description)
		{
			TSetting* Setting = NewObject<TSetting>(Schema);
			Setting->Id = Id;
			Setting->DisplayName = Name;
			Setting->Description = Description;
			Setting->Scope = CurrentScope;
			Schema->Pages[PageIndex].Sections[SectionIndex].Settings.Add(Setting);
			return Setting;
		}

		UMenuSettingBool* Bool(FName Id, const FText& Name, const FText& Description, bool bDefault)
		{
			UMenuSettingBool* Setting = Add<UMenuSettingBool>(Id, Name, Description);
			Setting->bDefaultValue = bDefault;
			return Setting;
		}

		UMenuSettingNumber* Number(FName Id, const FText& Name, const FText& Description, float Default, float Min, float Max, float Step, EMenuNumberFormat Format)
		{
			UMenuSettingNumber* Setting = Add<UMenuSettingNumber>(Id, Name, Description);
			Setting->DefaultValue = Default;
			Setting->MinValue = Min;
			Setting->MaxValue = Max;
			Setting->Step = Step;
			Setting->Format = Format;
			return Setting;
		}

		UMenuSettingChoice* Choice(FName Id, const FText& Name, const FText& Description, const TArray<FMenuSettingOption>& Options, const FString& Default)
		{
			UMenuSettingChoice* Setting = Add<UMenuSettingChoice>(Id, Name, Description);
			Setting->Options = Options;
			Setting->DefaultValue = Default;
			return Setting;
		}

		UMenuSettingAction* Action(FName Id, const FText& Name, const FText& Description, EMenuSettingActionKind Kind, const FText& Button, const FText& Confirm = FText::GetEmpty())
		{
			UMenuSettingAction* Setting = Add<UMenuSettingAction>(Id, Name, Description);
			Setting->ActionKind = Kind;
			Setting->ButtonText = Button;
			Setting->ConfirmText = Confirm;
			return Setting;
		}

		UMenuSettingInfo* Info(FName Id, const FText& Name, EMenuSystemInfo Field)
		{
			UMenuSettingInfo* Setting = Add<UMenuSettingInfo>(Id, Name, FText::GetEmpty());
			UMenuSettingTarget_SystemInfo* InfoTarget = NewObject<UMenuSettingTarget_SystemInfo>(Setting);
			InfoTarget->Field = Field;
			Setting->Target = InfoTarget;
			Setting->Scope = EMenuSettingScope::Machine;
			return Setting;
		}
	};

	template <typename TTarget>
	TTarget* Target(UMenuSetting* Setting)
	{
		TTarget* NewTarget = NewObject<TTarget>(Setting);
		Setting->Target = NewTarget;
		return NewTarget;
	}

	UMenuSettingTarget_ConsoleVariable* CVar(UMenuSetting* Setting, const TCHAR* Name)
	{
		UMenuSettingTarget_ConsoleVariable* NewTarget = Target<UMenuSettingTarget_ConsoleVariable>(Setting);
		NewTarget->Name = Name;
		return NewTarget;
	}

	/** A Bool setting into a cvar taking OnValue / OffValue (r.MotionBlurQuality 4 / 0). */
	void CVarBool(UMenuSetting* Setting, const TCHAR* Name, const TCHAR* OnValue, const TCHAR* OffValue)
	{
		UMenuSettingTarget_ConsoleVariable* NewTarget = CVar(Setting, Name);
		FMenuCVarValueMap& On = NewTarget->ValueMap.AddDefaulted_GetRef();
		On.SettingValue = TEXT("true");
		On.CVarValue = OnValue;
		FMenuCVarValueMap& Off = NewTarget->ValueMap.AddDefaulted_GetRef();
		Off.SettingValue = TEXT("false");
		Off.CVarValue = OffValue;
	}

	void GameUserSetting(UMenuSetting* Setting, EMenuGameUserSetting Field)
	{
		Target<UMenuSettingTarget_GameUserSettings>(Setting)->Field = Field;
		Setting->Scope = EMenuSettingScope::Machine;
	}

	void BuiltIn(UMenuSetting* Setting, EMenuBuiltInSetting Field)
	{
		Target<UMenuSettingTarget_BuiltIn>(Setting)->Field = Field;
	}

	void Graphics(UMenuSetting* Setting, EMenuGraphicsFeatureField Field)
	{
		Target<UMenuSettingTarget_GraphicsFeature>(Setting)->Field = Field;
		Setting->Scope = EMenuSettingScope::Machine;
	}

	void Volume(UMenuSetting* Setting, FName Channel)
	{
		Target<UMenuSettingTarget_SoundVolume>(Setting)->Channel = Channel;
		Setting->Scope = EMenuSettingScope::Machine;
	}

	void Notification(UMenuSetting* Setting, EMenuNotificationPreference Field)
	{
		Target<UMenuSettingTarget_Notifications>(Setting)->Field = Field;
	}

	TArray<FMenuSettingOption> QualityOptions(bool bWithCinematic)
	{
		TArray<FMenuSettingOption> Options = {
			FMenuSettingOption(TEXT("0"), LOCTEXT("Low", "Low")),
			FMenuSettingOption(TEXT("1"), LOCTEXT("Medium", "Medium")),
			FMenuSettingOption(TEXT("2"), LOCTEXT("High", "High")),
			FMenuSettingOption(TEXT("3"), LOCTEXT("Epic", "Epic")),
		};
		FMenuSettingOption Cinematic(TEXT("4"), LOCTEXT("Cinematic", "Cinematic"), LOCTEXT("CinematicDesc", "Offline-render quality: for screenshots and the fastest machines."));
		Cinematic.bHiddenUnlessCurrent = !bWithCinematic;
		Options.Add(Cinematic);
		return Options;
	}

	/** The condition "Id's value is Value". */
	FMenuSettingCondition Is(FName Id, const FString& Value)
	{
		return FMenuSettingCondition(Id, EMenuSettingCompare::Equals, Value);
	}

	FMenuSettingCondition IsNot(FName Id, const FString& Value)
	{
		return FMenuSettingCondition(Id, EMenuSettingCompare::NotEquals, Value);
	}

	FMenuSettingCondition IsOn(FName Id)
	{
		return FMenuSettingCondition(Id, EMenuSettingCompare::IsTrue);
	}
}

namespace MenuSettingsDefaults
{
	UMenuSettingsSchema* MakeBuiltInSchema(UObject* Outer)
	{
		using namespace MenuSettingsDefaultsLocal;

		UObject* SchemaOuter = Outer ? Outer : GetTransientPackage();
		UMenuSettingsSchema* Schema = NewObject<UMenuSettingsSchema>(SchemaOuter,
			MakeUniqueObjectName(SchemaOuter, UMenuSettingsSchema::StaticClass(), TEXT("NameTBD_BuiltInSettings")), RF_Transient);
		Schema->Priority = -1000;
		FBuilder B;
		B.Schema = Schema;

		// ===========================================================================================================
		//  Gameplay
		// ===========================================================================================================

		B.Page(TEXT("Gameplay"), LOCTEXT("GameplayPage", "Gameplay"), 100, EMenuSettingScope::User);
		B.Section(TEXT("General"), LOCTEXT("GeneralSection", "General"));
		{
			UMenuSettingChoice* Language = B.Choice(TEXT("Gameplay.Language"), LOCTEXT("Language", "Language"),
				LOCTEXT("LanguageDesc", "The language of the game's text."), {}, FString());
			Target<UMenuSettingTarget_Language>(Language);
			Language->Scope = EMenuSettingScope::Machine;
			Language->Display = EMenuChoiceDisplay::Dropdown;
			Language->Keywords = { TEXT("locale"), TEXT("culture"), TEXT("text") };

			UMenuSettingNumber* Fov = B.Number(TEXT("Gameplay.FieldOfView"), LOCTEXT("FieldOfView", "Field of View"),
				LOCTEXT("FieldOfViewDesc", "How wide the camera sees. Wider shows more around you; narrower can feel closer and helps with motion sickness on some screens."),
				90.f, 70.f, 120.f, 1.f, EMenuNumberFormat::Number);
			Fov->Units = LOCTEXT("Degrees", "°");
			Fov->Keywords = { TEXT("fov"), TEXT("camera") };
			BuiltIn(Fov, EMenuBuiltInSetting::FieldOfView);

			UMenuSettingNumber* Shake = B.Number(TEXT("Gameplay.CameraShake"), LOCTEXT("CameraShake", "Camera Shake"),
				LOCTEXT("CameraShakeDesc", "How strongly the camera shakes for impacts and explosions. Lower it if motion makes you uncomfortable."),
				1.f, 0.f, 1.f, 0.1f, EMenuNumberFormat::Percent);
			Shake->MinText = LOCTEXT("OffValue", "Off");
			Shake->Keywords = { TEXT("motion sickness"), TEXT("screen shake") };
		}
		B.Section(TEXT("Notifications"), LOCTEXT("NotificationsSection", "Notifications"));
		{
			UMenuSettingBool* Dnd = B.Bool(TEXT("Gameplay.DoNotDisturb"), LOCTEXT("DoNotDisturb", "Do Not Disturb"),
				LOCTEXT("DoNotDisturbDesc", "Only important notifications show; the rest wait in the notification history."), false);
			Notification(Dnd, EMenuNotificationPreference::DoNotDisturb);
			UMenuSettingBool* Sounds = B.Bool(TEXT("Gameplay.NotificationSounds"), LOCTEXT("NotificationSounds", "Notification Sounds"),
				LOCTEXT("NotificationSoundsDesc", "Notifications play a sound when they appear."), true);
			Notification(Sounds, EMenuNotificationPreference::PlaySounds);
		}

		// ===========================================================================================================
		//  Video
		// ===========================================================================================================

		B.Page(TEXT("Video"), LOCTEXT("VideoPage", "Video"), 200, EMenuSettingScope::Machine);
		B.Section(TEXT("Display"), LOCTEXT("DisplaySection", "Display"));
		{
			UMenuSettingChoice* Mode = B.Choice(TEXT("Video.WindowMode"), LOCTEXT("WindowMode", "Display Mode"),
				LOCTEXT("WindowModeDesc", "Fullscreen gives the game the display (lowest latency); Borderless is a fullscreen window you can alt-tab out of instantly; Windowed is a window."),
				{
					FMenuSettingOption(TEXT("Fullscreen"), LOCTEXT("Fullscreen", "Fullscreen")),
					FMenuSettingOption(TEXT("WindowedFullscreen"), LOCTEXT("Borderless", "Borderless")),
					FMenuSettingOption(TEXT("Windowed"), LOCTEXT("Windowed", "Windowed")),
				}, TEXT("WindowedFullscreen"));
			GameUserSetting(Mode, EMenuGameUserSetting::WindowMode);
			Mode->bRequiresConfirmation = true;
			Mode->Keywords = { TEXT("fullscreen"), TEXT("borderless"), TEXT("window") };

			UMenuSettingChoice* Resolution = B.Choice(TEXT("Video.Resolution"), LOCTEXT("Resolution", "Resolution"),
				LOCTEXT("ResolutionDesc", "The size of the picture in pixels. Your display's own resolution is the sharpest."), {}, FString());
			GameUserSetting(Resolution, EMenuGameUserSetting::Resolution);
			Resolution->bRequiresConfirmation = true;
			Resolution->Display = EMenuChoiceDisplay::Dropdown;
			Resolution->EnabledWhen = { IsNot(TEXT("Video.WindowMode"), TEXT("WindowedFullscreen")) };
			Resolution->DisabledReason = LOCTEXT("ResolutionBorderless", "Borderless uses your desktop's resolution.");
			Resolution->ReapplyWhenChanged = { TEXT("Video.WindowMode") };

			UMenuSettingBool* VSync = B.Bool(TEXT("Video.VSync"), LOCTEXT("VSync", "VSync"),
				LOCTEXT("VSyncDesc", "Waits for the display before showing each frame: no tearing, a little more input latency. With a variable refresh rate display (G-SYNC / FreeSync) leave it off and use a frame rate limit just under the refresh rate."), false);
			GameUserSetting(VSync, EMenuGameUserSetting::VSync);
			VSync->Keywords = { TEXT("tearing"), TEXT("vertical sync") };

			UMenuSettingChoice* Limit = B.Choice(TEXT("Video.FrameRateLimit"), LOCTEXT("FrameRateLimit", "Frame Rate Limit"),
				LOCTEXT("FrameRateLimitDesc", "The most frames per second the game draws. A limit saves power and heat and keeps frame times even."),
				{
					FMenuSettingOption(TEXT("30"), LOCTEXT("Fps30", "30 fps")),
					FMenuSettingOption(TEXT("60"), LOCTEXT("Fps60", "60 fps")),
					FMenuSettingOption(TEXT("90"), LOCTEXT("Fps90", "90 fps")),
					FMenuSettingOption(TEXT("120"), LOCTEXT("Fps120", "120 fps")),
					FMenuSettingOption(TEXT("144"), LOCTEXT("Fps144", "144 fps")),
					FMenuSettingOption(TEXT("165"), LOCTEXT("Fps165", "165 fps")),
					FMenuSettingOption(TEXT("240"), LOCTEXT("Fps240", "240 fps")),
					FMenuSettingOption(TEXT("0"), LOCTEXT("Unlimited", "Unlimited")),
				}, TEXT("0"));
			GameUserSetting(Limit, EMenuGameUserSetting::FrameRateLimit);
			Limit->Keywords = { TEXT("fps"), TEXT("cap"), TEXT("max fps") };

			UMenuSettingNumber* Brightness = B.Number(TEXT("Video.Brightness"), LOCTEXT("Brightness", "Brightness"),
				LOCTEXT("BrightnessDesc", "Adjust until a dark scene's shadows are just visible. The standard is 2.2."),
				2.2f, 1.6f, 3.f, 0.05f, EMenuNumberFormat::Number);
			Brightness->Decimals = 2;
			Brightness->Keywords = { TEXT("gamma") };
			BuiltIn(Brightness, EMenuBuiltInSetting::DisplayGamma);

			UMenuSettingBool* Hdr = B.Bool(TEXT("Video.HDR"), LOCTEXT("HDR", "HDR"),
				LOCTEXT("HDRDesc", "High dynamic range output for HDR displays: brighter highlights, richer colour."), false);
			GameUserSetting(Hdr, EMenuGameUserSetting::HDROutput);
			Hdr->bRequiresConfirmation = true;

			UMenuSettingNumber* Nits = B.Number(TEXT("Video.HDRBrightness"), LOCTEXT("HDRBrightness", "HDR Peak Brightness"),
				LOCTEXT("HDRBrightnessDesc", "Your display's peak brightness. Set it to the value in your display's specifications."),
				1000.f, 400.f, 2000.f, 50.f, EMenuNumberFormat::Number);
			Nits->Units = LOCTEXT("Nits", " nits");
			GameUserSetting(Nits, EMenuGameUserSetting::HDRNits);
			Nits->ShownWhen = { IsOn(TEXT("Video.HDR")) };
		}

		B.Section(TEXT("Upscaling"), LOCTEXT("UpscalingSection", "Upscaling & Frame Generation"));
		{
			UMenuSettingChoice* Upscaler = B.Choice(TEXT("Video.Upscaler"), LOCTEXT("Upscaler", "Upscaling / Anti-Aliasing"),
				LOCTEXT("UpscalerDesc", "How the image is anti-aliased and upscaled. Upscalers render fewer pixels and rebuild a sharp image: far higher frame rates. Auto picks the best for your graphics card."), {}, TEXT("Auto"));
			Graphics(Upscaler, EMenuGraphicsFeatureField::Upscaler);
			Upscaler->Keywords = { TEXT("dlss"), TEXT("fsr"), TEXT("xess"), TEXT("tsr"), TEXT("taa"), TEXT("fxaa"), TEXT("anti-aliasing"), TEXT("aa") };
			Upscaler->Display = EMenuChoiceDisplay::Dropdown;

			UMenuSettingChoice* UpscalerMode = B.Choice(TEXT("Video.UpscalerMode"), LOCTEXT("UpscalerMode", "Upscaling Quality"),
				LOCTEXT("UpscalerModeDesc", "How much the upscaler renders before upscaling. Quality looks closest to native; Performance runs fastest."), {}, FString());
			Graphics(UpscalerMode, EMenuGraphicsFeatureField::UpscalerMode);
			UpscalerMode->ReapplyWhenChanged = { TEXT("Video.Upscaler") };

			UMenuSettingNumber* Scale = B.Number(TEXT("Video.ResolutionScale"), LOCTEXT("ResolutionScale", "Resolution Scale"),
				LOCTEXT("ResolutionScaleDesc", "The share of the output resolution the scene is rendered at. Lower is faster and softer."),
				100.f, 25.f, 100.f, 1.f, EMenuNumberFormat::PercentValue);
			Graphics(Scale, EMenuGraphicsFeatureField::ResolutionScale);
			Scale->WhenUnavailable = EMenuUnavailableBehavior::Disable;
			Scale->ReapplyWhenChanged = { TEXT("Video.Upscaler"), TEXT("Video.UpscalerMode") };
			Scale->Keywords = { TEXT("screen percentage"), TEXT("render scale") };

			UMenuSettingNumber* Sharpness = B.Number(TEXT("Video.Sharpness"), LOCTEXT("Sharpness", "Sharpness"),
				LOCTEXT("SharpnessDesc", "Sharpening the upscaler adds to the image."), 0.f, 0.f, 1.f, 0.05f, EMenuNumberFormat::Percent);
			Graphics(Sharpness, EMenuGraphicsFeatureField::Sharpness);
			Sharpness->ReapplyWhenChanged = { TEXT("Video.Upscaler") };

			UMenuSettingBool* RayReconstruction = B.Bool(TEXT("Video.DLSSRayReconstruction"), LOCTEXT("DLSSRR", "DLSS Ray Reconstruction"),
				LOCTEXT("DLSSRRDesc", "Replaces the ray tracing denoisers with NVIDIA's AI denoiser: clearer reflections and lighting."), false);
			CVar(RayReconstruction, TEXT("r.NGX.DLSS.DenoiserMode"));
			RayReconstruction->ShownWhen = { Is(TEXT("@ActiveUpscaler"), TEXT("DLSS")) };

			UMenuSettingChoice* FrameGeneration = B.Choice(TEXT("Video.FrameGeneration"), LOCTEXT("FrameGeneration", "Frame Generation"),
				LOCTEXT("FrameGenerationDesc", "Generates frames between the ones the game renders: much smoother motion, a little more latency. Works best above 60 fps; turn on latency reduction with it."), {}, TEXT("Off"));
			Graphics(FrameGeneration, EMenuGraphicsFeatureField::FrameGeneration);
			FrameGeneration->ReapplyWhenChanged = { TEXT("Video.Upscaler") };
			FrameGeneration->Keywords = { TEXT("dlss frame generation"), TEXT("fsr frame generation"), TEXT("fg") };

			UMenuSettingChoice* Latency = B.Choice(TEXT("Video.LatencyReduction"), LOCTEXT("LatencyReduction", "Latency Reduction"),
				LOCTEXT("LatencyReductionDesc", "Lowers the delay between your input and the screen (NVIDIA Reflex, AMD Anti-Lag 2, Intel XeLL)."), {}, TEXT("Off"));
			Graphics(Latency, EMenuGraphicsFeatureField::LatencyReduction);
			Latency->Keywords = { TEXT("reflex"), TEXT("anti-lag"), TEXT("input lag") };
		}

		B.Section(TEXT("Quality"), LOCTEXT("QualitySection", "Quality"));
		{
			TArray<FMenuSettingOption> Overall = QualityOptions(true);
			FMenuSettingOption Custom(TEXT("-1"), LOCTEXT("Custom", "Custom"));
			Custom.bHiddenUnlessCurrent = true;
			Overall.Add(Custom);
			UMenuSettingChoice* Preset = B.Choice(TEXT("Video.OverallQuality"), LOCTEXT("OverallQuality", "Quality Preset"),
				LOCTEXT("OverallQualityDesc", "Sets every quality setting below at once."), Overall, TEXT("2"));
			GameUserSetting(Preset, EMenuGameUserSetting::OverallQuality);
			Preset->Keywords = { TEXT("graphics"), TEXT("preset"), TEXT("scalability") };

			struct FQualityRow { const TCHAR* Id; FText Name; FText Description; EMenuGameUserSetting Field; };
			const FQualityRow Rows[] = {
				{ TEXT("Video.ViewDistanceQuality"), LOCTEXT("ViewDistance", "View Distance"), LOCTEXT("ViewDistanceDesc", "How far away detail and objects are drawn."), EMenuGameUserSetting::ViewDistanceQuality },
				{ TEXT("Video.TextureQuality"), LOCTEXT("Textures", "Textures"), LOCTEXT("TexturesDesc", "Texture resolution. Mostly costs graphics memory, little speed."), EMenuGameUserSetting::TextureQuality },
				{ TEXT("Video.ShadowQuality"), LOCTEXT("Shadows", "Shadows"), LOCTEXT("ShadowsDesc", "Shadow resolution and distance. One of the most expensive settings."), EMenuGameUserSetting::ShadowQuality },
				{ TEXT("Video.GlobalIlluminationQuality"), LOCTEXT("GI", "Global Illumination"), LOCTEXT("GIDesc", "Bounced light (Lumen). Expensive; makes scenes look grounded."), EMenuGameUserSetting::GlobalIlluminationQuality },
				{ TEXT("Video.ReflectionQuality"), LOCTEXT("Reflections", "Reflections"), LOCTEXT("ReflectionsDesc", "Reflection quality on shiny and wet surfaces."), EMenuGameUserSetting::ReflectionQuality },
				{ TEXT("Video.EffectsQuality"), LOCTEXT("Effects", "Effects"), LOCTEXT("EffectsDesc", "Particles, smoke, fire and translucency."), EMenuGameUserSetting::EffectsQuality },
				{ TEXT("Video.PostProcessQuality"), LOCTEXT("PostProcess", "Post Processing"), LOCTEXT("PostProcessDesc", "Bloom, depth of field, motion blur and other screen effects."), EMenuGameUserSetting::PostProcessQuality },
				{ TEXT("Video.FoliageQuality"), LOCTEXT("Foliage", "Foliage"), LOCTEXT("FoliageDesc", "Grass and plant density and distance."), EMenuGameUserSetting::FoliageQuality },
				{ TEXT("Video.ShadingQuality"), LOCTEXT("Shading", "Shading"), LOCTEXT("ShadingDesc", "Material and lighting detail."), EMenuGameUserSetting::ShadingQuality },
				{ TEXT("Video.AntiAliasingQuality"), LOCTEXT("AAQuality", "Anti-Aliasing Quality"), LOCTEXT("AAQualityDesc", "How much work anti-aliasing and upscaling do."), EMenuGameUserSetting::AntiAliasingQuality },
			};
			for (const FQualityRow& Row : Rows)
			{
				UMenuSettingChoice* Quality = B.Choice(Row.Id, Row.Name, Row.Description, QualityOptions(false), TEXT("2"));
				GameUserSetting(Quality, Row.Field);
			}

			UMenuSettingNumber* RenderDistance = B.Number(TEXT("Video.RenderDistance"), LOCTEXT("RenderDistance", "Render Distance"),
				LOCTEXT("RenderDistanceDesc", "Multiplies how far objects are drawn before they disappear (on top of View Distance)."),
				1.f, 0.5f, 2.f, 0.1f, EMenuNumberFormat::Multiplier);
			CVar(RenderDistance, TEXT("r.ViewDistanceScale"));
			RenderDistance->Keywords = { TEXT("draw distance"), TEXT("lod") };

			UMenuSettingChoice* Anisotropy = B.Choice(TEXT("Video.TextureFiltering"), LOCTEXT("TextureFiltering", "Texture Filtering"),
				LOCTEXT("TextureFilteringDesc", "Keeps textures sharp at steep angles (roads, floors). Very cheap on modern cards."),
				{
					FMenuSettingOption(TEXT("1"), LOCTEXT("Bilinear", "Off")),
					FMenuSettingOption(TEXT("2"), LOCTEXT("Aniso2", "2x")),
					FMenuSettingOption(TEXT("4"), LOCTEXT("Aniso4", "4x")),
					FMenuSettingOption(TEXT("8"), LOCTEXT("Aniso8", "8x")),
					FMenuSettingOption(TEXT("16"), LOCTEXT("Aniso16", "16x")),
				}, TEXT("8"));
			CVar(Anisotropy, TEXT("r.MaxAnisotropy"));
			Anisotropy->Keywords = { TEXT("anisotropic") };

			UMenuSettingBool* RayTracing = B.Bool(TEXT("Video.HardwareRayTracing"), LOCTEXT("HardwareRayTracing", "Hardware Ray Tracing"),
				LOCTEXT("HardwareRayTracingDesc", "Lumen uses the graphics card's ray tracing hardware: more accurate lighting and reflections, more expensive."), false);
			CVar(RayTracing, TEXT("r.Lumen.HardwareRayTracing"));
			RayTracing->ShownWhen = { IsOn(TEXT("@RayTracingSupported")) };
			RayTracing->Keywords = { TEXT("rtx"), TEXT("lumen"), TEXT("rt") };

			B.Action(TEXT("Video.AutoDetect"), LOCTEXT("AutoDetect", "Auto-Detect Settings"),
				LOCTEXT("AutoDetectDesc", "Measures this machine and picks the quality levels and upscaler that suit it."),
				EMenuSettingActionKind::AutoDetectGraphics, LOCTEXT("AutoDetectButton", "Detect"),
				LOCTEXT("AutoDetectConfirm", "Replace your quality settings with the recommended ones for this machine?"));
		}

		B.Section(TEXT("Effects"), LOCTEXT("EffectsSection", "Camera Effects"));
		{
			UMenuSettingBool* MotionBlur = B.Bool(TEXT("Video.MotionBlur"), LOCTEXT("MotionBlur", "Motion Blur"),
				LOCTEXT("MotionBlurDesc", "Blurs fast movement. Some players find it uncomfortable."), true);
			CVarBool(MotionBlur, TEXT("r.MotionBlurQuality"), TEXT("4"), TEXT("0"));

			UMenuSettingBool* Dof = B.Bool(TEXT("Video.DepthOfField"), LOCTEXT("DepthOfField", "Depth of Field"),
				LOCTEXT("DepthOfFieldDesc", "Blurs what is out of focus, like a camera lens."), true);
			CVarBool(Dof, TEXT("r.DepthOfFieldQuality"), TEXT("2"), TEXT("0"));

			UMenuSettingBool* Bloom = B.Bool(TEXT("Video.Bloom"), LOCTEXT("Bloom", "Bloom"),
				LOCTEXT("BloomDesc", "Glow around bright lights."), true);
			CVarBool(Bloom, TEXT("r.BloomQuality"), TEXT("5"), TEXT("0"));

			UMenuSettingBool* Flares = B.Bool(TEXT("Video.LensFlares"), LOCTEXT("LensFlares", "Lens Flares"),
				LOCTEXT("LensFlaresDesc", "Streaks and rings from bright lights."), true);
			CVarBool(Flares, TEXT("r.LensFlareQuality"), TEXT("2"), TEXT("0"));

			UMenuSettingBool* Fringe = B.Bool(TEXT("Video.ChromaticAberration"), LOCTEXT("ChromaticAberration", "Chromatic Aberration"),
				LOCTEXT("ChromaticAberrationDesc", "Colour fringes at the edges of the screen, like a cheap lens."), true);
			CVarBool(Fringe, TEXT("r.SceneColorFringeQuality"), TEXT("1"), TEXT("0"));

			UMenuSettingBool* Grain = B.Bool(TEXT("Video.FilmGrain"), LOCTEXT("FilmGrain", "Film Grain"),
				LOCTEXT("FilmGrainDesc", "A film-like noise over the picture."), true);
			CVarBool(Grain, TEXT("r.FilmGrain"), TEXT("1"), TEXT("0"));
		}

		B.Section(TEXT("Performance"), LOCTEXT("PerformanceSection", "Performance"));
		{
			UMenuSettingChoice* Overlay = B.Choice(TEXT("Video.PerformanceOverlay"), LOCTEXT("PerformanceOverlay", "Performance Overlay"),
				LOCTEXT("PerformanceOverlayDesc", "Shows the frame rate (and frame times) in a corner of the screen while you play."),
				{
					FMenuSettingOption(TEXT("Off"), LOCTEXT("OverlayOff", "Off")),
					FMenuSettingOption(TEXT("FPS"), LOCTEXT("OverlayFps", "FPS"), LOCTEXT("OverlayFpsDesc", "The frame rate, with a small graph.")),
					FMenuSettingOption(TEXT("FrameTimes"), LOCTEXT("OverlayFrameTimes", "Frame Times"), LOCTEXT("OverlayFrameTimesDesc", "Frame time, game thread, render thread and GPU, graphed.")),
					FMenuSettingOption(TEXT("Detailed"), LOCTEXT("OverlayDetailed", "Detailed"), LOCTEXT("OverlayDetailedDesc", "Frame rate and frame times graphed, with 1% lows and memory.")),
				}, TEXT("Off"));
			Overlay->Keywords = { TEXT("fps counter"), TEXT("stats"), TEXT("frame time") };

			UMenuSettingAction* Benchmark = B.Action(TEXT("Video.Benchmark"), LOCTEXT("Benchmark", "Benchmark"),
				LOCTEXT("BenchmarkDesc", "Measures the frame rate for 15 seconds and reports the average, the 1% lows and frame times. Saved as a CSV in Saved/Benchmarks."),
				EMenuSettingActionKind::RunBenchmark, LOCTEXT("BenchmarkButton", "Run"));
			Benchmark->BenchmarkSeconds = 15.f;
		}

		B.Section(TEXT("System"), LOCTEXT("SystemSection", "System"));
		{
			B.Info(TEXT("Video.Info.GraphicsCard"), LOCTEXT("InfoGPU", "Graphics Card"), EMenuSystemInfo::GraphicsCard);
			B.Info(TEXT("Video.Info.Driver"), LOCTEXT("InfoDriver", "Graphics Driver"), EMenuSystemInfo::GraphicsDriver);
			B.Info(TEXT("Video.Info.GraphicsAPI"), LOCTEXT("InfoAPI", "Graphics API"), EMenuSystemInfo::GraphicsAPI);
			B.Info(TEXT("Video.Info.Processor"), LOCTEXT("InfoCPU", "Processor"), EMenuSystemInfo::Processor);
			B.Info(TEXT("Video.Info.Memory"), LOCTEXT("InfoMemory", "Memory"), EMenuSystemInfo::Memory);
		}

		// ===========================================================================================================
		//  Audio
		// ===========================================================================================================

		B.Page(TEXT("Audio"), LOCTEXT("AudioPage", "Audio"), 300, EMenuSettingScope::Machine);
		B.Section(TEXT("Volume"), LOCTEXT("VolumeSection", "Volume"));
		{
			struct FVolumeRow { const TCHAR* Id; const TCHAR* Channel; FText Name; FText Description; };
			const FVolumeRow Rows[] = {
				{ TEXT("Audio.MasterVolume"), TEXT("Master"), LOCTEXT("MasterVolume", "Master Volume"), LOCTEXT("MasterVolumeDesc", "Every sound in the game.") },
				{ TEXT("Audio.MusicVolume"), TEXT("Music"), LOCTEXT("MusicVolume", "Music"), FText::GetEmpty() },
				{ TEXT("Audio.EffectsVolume"), TEXT("Effects"), LOCTEXT("EffectsVolume", "Sound Effects"), FText::GetEmpty() },
				{ TEXT("Audio.DialogueVolume"), TEXT("Dialogue"), LOCTEXT("DialogueVolume", "Dialogue"), FText::GetEmpty() },
				{ TEXT("Audio.UIVolume"), TEXT("UI"), LOCTEXT("UIVolume", "Interface"), LOCTEXT("UIVolumeDesc", "Menu and HUD sounds.") },
				{ TEXT("Audio.VoiceVolume"), TEXT("Voice"), LOCTEXT("VoiceVolume", "Voice Chat"), FText::GetEmpty() },
			};
			for (const FVolumeRow& Row : Rows)
			{
				UMenuSettingNumber* Setting = B.Number(Row.Id, Row.Name, Row.Description, 1.f, 0.f, 1.f, 0.05f, EMenuNumberFormat::Percent);
				Setting->MinText = LOCTEXT("Muted", "Muted");
				Setting->Keywords = { TEXT("volume"), TEXT("sound") };
				Volume(Setting, Row.Channel);
			}

			UMenuSettingNumber* Background = B.Number(TEXT("Audio.BackgroundVolume"), LOCTEXT("BackgroundVolume", "Volume in Background"),
				LOCTEXT("BackgroundVolumeDesc", "The game's volume while its window isn't focused."), 1.f, 0.f, 1.f, 0.1f, EMenuNumberFormat::Percent);
			Background->MinText = LOCTEXT("Muted", "Muted");
			Background->Platforms = { TEXT("Desktop") };
			BuiltIn(Background, EMenuBuiltInSetting::BackgroundVolume);
		}
		B.Section(TEXT("Output"), LOCTEXT("OutputSection", "Output"));
		{
			UMenuSettingChoice* Quality = B.Choice(TEXT("Audio.Quality"), LOCTEXT("AudioQuality", "Audio Quality"),
				LOCTEXT("AudioQualityDesc", "How many sounds play at once and at what quality."), {}, TEXT("0"));
			GameUserSetting(Quality, EMenuGameUserSetting::AudioQuality);
			Quality->bRequiresRestart = true;
		}

		// ===========================================================================================================
		//  Controls
		// ===========================================================================================================

		B.Page(TEXT("Controls"), LOCTEXT("ControlsPage", "Controls"), 400, EMenuSettingScope::User);
		B.Section(TEXT("Mouse"), LOCTEXT("MouseSection", "Mouse"));
		{
			UMenuSettingNumber* Sensitivity = B.Number(TEXT("Controls.MouseSensitivity"), LOCTEXT("MouseSensitivity", "Mouse Sensitivity"),
				LOCTEXT("MouseSensitivityDesc", "How fast the camera turns with the mouse. (Applied by the Scale By Setting input modifier.)"),
				1.f, 0.1f, 5.f, 0.05f, EMenuNumberFormat::Multiplier);
			Sensitivity->Platforms = { TEXT("Desktop") };
			UMenuSettingBool* Invert = B.Bool(TEXT("Controls.MouseInvertY"), LOCTEXT("MouseInvertY", "Invert Mouse Y"),
				LOCTEXT("MouseInvertYDesc", "Moving the mouse up looks down."), false);
			Invert->Platforms = { TEXT("Desktop") };
		}
		B.Section(TEXT("Gamepad"), LOCTEXT("GamepadSection", "Controller"));
		{
			B.Number(TEXT("Controls.GamepadSensitivityX"), LOCTEXT("LookSensitivityX", "Horizontal Look Sensitivity"),
				LOCTEXT("LookSensitivityXDesc", "How fast the camera turns left and right with the stick."), 1.f, 0.1f, 5.f, 0.05f, EMenuNumberFormat::Multiplier);
			B.Number(TEXT("Controls.GamepadSensitivityY"), LOCTEXT("LookSensitivityY", "Vertical Look Sensitivity"),
				LOCTEXT("LookSensitivityYDesc", "How fast the camera turns up and down with the stick."), 1.f, 0.1f, 5.f, 0.05f, EMenuNumberFormat::Multiplier);
			B.Bool(TEXT("Controls.GamepadInvertY"), LOCTEXT("GamepadInvertY", "Invert Look Y"),
				LOCTEXT("GamepadInvertYDesc", "Pushing the stick up looks down."), false);
			UMenuSettingNumber* DeadZone = B.Number(TEXT("Controls.GamepadDeadZone"), LOCTEXT("DeadZone", "Stick Dead Zone"),
				LOCTEXT("DeadZoneDesc", "How far a stick moves before it counts. Raise it if the camera drifts on its own. (Applied by the Dead Zone By Setting input modifier.)"),
				0.2f, 0.f, 0.5f, 0.01f, EMenuNumberFormat::Percent);
			DeadZone->Keywords = { TEXT("drift") };

			UMenuSettingBool* Vibration = B.Bool(TEXT("Controls.Vibration"), LOCTEXT("Vibration", "Vibration"),
				LOCTEXT("VibrationDesc", "Controller rumble."), true);
			BuiltIn(Vibration, EMenuBuiltInSetting::Vibration);
			Vibration->Keywords = { TEXT("rumble"), TEXT("force feedback"), TEXT("haptics") };
			UMenuSettingNumber* Strength = B.Number(TEXT("Controls.VibrationStrength"), LOCTEXT("VibrationStrength", "Vibration Strength"),
				FText::GetEmpty(), 1.f, 0.f, 1.f, 0.1f, EMenuNumberFormat::Percent);
			BuiltIn(Strength, EMenuBuiltInSetting::VibrationStrength);
			Strength->ShownWhen = { IsOn(TEXT("Controls.Vibration")) };

			B.Bool(TEXT("Controls.AimAssist"), LOCTEXT("AimAssist", "Aim Assist"),
				LOCTEXT("AimAssistDesc", "Helps the crosshair stay on targets with a controller."), true);

			UMenuSettingChoice* Prompts = B.Choice(TEXT("Controls.ButtonPrompts"), LOCTEXT("ButtonPrompts", "Button Prompts"),
				LOCTEXT("ButtonPromptsDesc", "Which controller's buttons the game shows. Auto follows the controller you use."),
				{
					FMenuSettingOption(TEXT("Auto"), LOCTEXT("PromptsAuto", "Auto")),
					FMenuSettingOption(TEXT("Xbox"), LOCTEXT("PromptsXbox", "Xbox")),
					FMenuSettingOption(TEXT("PlayStation"), LOCTEXT("PromptsPlayStation", "PlayStation")),
					FMenuSettingOption(TEXT("Switch"), LOCTEXT("PromptsSwitch", "Nintendo Switch")),
					FMenuSettingOption(TEXT("Generic"), LOCTEXT("PromptsGeneric", "Generic")),
				}, TEXT("Auto"));
			// The project's Custom families (Key Glyph Sets with Family Custom) are choices too, by their names.
			for (const TSoftObjectPtr<UMenuKeyGlyphSet>& SetEntry : UMenuProjectSettings::Get().KeyGlyphSets)
			{
				const UMenuKeyGlyphSet* GlyphSet = SetEntry.LoadSynchronous();
				if (GlyphSet && !GlyphSet->bForKeyboard && GlyphSet->Family == EMenuGamepadFamily::Custom && !GlyphSet->CustomFamilyName.IsNone())
				{
					Prompts->Options.Add(FMenuSettingOption(GlyphSet->CustomFamilyName.ToString(),
						GlyphSet->CustomFamilyDisplayName.IsEmpty() ? FText::FromName(GlyphSet->CustomFamilyName) : GlyphSet->CustomFamilyDisplayName));
				}
			}
			BuiltIn(Prompts, EMenuBuiltInSetting::ControllerButtonStyle);
			Prompts->Keywords = { TEXT("glyphs"), TEXT("icons") };
		}
		{
			FMenuSettingsSection& Keyboard = B.Section(TEXT("KeyboardBindings"), LOCTEXT("KeyboardBindings", "Keyboard & Mouse Bindings"));
			Keyboard.bKeyBindings = true;
			Keyboard.KeyBindingDevice = EMenuKeyBindingDevice::KeyboardMouse;

			FMenuSettingsSection& Pad = B.Section(TEXT("GamepadBindings"), LOCTEXT("GamepadBindings", "Controller Bindings"));
			Pad.bKeyBindings = true;
			Pad.KeyBindingDevice = EMenuKeyBindingDevice::Gamepad;
			B.Action(TEXT("Controls.ResetKeyBindings"), LOCTEXT("ResetBindings", "Reset Bindings"),
				LOCTEXT("ResetBindingsDesc", "Puts every key and button back to the game's defaults."),
				EMenuSettingActionKind::ResetKeyBindings, LOCTEXT("ResetButton", "Reset"),
				LOCTEXT("ResetBindingsConfirm", "Reset every key binding to the default?"));
		}

		// ===========================================================================================================
		//  Accessibility
		// ===========================================================================================================

		B.Page(TEXT("Accessibility"), LOCTEXT("AccessibilityPage", "Accessibility"), 500, EMenuSettingScope::User);
		B.Section(TEXT("Visual"), LOCTEXT("VisualSection", "Visual"));
		{
			UMenuSettingChoice* ColorBlind = B.Choice(TEXT("Accessibility.ColorBlindMode"), LOCTEXT("ColorBlindMode", "Color Blind Mode"),
				LOCTEXT("ColorBlindModeDesc", "Shifts the whole picture's colours so they stay distinct for this type of colour blindness."),
				{
					FMenuSettingOption(TEXT("None"), LOCTEXT("ColorBlindNone", "Off")),
					FMenuSettingOption(TEXT("Deuteranope"), LOCTEXT("Deuteranope", "Deuteranopia"), LOCTEXT("DeuteranopeDesc", "Green-weak: reds and greens look alike. The most common.")),
					FMenuSettingOption(TEXT("Protanope"), LOCTEXT("Protanope", "Protanopia"), LOCTEXT("ProtanopeDesc", "Red-weak: reds look dark and close to greens.")),
					FMenuSettingOption(TEXT("Tritanope"), LOCTEXT("Tritanope", "Tritanopia"), LOCTEXT("TritanopeDesc", "Blue-weak: blues and greens, yellows and pinks look alike.")),
				}, TEXT("None"));
			BuiltIn(ColorBlind, EMenuBuiltInSetting::ColorBlindMode);
			ColorBlind->Scope = EMenuSettingScope::Machine;
			ColorBlind->Keywords = { TEXT("colorblind"), TEXT("colour blind"), TEXT("deuteranopia") };

			UMenuSettingNumber* Strength = B.Number(TEXT("Accessibility.ColorBlindStrength"), LOCTEXT("ColorBlindStrength", "Color Blind Strength"),
				FText::GetEmpty(), 10.f, 1.f, 10.f, 1.f, EMenuNumberFormat::Number);
			BuiltIn(Strength, EMenuBuiltInSetting::ColorBlindStrength);
			Strength->Scope = EMenuSettingScope::Machine;
			Strength->ShownWhen = { IsNot(TEXT("Accessibility.ColorBlindMode"), TEXT("None")) };

			UMenuSettingNumber* UIScale = B.Number(TEXT("Accessibility.UIScale"), LOCTEXT("UIScale", "Interface Size"),
				LOCTEXT("UIScaleDesc", "Makes every menu and the HUD larger or smaller."), 1.f, 0.75f, 1.5f, 0.05f, EMenuNumberFormat::Percent);
			BuiltIn(UIScale, EMenuBuiltInSetting::UIScale);
			UIScale->Scope = EMenuSettingScope::Machine;
			UIScale->Keywords = { TEXT("text size"), TEXT("hud scale"), TEXT("ui scale") };

			UMenuSettingBool* Contrast = B.Bool(TEXT("Accessibility.HighContrast"), LOCTEXT("HighContrast", "High Contrast Menus"),
				LOCTEXT("HighContrastDesc", "Menus use stronger colours and outlines."), false);
			BuiltIn(Contrast, EMenuBuiltInSetting::HighContrast);

			UMenuSettingBool* Motion = B.Bool(TEXT("Accessibility.ReduceMotion"), LOCTEXT("ReduceMotion", "Reduce Motion"),
				LOCTEXT("ReduceMotionDesc", "Menus and notifications fade instead of sliding and bouncing."), false);
			BuiltIn(Motion, EMenuBuiltInSetting::ReduceMotion);

			B.Bool(TEXT("Accessibility.ReduceFlashing"), LOCTEXT("ReduceFlashing", "Reduce Flashing"),
				LOCTEXT("ReduceFlashingDesc", "Fewer and softer flashing effects (photosensitivity)."), false);
		}
		B.Section(TEXT("Subtitles"), LOCTEXT("SubtitlesSection", "Subtitles"));
		{
			UMenuSettingBool* Subtitles = B.Bool(TEXT("Accessibility.Subtitles"), LOCTEXT("Subtitles", "Subtitles"),
				LOCTEXT("SubtitlesDesc", "Shows spoken dialogue as text."), true);
			BuiltIn(Subtitles, EMenuBuiltInSetting::Subtitles);
			Subtitles->Keywords = { TEXT("captions"), TEXT("cc") };

			UMenuSettingChoice* Size = B.Choice(TEXT("Accessibility.SubtitleSize"), LOCTEXT("SubtitleSize", "Subtitle Size"), FText::GetEmpty(),
				{
					FMenuSettingOption(TEXT("0.8"), LOCTEXT("SizeSmall", "Small")),
					FMenuSettingOption(TEXT("1"), LOCTEXT("SizeMedium", "Medium")),
					FMenuSettingOption(TEXT("1.25"), LOCTEXT("SizeLarge", "Large")),
					FMenuSettingOption(TEXT("1.5"), LOCTEXT("SizeExtraLarge", "Extra Large")),
				}, TEXT("1"));
			Size->ShownWhen = { IsOn(TEXT("Accessibility.Subtitles")) };

			UMenuSettingNumber* Background = B.Number(TEXT("Accessibility.SubtitleBackground"), LOCTEXT("SubtitleBackground", "Subtitle Background"),
				LOCTEXT("SubtitleBackgroundDesc", "How dark the box behind subtitles is."), 0.5f, 0.f, 1.f, 0.1f, EMenuNumberFormat::Percent);
			Background->MinText = LOCTEXT("OffValue", "Off");
			Background->ShownWhen = { IsOn(TEXT("Accessibility.Subtitles")) };

			UMenuSettingBool* Speakers = B.Bool(TEXT("Accessibility.SpeakerNames"), LOCTEXT("SpeakerNames", "Speaker Names"),
				LOCTEXT("SpeakerNamesDesc", "Shows who is speaking before each line."), true);
			Speakers->ShownWhen = { IsOn(TEXT("Accessibility.Subtitles")) };
		}
		B.Section(TEXT("Reading"), LOCTEXT("ReadingSection", "Reading & Notifications"));
		{
			UMenuSettingBool* Narration = B.Bool(TEXT("Accessibility.MenuNarration"), LOCTEXT("MenuNarration", "Menu Narration"),
				LOCTEXT("MenuNarrationDesc", "Reads out the setting you are on and its value with the system's screen reader."), false);
			BuiltIn(Narration, EMenuBuiltInSetting::MenuNarration);
			Narration->Keywords = { TEXT("screen reader"), TEXT("text to speech"), TEXT("tts") };

			UMenuSettingBool* Announce = B.Bool(TEXT("Accessibility.ReadNotifications"), LOCTEXT("ReadNotifications", "Read Notifications Aloud"),
				LOCTEXT("ReadNotificationsDesc", "Notifications are read out with the system's screen reader."), false);
			Notification(Announce, EMenuNotificationPreference::ScreenReader);

			UMenuSettingNumber* Duration = B.Number(TEXT("Accessibility.NotificationDuration"), LOCTEXT("NotificationDuration", "Notification Time"),
				LOCTEXT("NotificationDurationDesc", "How long notifications stay on screen."), 1.f, 1.f, 10.f, 0.5f, EMenuNumberFormat::Multiplier);
			Notification(Duration, EMenuNotificationPreference::DurationScale);

			UMenuSettingBool* Keep = B.Bool(TEXT("Accessibility.KeepNotifications"), LOCTEXT("KeepNotifications", "Keep Notifications Until Closed"),
				LOCTEXT("KeepNotificationsDesc", "Notifications never go by themselves."), false);
			Notification(Keep, EMenuNotificationPreference::KeepUntilDismissed);
		}
		B.Section(TEXT("Input"), LOCTEXT("AccessibilityInputSection", "Input"));
		{
			B.Bool(TEXT("Accessibility.ToggleInsteadOfHold"), LOCTEXT("ToggleInsteadOfHold", "Toggle Instead of Hold"),
				LOCTEXT("ToggleInsteadOfHoldDesc", "Actions you hold (aim, sprint, crouch) are pressed once to start and again to stop."), false);
		}

		return Schema;
	}
}

#undef LOCTEXT_NAMESPACE
