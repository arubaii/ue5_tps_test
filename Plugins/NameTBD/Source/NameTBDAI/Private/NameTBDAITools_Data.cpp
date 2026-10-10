// Copyright AR GameSystems 2026. All rights reserved.

// Data tools: UI components on widgets (the Widget Data Binder's rules, lists and sample data; Flow List item looks; any
// other), and any asset's settings (state machines, data assets) - plus the opt-in preview capture.

#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "Engine/DataAsset.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Extensions/UIComponent.h"
#include "Factories/DataAssetFactory.h"
#include "ImageUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/Base64.h"
#include "NameTBDAIHelpers.h"
#include "NameTBDAIJson.h"
#include "NameTBDAITools.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "WidgetBlueprint.h"
#include "WidgetDataBinderComponent.h"

using namespace NameTBDAIJson;

namespace NameTBDAIDataTools
{
	/** Adds (or finds) a component and writes Props onto it; the result names it. */
	FNameTBDAIToolResult SetComponent(const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context, UClass* Class, const TSharedPtr<FJsonObject>& Props, bool bAdd)
	{
		FString Error;
		UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
		UWidget* Widget = Blueprint ? NameTBDAIHelpers::FindWidget(Blueprint, GetString(Args, TEXT("widget")), Error) : nullptr;
		if (!Widget)
		{
			return FNameTBDAIToolResult::Fail(Error);
		}
		UUIComponent* Component = NameTBDAIHelpers::FindOrAddComponent(Blueprint, Widget, Class, bAdd, Error);
		if (!Component)
		{
			return FNameTBDAIToolResult::Fail(Error);
		}
		TArray<FString> Errors;
		if (Props.IsValid())
		{
			NameTBDAIHelpers::ExpandThemeTokens(Props, NameTBDAIHelpers::IsDark(FString()));
			NameTBDAIJson::ApplyToObject(Component, Props, Errors);
		}
		Context.Changes->NoteTouched(Blueprint, FString::Printf(TEXT("%s on %s"), *Class->GetName(), *Widget->GetName()));
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		TArray<FString> Problems;
		const bool bCompiled = NameTBDAIHelpers::Compile(Blueprint, Problems);
		Errors.Append(Problems);
		NameTBDAITools::SetLastAsset(Blueprint);

		FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
		Result.Data->SetStringField(TEXT("component"), Class->GetName());
		Result.Data->SetStringField(TEXT("widget"), Widget->GetName());
		Result.Data->SetBoolField(TEXT("compiled"), bCompiled);
		Result.Data->SetObjectField(TEXT("settings"), ReadObjectNonDefault(Component, 2));
		Result.Warnings = Errors;
		return Result;
	}

	/** add_trigger / add_list_binding / add_sample_set: appends entries to one of the Widget Data Binder's lists. */
	FNameTBDAIToolResult AppendToBinder(const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context, const FString& ListField, const FString& ArgName)
	{
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		const FString ViewModel = GetString(Args, TEXT("view_model_class"));
		if (!ViewModel.IsEmpty())
		{
			Props->SetStringField(TEXT("ViewModelClass"), ViewModel);
		}
		Props->SetArrayField(ListField + TEXT("+"), GetArray(Args, ArgName));
		return SetComponent(Args, Context, UWidgetDataBinderComponent::StaticClass(), Props, true);
	}

	FString PngBase64(const TArray<FColor>& Pixels, int32 Width, int32 Height)
	{
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		return FBase64::Encode(Png.GetData(), static_cast<uint32>(Png.Num()));
	}
}

namespace NameTBDAITools
{
	void RegisterDataTools()
	{
		using namespace NameTBDAIDataTools;

		Register({
			TEXT("set_component"),
			TEXT("Adds a UI component to a widget (if it has none of that class) and/or sets its fields: WidgetAnimationComponent (Anim In / Anim Out entries, State Machines, Parameters), WidgetDataBinderComponent (View Model, Field Bindings, Triggers, List Bindings, Mock Values, Sample Sets), FlowListItemStyle (selected / unselected looks)... list_options components; fields: list_options fields <class>. \"List+\" appends."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Str(TEXT("widget"), TEXT("Widget it's on (omit: the root)."))
				.Str(TEXT("component"), TEXT("Component class."), true)
				.Obj(TEXT("props"), TEXT("Fields to set."))
				.Bool(TEXT("add"), TEXT("Add it when missing (default true)."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UClass* Class = FindClass(GetString(Args, TEXT("component")), UUIComponent::StaticClass(), &Error);
				if (!Class)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				return SetComponent(Args, Context, Class, GetObject(Args, TEXT("props")), GetBool(Args, TEXT("add"), true));
			}
		});

		const FString BinderHelp = TEXT(" On the widget's Widget Data Binder (added if missing; view_model_class sets its View Model). Entry fields: list_options fields ");

		Register({
			TEXT("add_trigger"),
			FString(TEXT("Adds data rules: when a View Model field changes or crosses a value, play / stop an animation, set an animation parameter, or count a number into another field (Went Up / Went Down, Compare To another field, Count Into Field).")) + BinderHelp + TEXT("FWidgetFieldTrigger."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Str(TEXT("widget"), TEXT("Widget the binder is on (omit: the root)."))
				.Str(TEXT("view_model_class"), TEXT("The binder's View Model class, when setting it up."))
				.Arr(TEXT("triggers"), TEXT("FWidgetFieldTrigger entries."), TEXT("object"), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context) { return AppendToBinder(Args, Context, TEXT("Triggers"), TEXT("triggers")); }
		});

		Register({
			TEXT("add_list_binding"),
			FString(TEXT("Binds a View Model array to a list: one entry widget per element in a Flow List (animated enter / exit / slide) or any panel, matched by Key Field.")) + BinderHelp + TEXT("FWidgetListBinding."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Str(TEXT("widget"), TEXT("Widget the binder is on (omit: the root)."))
				.Str(TEXT("view_model_class"), TEXT("The binder's View Model class, when setting it up."))
				.Arr(TEXT("list_bindings"), TEXT("FWidgetListBinding entries."), TEXT("object"), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context) { return AppendToBinder(Args, Context, TEXT("ListBindings"), TEXT("list_bindings")); }
		});

		Register({
			TEXT("add_sample_set"),
			FString(TEXT("Adds sample data the designer's Data Preview steps through (values as text by field, list rows like \"Name=Ann, Score=1200\") - so a leaderboard works in the designer without play.")) + BinderHelp + TEXT("FWidgetDataSampleSet."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Str(TEXT("widget"), TEXT("Widget the binder is on (omit: the root)."))
				.Str(TEXT("view_model_class"), TEXT("The binder's View Model class, when setting it up."))
				.Arr(TEXT("sample_sets"), TEXT("FWidgetDataSampleSet entries."), TEXT("object"), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context) { return AppendToBinder(Args, Context, TEXT("SampleSets"), TEXT("sample_sets")); }
		});

		Register({
			TEXT("create_asset"),
			TEXT("Makes a data asset of a class - a Widget Animation State Machine (WidgetAnimStateMachineAsset: Inputs, Layers with States and Transitions), a Context Menu preset, an Info Card data asset - with fields set."),
			FNameTBDAISchema()
				.Str(TEXT("class"), TEXT("Data asset class."), true)
				.Str(TEXT("name"), TEXT("Asset name."), true)
				.Str(TEXT("folder"), TEXT("Content folder (default: the Output Folder)."))
				.Obj(TEXT("props"), TEXT("Fields to set."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UClass* Class = FindClass(GetString(Args, TEXT("class")), UDataAsset::StaticClass(), &Error);
				if (!Class)
				{
					return FNameTBDAIToolResult::Fail(Error + TEXT(" (Widget Animations: create_widget_animation; materials, particles and Widget Blueprints have their own tools)."));
				}
				UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
				Factory->DataAssetClass = Class;
				UObject* Asset = NameTBDAIHelpers::CreateAsset(GetString(Args, TEXT("folder")), NameTBDAIHelpers::MakeAssetName(GetString(Args, TEXT("name")), FString()), Class, Factory, Context, Error);
				if (!Asset)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				Asset->SetFlags(RF_Transactional);
				TArray<FString> Errors;
				if (TSharedPtr<FJsonObject> Props = GetObject(Args, TEXT("props")))
				{
					NameTBDAIHelpers::ExpandThemeTokens(Props, NameTBDAIHelpers::IsDark(FString()));
					ApplyToObject(Asset, Props, Errors);
				}
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetStringField(TEXT("asset"), Asset->GetPathName());
				Result.Warnings = Errors;
				return Result;
			}
		});

		Register({
			TEXT("set_asset"),
			TEXT("Sets fields on any asset (paths and \"List+\" appends work): a state machine's layers, states and transitions, a Widget Animation's groups or speed, a data asset. Read it first with get_asset."),
			FNameTBDAISchema()
				.Str(TEXT("asset"), TEXT("Asset path or name."), true)
				.Obj(TEXT("props"), TEXT("Fields to set."), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UObject* Asset = NameTBDAIHelpers::FindAsset(GetString(Args, TEXT("asset")), nullptr, Error);
				if (!Asset)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				if (Cast<UWidgetBlueprint>(Asset))
				{
					return FNameTBDAIToolResult::Fail(TEXT("For a Widget Blueprint use set_widget / add_widgets."));
				}
				TArray<FString> Errors;
				TSharedPtr<FJsonObject> Props = GetObject(Args, TEXT("props"));
				NameTBDAIHelpers::ExpandThemeTokens(Props, NameTBDAIHelpers::IsDark(FString()));
				ApplyToObject(Asset, Props, Errors);
				Context.Changes->NoteTouched(Asset, FString::Printf(TEXT("Changed %s"), *Asset->GetName()));
				NameTBDAITools::SetLastAsset(Asset);
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Warnings = Errors;
				return Result;
			}
		});
	}

	void RegisterCaptureTools()
	{
		using namespace NameTBDAIDataTools;

		Register({
			TEXT("capture_preview"),
			TEXT("Renders a Widget Blueprint (as it looks after 'seconds' of playing: entrances done, particles flowing) to an image you can look at, to check layout and contrast - text over a glow, things off the card. Costs an image's tokens: use once per build, at most."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Arr(TEXT("size"), TEXT("[width, height] (default: its own size, else 1280x720; at most 1024 on a side)."), TEXT("number"))
				.Num(TEXT("seconds"), TEXT("How long it plays before the picture (default 1)."))
				.Build(),
			false, true, true,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
				if (!Blueprint)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				UClass* Generated = Blueprint->GeneratedClass;
				UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
				if (!Generated || !Generated->IsChildOf(UUserWidget::StaticClass()) || !World)
				{
					return FNameTBDAIToolResult::Fail(TEXT("Compile the Widget Blueprint first."));
				}

				UUserWidget* Instance = CreateWidget<UUserWidget>(World, TSubclassOf<UUserWidget>(Generated));
				if (!Instance)
				{
					return FNameTBDAIToolResult::Fail(TEXT("Couldn't make the widget."));
				}
				TSharedRef<SWidget> Slate = Instance->TakeWidget();
				Slate->SlatePrepass(1.f);

				FVector2D Size = Slate->GetDesiredSize();
				const TArray<TSharedPtr<FJsonValue>> Asked = GetArray(Args, TEXT("size"));
				if (Asked.Num() == 2)
				{
					Size = FVector2D(Asked[0]->AsNumber(), Asked[1]->AsNumber());
				}
				if (Size.X < 8.0 || Size.Y < 8.0)
				{
					Size = FVector2D(1280.0, 720.0);
				}
				const double Fit = FMath::Min(1.0, 1024.0 / FMath::Max(Size.X, Size.Y));
				const FVector2D DrawSize = Size;
				const FIntPoint Pixels(FMath::Max(8, FMath::RoundToInt(Size.X * Fit)), FMath::Max(8, FMath::RoundToInt(Size.Y * Fit)));

				UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(FVector2D(Pixels), TF_Bilinear, /*bUseGammaCorrection=*/true);
				TUniquePtr<FWidgetRenderer> Renderer = MakeUnique<FWidgetRenderer>(/*bUseGammaCorrection=*/true, /*bInClearTarget=*/true);
				const float Seconds = FMath::Clamp(static_cast<float>(GetNumber(Args, TEXT("seconds"), 1.0)), 0.f, 5.f);
				const int32 Frames = FMath::Max(1, FMath::RoundToInt(Seconds * 30.f));
				for (int32 Frame = 0; Frame < Frames; ++Frame)
				{
					// Drawn at its own size, scaled into the picture.
					Renderer->SetIsPrepassNeeded(true);
					Renderer->DrawWidget(Target, Slate, static_cast<float>(Fit), FVector2D(Pixels), 1.f / 30.f);
				}
				FlushRenderingCommands();

				TArray<FColor> Colors;
				FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
				const bool bRead = Resource && Resource->ReadPixels(Colors);
				Instance->RemoveFromParent();
				Instance->MarkAsGarbage();
				Target->MarkAsGarbage();
				if (!bRead || Colors.Num() != Pixels.X * Pixels.Y)
				{
					return FNameTBDAIToolResult::Fail(TEXT("Couldn't read the picture back."));
				}

				FNameTBDAIImage Image;
				Image.Base64 = PngBase64(Colors, Pixels.X, Pixels.Y);
				Image.Width = Pixels.X;
				Image.Height = Pixels.Y;
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
				Result.Data->SetStringField(TEXT("size"), FString::Printf(TEXT("%dx%d (drawn at %.0fx%.0f)"), Pixels.X, Pixels.Y, DrawSize.X, DrawSize.Y));
				Result.Images.Add(MoveTemp(Image));
				return Result;
			}
		});
	}
}
