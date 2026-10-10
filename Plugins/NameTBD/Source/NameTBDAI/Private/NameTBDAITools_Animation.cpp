// Copyright AR GameSystems 2026. All rights reserved.

// Animation tools: Widget Animation assets (groups, tracks, keys, easing, springs, parameters), motion recipes, track
// presets, and playing an animation on a widget through its Widget Animation Component. Built on the track scripting layer
// (WidgetAnimScripting.h), so what is made is what the Widget Animations panel would have made.

#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Factories/DataAssetFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBDAIDocs.h"
#include "NameTBDAIHelpers.h"
#include "NameTBDAIJson.h"
#include "NameTBDAITheme.h"
#include "NameTBDAITools.h"
#include "WidgetAnimScripting.h"
#include "WidgetAnimTrackPresets.h"
#include "WidgetBlueprint.h"

using namespace NameTBDAIJson;

namespace NameTBDAIAnimationTools
{
	/** Resources/AI/Motions.json: named motions ("pop_in", "pulse_loop") as tracks with {placeholders}. */
	TSharedPtr<FJsonObject> LoadRecipes()
	{
		TSharedPtr<FJsonObject> Recipes = ParseObject(NameTBDAIDocs::LoadResourceText(TEXT("Motions.json")));
		return Recipes.IsValid() ? Recipes : MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> FindRecipe(const FString& Name, FString& OutError)
	{
		const TSharedPtr<FJsonObject> Recipes = LoadRecipes();
		const FString Key = Normalize(Name);
		TArray<FString> Names;
		for (const auto& Pair : Recipes->Values)
		{
			if (Normalize(Pair.Key) == Key && Pair.Value->Type == EJson::Object)
			{
				return ParseObject(ToString(Pair.Value->AsObject()));
			}
			Names.Add(Pair.Key);
		}
		OutError = FString::Printf(TEXT("No motion '%s'. Motions: %s."), *Name, *FString::Join(Names, TEXT(", ")));
		return nullptr;
	}

	/** Replaces "{name}" strings with Params' values (or the recipe's defaults), recursively. */
	void FillPlaceholders(TSharedPtr<FJsonValue>& Value, const TSharedPtr<FJsonObject>& Params)
	{
		if (!Value.IsValid())
		{
			return;
		}
		if (Value->Type == EJson::String)
		{
			const FString Text = Value->AsString();
			if (Text.StartsWith(TEXT("{")) && Text.EndsWith(TEXT("}")))
			{
				const FString Name = Text.Mid(1, Text.Len() - 2);
				if (TSharedPtr<FJsonValue> Found = Params->TryGetField(Name))
				{
					Value = Found;
				}
			}
		}
		else if (Value->Type == EJson::Array)
		{
			TArray<TSharedPtr<FJsonValue>> Items = Value->AsArray();
			for (TSharedPtr<FJsonValue>& Item : Items)
			{
				FillPlaceholders(Item, Params);
			}
			Value = MakeShared<FJsonValueArray>(Items);
		}
		else if (Value->Type == EJson::Object)
		{
			for (auto& Pair : Value->AsObject()->Values)
			{
				FillPlaceholders(Pair.Value, Params);
			}
		}
	}

	/** Tells the asset and anything showing it that its tracks changed - what the scripting layer does for an asset. */
	void NotifyAnimationChanged(UWidgetAnimPresetAsset* Asset)
	{
		UWidgetAnimTransformLibrary::SyncAllTransformAnims(Asset);
		UWidgetAnimLibrary::AssignDefaultValueNames(Asset->Layers);
		FProperty* LayersProperty = FindFProperty<FProperty>(UWidgetAnimPresetAsset::StaticClass(), TEXT("Layers"));
		FPropertyChangedEvent Event(LayersProperty, EPropertyChangeType::ValueSet);
		Asset->PostEditChangeProperty(Event);
	}

	/**
	 * Writes a track's fields. "$Param" / "-$Param" binds a field to an animation parameter; a field the track doesn't have is
	 * looked for in its registered type's settings (TypeParams).
	 */
	void WriteTrackFields(UWidgetAnimScriptAnimation* Script, UWidgetAnimPresetAsset* Asset, int32 Track, const TSharedPtr<FJsonObject>& Fields, bool bDark, TArray<FString>& OutErrors)
	{
		if (!Fields.IsValid() || !Asset->Layers.IsValidIndex(Track))
		{
			return;
		}
		NameTBDAIHelpers::ExpandThemeTokens(Fields, bDark);
		for (const auto& Pair : Fields->Values)
		{
			FString Text;
			if (Pair.Value->TryGetString(Text) && (Text.StartsWith(TEXT("$")) || Text.StartsWith(TEXT("-$"))))
			{
				const bool bMirrored = Text.StartsWith(TEXT("-"));
				if (!Script->BindTrackField(Track, Pair.Key, Text.Mid(bMirrored ? 2 : 1), bMirrored ? -1.f : 1.f, 0.f))
				{
					OutErrors.Add(FString::Printf(TEXT("Track %d %s: %s"), Track, *Pair.Key, *Script->GetLastError()));
				}
				continue;
			}
			if (Normalize(Pair.Key) == TEXT("start") && Pair.Value->Type == EJson::Number)
			{
				Script->SetTrackStart(Track, static_cast<float>(Pair.Value->AsNumber()));
				continue;
			}

			FString First = Pair.Key;
			Pair.Key.Split(TEXT("."), &First, nullptr);
			int32 Bracket = INDEX_NONE;
			if (First.FindChar(TEXT('['), Bracket))
			{
				First = First.Left(Bracket);
			}
			FString Key = Pair.Key;
			if (!FindField(FWidgetAnimLayer::StaticStruct(), First) && Asset->Layers[Track].TypeParams.IsValid()
				&& FindField(Asset->Layers[Track].TypeParams.GetScriptStruct(), First))
			{
				Key = TEXT("TypeParams.") + Pair.Key;
			}
			TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
			One->SetField(Key, Pair.Value);
			TArray<FString> Errors;
			WriteObject(FWidgetAnimLayer::StaticStruct(), &Asset->Layers[Track], One, Asset, Errors);
			for (const FString& Error : Errors)
			{
				OutErrors.Add(FString::Printf(TEXT("Track %d: %s"), Track, *Error));
			}
		}
	}

	/** Group index for a target widget path: an existing group for it, or a new one. Empty target: the widget played on. */
	int32 FindOrAddGroup(UWidgetAnimScriptAnimation* Script, const FString& Target, const FString& GroupName)
	{
		const FString Wanted = Target.Equals(TEXT("(this widget)"), ESearchCase::IgnoreCase) || Target.Equals(TEXT("None"), ESearchCase::IgnoreCase) ? FString() : Target;
		for (int32 Group = 0; Group < Script->GetNumGroups(); ++Group)
		{
			if (Script->GetGroupTarget(Group).Equals(Wanted, ESearchCase::IgnoreCase)
				&& (GroupName.IsEmpty() || Script->GetGroupName(Group).Equals(GroupName, ESearchCase::IgnoreCase)))
			{
				return Group;
			}
		}
		return Script->AddGroup(GroupName.IsEmpty() ? (Wanted.IsEmpty() ? FString(TEXT("This Widget")) : Wanted) : GroupName, Wanted);
	}

	/**
	 * Adds tracks from specs: {type, target (widget the group animates; omit for the widget it's played on), group (name),
	 * start (seconds), name, stagger ({"$type": ...}), fields {...}}. Returns the new tracks' indices.
	 */
	TArray<int32> AddTracks(UWidgetAnimScriptAnimation* Script, UWidgetAnimPresetAsset* Asset, const TArray<TSharedPtr<FJsonValue>>& Specs, bool bDark, TArray<FString>& OutErrors)
	{
		TArray<int32> Added;
		for (const TSharedPtr<FJsonValue>& SpecValue : Specs)
		{
			if (!SpecValue.IsValid() || SpecValue->Type != EJson::Object)
			{
				OutErrors.Add(TEXT("Each track is an object."));
				continue;
			}
			const TSharedPtr<FJsonObject> Spec = SpecValue->AsObject();
			const FString Type = GetString(Spec, TEXT("type"));
			const int32 Group = FindOrAddGroup(Script, GetString(Spec, TEXT("target")), GetString(Spec, TEXT("group")));
			const int32 Track = Script->AddTrack(Type, Group);
			if (Track == INDEX_NONE)
			{
				OutErrors.Add(FString::Printf(TEXT("Track '%s': %s (list_options track_types)."), *Type, *Script->GetLastError()));
				continue;
			}
			Added.Add(Track);
			Asset->Modify();

			if (TSharedPtr<FJsonObject> Stagger = GetObject(Spec, TEXT("stagger")))
			{
				TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
				One->SetObjectField(TEXT("Expander"), Stagger);
				TArray<FString> Errors;
				WriteObject(FWidgetAnimLayer::StaticStruct(), &Asset->Layers[Track], One, Asset, Errors);
				OutErrors.Append(Errors);
			}
			WriteTrackFields(Script, Asset, Track, GetObject(Spec, TEXT("fields")), bDark, OutErrors);
			if (Has(Spec, TEXT("start")))
			{
				Script->SetTrackStart(Track, static_cast<float>(GetNumber(Spec, TEXT("start"))));
			}
			const FString Name = GetString(Spec, TEXT("name"));
			if (!Name.IsEmpty())
			{
				Script->SetTrackName(Track, Name);
			}
		}
		return Added;
	}

	/** The animation and its tracks, short: what the model needs to edit it next. */
	TSharedPtr<FJsonObject> Summarize(UWidgetAnimScriptAnimation* Script, UWidgetAnimPresetAsset* Asset)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("animation"), Asset->GetPathName());
		Out->SetNumberField(TEXT("length_seconds"), FMath::RoundToDouble(Script->GetLength() * 1000.0) / 1000.0);
		TArray<TSharedPtr<FJsonValue>> Tracks;
		for (int32 Track = 0; Track < Script->GetNumTracks(); ++Track)
		{
			const FString Target = Script->GetTrackTarget(Track);
			Tracks.Add(Str(FString::Printf(TEXT("%d: %s%s on %s, %.2fs-%.2fs"), Track, *Script->GetTrackType(Track),
				Script->GetTrackName(Track).IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" '%s'"), *Script->GetTrackName(Track)),
				Target.IsEmpty() ? TEXT("(this widget)") : *Target, Script->GetTrackStart(Track), Script->GetTrackStart(Track) + Script->GetTrackLength(Track))));
		}
		Out->SetArrayField(TEXT("tracks"), Tracks);
		const TArray<FString> Parameters = Script->GetParameterNames();
		if (Parameters.Num() > 0)
		{
			Out->SetField(TEXT("parameters"), Arr(Parameters));
		}
		return Out;
	}

	UWidgetAnimPresetAsset* FindAnimation(const FString& Name, FString& OutError)
	{
		if (Name.IsEmpty())
		{
			if (UWidgetAnimPresetAsset* Last = Cast<UWidgetAnimPresetAsset>(NameTBDAITools::GetLastAsset()))
			{
				return Last;
			}
			OutError = TEXT("Name the animation.");
			return nullptr;
		}
		return Cast<UWidgetAnimPresetAsset>(NameTBDAIHelpers::FindAsset(Name, UWidgetAnimPresetAsset::StaticClass(), OutError));
	}

	UWidgetAnimPresetAsset* CreateAnimationAsset(const FString& Folder, const FString& Name, const FString& Description, FNameTBDAIToolContext& Context, FString& OutError)
	{
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = UWidgetAnimPresetAsset::StaticClass();
		UWidgetAnimPresetAsset* Asset = Cast<UWidgetAnimPresetAsset>(NameTBDAIHelpers::CreateAsset(Folder.IsEmpty() ? FString(TEXT("Animations")) : Folder,
			NameTBDAIHelpers::MakeAssetName(Name, TEXT("WA_")), UWidgetAnimPresetAsset::StaticClass(), Factory, Context, OutError));
		if (Asset && !Description.IsEmpty())
		{
			Asset->Description = FText::FromString(Description);
		}
		if (Asset)
		{
			Asset->SetFlags(RF_Transactional);
		}
		return Asset;
	}

	/** Builds a recipe's tracks into Asset. */
	bool ApplyRecipe(UWidgetAnimScriptAnimation* Script, UWidgetAnimPresetAsset* Asset, const TSharedPtr<FJsonObject>& Recipe, const TSharedPtr<FJsonObject>& InParams,
		const FString& Target, bool bDark, TArray<FString>& OutErrors)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		if (TSharedPtr<FJsonObject> Defaults = GetObject(Recipe, TEXT("params")))
		{
			Params->Values = Defaults->Values;
		}
		if (InParams.IsValid())
		{
			for (const auto& Pair : InParams->Values)
			{
				Params->SetField(Pair.Key, Pair.Value);
			}
		}
		TArray<TSharedPtr<FJsonValue>> Tracks = GetArray(Recipe, TEXT("tracks"));
		for (TSharedPtr<FJsonValue>& Track : Tracks)
		{
			FillPlaceholders(Track, Params);
			if (!Target.IsEmpty() && Track->Type == EJson::Object && !Track->AsObject()->HasField(TEXT("target")))
			{
				Track->AsObject()->SetStringField(TEXT("target"), Target);
			}
		}
		return AddTracks(Script, Asset, Tracks, bDark, OutErrors).Num() > 0;
	}
}

namespace NameTBDAIReadTools
{
	TSharedPtr<FJsonObject> MotionRecipes()
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		for (const auto& Pair : NameTBDAIAnimationTools::LoadRecipes()->Values)
		{
			const TSharedPtr<FJsonObject> Recipe = Pair.Value->Type == EJson::Object ? Pair.Value->AsObject() : nullptr;
			if (!Recipe.IsValid())
			{
				continue;
			}
			FString Line = GetString(Recipe, TEXT("about"));
			if (TSharedPtr<FJsonObject> Params = GetObject(Recipe, TEXT("params")))
			{
				TArray<FString> Names;
				for (const auto& Param : Params->Values)
				{
					Names.Add(FString::Printf(TEXT("%s=%s"), *Param.Key, *ToString(Param.Value)));
				}
				Line += FString::Printf(TEXT(" [params: %s]"), *FString::Join(Names, TEXT(", ")));
			}
			Out->SetStringField(Pair.Key, Line);
		}
		return Out;
	}
}

namespace NameTBDAITools
{
	void RegisterAnimationTools()
	{
		using namespace NameTBDAIAnimationTools;

		const FString TrackHelp = TEXT("A track spec: {type (Fade, Scale, Slide, Rotate, Pulse, Shake, Transform, MaterialScalar, MaterialVector, Shape, Design, or a registered type's name - list_options track_types), target (widget name the track moves; omit for the widget the animation plays on), start (seconds), name, stagger ({\"$type\": \"EachChild\", ...}), fields {FWidgetAnimLayer fields: Duration, Delay, Easing (Linear/EaseIn/EaseOut/EaseInOut/Hold), Timing (Tween/Spring), Spring (\"@spring.Bouncy\"), StartOpacity, EndOpacity, StartScale [x,y], EndScale, Overshoot, SlideFromDirection (Up/Down/Left/Right), SlideDistance, StartAngle, EndAngle, PulseScaleAmount, ScalarParameterName, StartScalarValue, EndScalarValue, VectorParameterName, StartVectorValue, EndVectorValue, Transform.Keys..., TypeParams.<setting>; \"$Param\" binds a field to an animation parameter}}.");

		// ---- create_widget_animation ----
		Register({
			TEXT("create_widget_animation"),
			FString(TEXT("Makes a Widget Animation asset (WA_), optionally with tracks, or from a motion recipe (list_options motion_recipes). To play it on a widget, use animate_widget. ")) + TrackHelp,
			FNameTBDAISchema()
				.Str(TEXT("name"), TEXT("Asset name."), true)
				.Str(TEXT("folder"), TEXT("Content folder (default: Output Folder/Animations)."))
				.Str(TEXT("description"), TEXT("What it is for (shown as its tooltip)."))
				.Str(TEXT("recipe"), TEXT("Start from a motion recipe."))
				.Obj(TEXT("params"), TEXT("The recipe's params (duration, distance...)."))
				.Arr(TEXT("tracks"), TEXT("Track specs."), TEXT("object"))
				.Num(TEXT("speed"), TEXT("Animation Speed Multiplier."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetAnimPresetAsset* Asset = CreateAnimationAsset(GetString(Args, TEXT("folder")), GetString(Args, TEXT("name")), GetString(Args, TEXT("description")), Context, Error);
				if (!Asset)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				UWidgetAnimScriptAnimation* Script = UWidgetAnimScriptLibrary::EditAnimationAsset(Asset);
				if (!Script)
				{
					return FNameTBDAIToolResult::Fail(TEXT("The animation can't be edited."));
				}
				Script->BeginEdit(TEXT("NameTBD AI: Build Animation"));
				TArray<FString> Errors;
				const bool bDark = NameTBDAIHelpers::IsDark(FString());
				const FString RecipeName = GetString(Args, TEXT("recipe"));
				if (!RecipeName.IsEmpty())
				{
					TSharedPtr<FJsonObject> Recipe = FindRecipe(RecipeName, Error);
					if (!Recipe.IsValid())
					{
						Errors.Add(Error);
					}
					else
					{
						ApplyRecipe(Script, Asset, Recipe, GetObject(Args, TEXT("params")), FString(), bDark, Errors);
					}
				}
				AddTracks(Script, Asset, GetArray(Args, TEXT("tracks")), bDark, Errors);
				if (Has(Args, TEXT("speed")))
				{
					Asset->AnimationSpeedMultiplier = FMath::Max(0.01f, static_cast<float>(GetNumber(Args, TEXT("speed"), 1.0)));
				}
				Script->EndEdit();
				NotifyAnimationChanged(Asset);
				Context.Changes->NoteTouched(Asset);

				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok(Summarize(Script, Asset));
				Result.Warnings = Errors;
				NameTBDAITools::SetLastAsset(Asset);
				return Result;
			}
		});

		// ---- add_tracks ----
		Register({
			TEXT("add_tracks"),
			FString(TEXT("Adds tracks to a Widget Animation, after its last one unless 'start' says. ")) + TrackHelp,
			FNameTBDAISchema()
				.Str(TEXT("animation"), TEXT("WA_ asset path or name (omit: the one built last)."))
				.Arr(TEXT("tracks"), TEXT("Track specs."), TEXT("object"), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetAnimPresetAsset* Asset = FindAnimation(GetString(Args, TEXT("animation")), Error);
				UWidgetAnimScriptAnimation* Script = Asset ? UWidgetAnimScriptLibrary::EditAnimationAsset(Asset) : nullptr;
				if (!Script)
				{
					return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString(TEXT("The animation can't be edited.")) : Error);
				}
				Script->BeginEdit(TEXT("NameTBD AI: Add Tracks"));
				TArray<FString> Errors;
				AddTracks(Script, Asset, GetArray(Args, TEXT("tracks")), NameTBDAIHelpers::IsDark(FString()), Errors);
				Script->EndEdit();
				NotifyAnimationChanged(Asset);
				Context.Changes->NoteTouched(Asset, FString::Printf(TEXT("Added tracks to %s"), *Asset->GetName()));
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok(Summarize(Script, Asset));
				Result.Warnings = Errors;
				return Result;
			}
		});

		// ---- set_track ----
		Register({
			TEXT("set_track"),
			TEXT("Changes one track of a Widget Animation: fields (paths work, \"$Param\" binds), start (seconds), target widget, name. Other tracks keep their time."),
			FNameTBDAISchema()
				.Str(TEXT("animation"), TEXT("WA_ asset (omit: the one built last)."))
				.Int(TEXT("track"), TEXT("Track index (see the tracks list a tool returned, or get_asset)."), true)
				.Obj(TEXT("fields"), TEXT("Fields to set."))
				.Num(TEXT("start"), TEXT("Where it starts, seconds."))
				.Str(TEXT("target"), TEXT("Widget it moves (empty: the widget it plays on)."))
				.Str(TEXT("name"), TEXT("Track name."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetAnimPresetAsset* Asset = FindAnimation(GetString(Args, TEXT("animation")), Error);
				UWidgetAnimScriptAnimation* Script = Asset ? UWidgetAnimScriptLibrary::EditAnimationAsset(Asset) : nullptr;
				if (!Script)
				{
					return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString(TEXT("The animation can't be edited.")) : Error);
				}
				const int32 Track = static_cast<int32>(GetNumber(Args, TEXT("track"), -1));
				if (Track < 0 || Track >= Script->GetNumTracks())
				{
					return FNameTBDAIToolResult::Fail(FString::Printf(TEXT("No track %d: it has %d."), Track, Script->GetNumTracks()));
				}
				Script->BeginEdit(TEXT("NameTBD AI: Edit Track"));
				TArray<FString> Errors;
				if (Has(Args, TEXT("target")) && !Script->SetTrackTarget(Track, GetString(Args, TEXT("target"))))
				{
					Errors.Add(Script->GetLastError());
				}
				// Each scalar field goes through the scripting layer, which keeps every other track where it was.
				if (TSharedPtr<FJsonObject> Fields = GetObject(Args, TEXT("fields")))
				{
					NameTBDAIHelpers::ExpandThemeTokens(Fields, NameTBDAIHelpers::IsDark(FString()));
					TSharedPtr<FJsonObject> Complex = MakeShared<FJsonObject>();
					for (const auto& Pair : Fields->Values)
					{
						FString Text;
						FLinearColor Color;
						if (Pair.Value->Type == EJson::Number)
						{
							Text = FString::SanitizeFloat(Pair.Value->AsNumber());
						}
						else if (Pair.Value->Type == EJson::Boolean)
						{
							Text = Pair.Value->AsBool() ? TEXT("True") : TEXT("False");
						}
						else if (Pair.Value->Type == EJson::String && Pair.Value->AsString().StartsWith(TEXT("#")) && ParseColor(Pair.Value, Color))
						{
							Text = Color.ToString();
						}
						else if (Pair.Value->Type == EJson::String)
						{
							Text = Pair.Value->AsString();
						}
						else
						{
							Complex->SetField(Pair.Key, Pair.Value);
							continue;
						}
						if (!Script->SetTrackProperty(Track, Pair.Key, Text))
						{
							Errors.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *Script->GetLastError()));
						}
					}
					if (Complex->Values.Num() > 0)
					{
						Asset->Modify();
						WriteTrackFields(Script, Asset, Track, Complex, NameTBDAIHelpers::IsDark(FString()), Errors);
					}
				}
				if (Has(Args, TEXT("start")))
				{
					Script->SetTrackStart(Track, static_cast<float>(GetNumber(Args, TEXT("start"))));
				}
				if (Has(Args, TEXT("name")))
				{
					Script->SetTrackName(Track, GetString(Args, TEXT("name")));
				}
				Script->EndEdit();
				NotifyAnimationChanged(Asset);
				Context.Changes->NoteTouched(Asset, FString::Printf(TEXT("Changed track %d of %s"), Track, *Asset->GetName()));
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok(Summarize(Script, Asset));
				Result.Warnings = Errors;
				return Result;
			}
		});

		// ---- remove_tracks ----
		Register({
			TEXT("remove_tracks"),
			TEXT("Removes tracks from a Widget Animation by index; the others keep their time."),
			FNameTBDAISchema()
				.Str(TEXT("animation"), TEXT("WA_ asset (omit: the one built last)."))
				.Arr(TEXT("tracks"), TEXT("Track indices."), TEXT("integer"), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetAnimPresetAsset* Asset = FindAnimation(GetString(Args, TEXT("animation")), Error);
				UWidgetAnimScriptAnimation* Script = Asset ? UWidgetAnimScriptLibrary::EditAnimationAsset(Asset) : nullptr;
				if (!Script)
				{
					return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString(TEXT("The animation can't be edited.")) : Error);
				}
				TArray<int32> Tracks;
				for (const TSharedPtr<FJsonValue>& Value : GetArray(Args, TEXT("tracks")))
				{
					double Number = 0.0;
					if (Value->TryGetNumber(Number))
					{
						Tracks.Add(static_cast<int32>(Number));
					}
				}
				if (!Script->RemoveTracks(Tracks))
				{
					return FNameTBDAIToolResult::Fail(Script->GetLastError());
				}
				NotifyAnimationChanged(Asset);
				Context.Changes->NoteTouched(Asset, FString::Printf(TEXT("Removed %d track(s) from %s"), Tracks.Num(), *Asset->GetName()));
				return FNameTBDAIToolResult::Ok(Summarize(Script, Asset));
			}
		});

		// ---- set_animation_parameter ----
		Register({
			TEXT("set_animation_parameter"),
			TEXT("Declares (or changes) an animation parameter - a named value every use can set (slide 40 px or 200 px, flash red or gold). Bind fields to it with \"$Name\" in add_tracks / set_track fields. remove=true removes it."),
			FNameTBDAISchema()
				.Str(TEXT("animation"), TEXT("WA_ asset (omit: the one built last)."))
				.Str(TEXT("name"), TEXT("Parameter name."), true)
				.Str(TEXT("type"), TEXT("Float, Integer, Bool, Color, Vector2D or Choice."), false, { TEXT("Float"), TEXT("Integer"), TEXT("Bool"), TEXT("Color"), TEXT("Vector2D"), TEXT("Choice") })
				.Str(TEXT("default"), TEXT("Default as text: 40, True, (X=1,Y=0), (R=1,G=0,B=0,A=1), an option."))
				.Arr(TEXT("options"), TEXT("A Choice's options."), TEXT("string"))
				.Bool(TEXT("remove"), TEXT("Remove it (and its bindings)."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetAnimPresetAsset* Asset = FindAnimation(GetString(Args, TEXT("animation")), Error);
				UWidgetAnimScriptAnimation* Script = Asset ? UWidgetAnimScriptLibrary::EditAnimationAsset(Asset) : nullptr;
				if (!Script)
				{
					return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString(TEXT("The animation can't be edited.")) : Error);
				}
				const FString Name = GetString(Args, TEXT("name"));
				const bool bOk = GetBool(Args, TEXT("remove"))
					? Script->RemoveParameter(Name)
					: Script->SetParameter(Name, GetString(Args, TEXT("type"), TEXT("Float")), GetString(Args, TEXT("default")), GetStringArray(Args, TEXT("options")));
				if (!bOk)
				{
					return FNameTBDAIToolResult::Fail(Script->GetLastError());
				}
				Context.Changes->NoteTouched(Asset, FString::Printf(TEXT("Parameter %s on %s"), *Name, *Asset->GetName()));
				return FNameTBDAIToolResult::Ok(Summarize(Script, Asset));
			}
		});

		// ---- track presets ----
		Register({
			TEXT("apply_track_preset"),
			TEXT("Adds a saved track preset's tracks (list_options track_presets) to a Widget Animation, moving 'target', starting at 'start'."),
			FNameTBDAISchema()
				.Str(TEXT("animation"), TEXT("WA_ asset (omit: the one built last)."))
				.Str(TEXT("preset"), TEXT("Preset name."), true)
				.Str(TEXT("target"), TEXT("Widget the tracks move (omit: the widget it plays on)."))
				.Num(TEXT("start"), TEXT("Seconds (default: after the last track)."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetAnimPresetAsset* Asset = FindAnimation(GetString(Args, TEXT("animation")), Error);
				UWidgetAnimScriptAnimation* Script = Asset ? UWidgetAnimScriptLibrary::EditAnimationAsset(Asset) : nullptr;
				if (!Script)
				{
					return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString(TEXT("The animation can't be edited.")) : Error);
				}
				TArray<WidgetAnimTrackPresets::FPreset> Presets;
				WidgetAnimTrackPresets::GetPresets(Presets);
				const FString Key = Normalize(GetString(Args, TEXT("preset")));
				const WidgetAnimTrackPresets::FPreset* Preset = Presets.FindByPredicate([&Key](const WidgetAnimTrackPresets::FPreset& Each) { return Normalize(Each.Name) == Key; });
				if (!Preset)
				{
					return FNameTBDAIToolResult::Fail(TEXT("No such preset (list_options track_presets)."));
				}
				Script->BeginEdit(TEXT("NameTBD AI: Apply Track Preset"));
				const int32 Group = FindOrAddGroup(Script, GetString(Args, TEXT("target")), FString());
				const float Base = Has(Args, TEXT("start")) ? static_cast<float>(GetNumber(Args, TEXT("start"))) : Script->GetLength();
				TArray<int32> Added;
				for (int32 Index = 0; Index < Preset->Layers.Num(); ++Index)
				{
					const int32 Track = Script->AddTrack(TEXT("Fade"), Group);
					if (Track == INDEX_NONE)
					{
						continue;
					}
					Asset->Modify();
					WidgetAnimTrackPresets::ApplyTo(Preset->Layers[Index], Asset->Layers[Track]);
					Script->SetTrackStart(Track, Base + (Preset->Starts.IsValidIndex(Index) ? Preset->Starts[Index] : 0.f));
					Added.Add(Track);
				}
				if (Preset->bCollapsed && Added.Num() > 1)
				{
					Script->CollapseTracks(Added, Preset->Name);
				}
				Script->EndEdit();
				NotifyAnimationChanged(Asset);
				Context.Changes->NoteTouched(Asset, FString::Printf(TEXT("Applied preset %s to %s"), *Preset->Name, *Asset->GetName()));
				return FNameTBDAIToolResult::Ok(Summarize(Script, Asset));
			}
		});

		Register({
			TEXT("save_track_preset"),
			TEXT("Saves tracks of a Widget Animation as a named track preset (the project's Content/WidgetAnimTrackPresets), to reuse anywhere."),
			FNameTBDAISchema()
				.Str(TEXT("animation"), TEXT("WA_ asset (omit: the one built last)."))
				.Arr(TEXT("tracks"), TEXT("Track indices."), TEXT("integer"), true)
				.Str(TEXT("name"), TEXT("Preset name."), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				FString Error;
				UWidgetAnimPresetAsset* Asset = FindAnimation(GetString(Args, TEXT("animation")), Error);
				UWidgetAnimScriptAnimation* Script = Asset ? UWidgetAnimScriptLibrary::EditAnimationAsset(Asset) : nullptr;
				if (!Script)
				{
					return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString(TEXT("The animation can't be edited.")) : Error);
				}
				TArray<FWidgetAnimLayer> Layers;
				TArray<float> Starts;
				for (const TSharedPtr<FJsonValue>& Value : GetArray(Args, TEXT("tracks")))
				{
					double Number = -1.0;
					const int32 Track = Value->TryGetNumber(Number) ? static_cast<int32>(Number) : INDEX_NONE;
					if (Asset->Layers.IsValidIndex(Track) && WidgetAnimTrackPresets::CanSave(Asset->Layers[Track]))
					{
						Layers.Add(Asset->Layers[Track]);
						Starts.Add(Script->GetTrackStart(Track));
					}
				}
				if (Layers.Num() == 0)
				{
					return FNameTBDAIToolResult::Fail(TEXT("No track that can be saved (tracks that play another animation can't)."));
				}
				FText SaveError;
				if (!WidgetAnimTrackPresets::Save(GetString(Args, TEXT("name")), Layers, Starts, Layers.Num() > 1, SaveError))
				{
					return FNameTBDAIToolResult::Fail(SaveError.ToString());
				}
				return FNameTBDAIToolResult::Ok();
			}
		});

		// ---- animate_widget ----
		Register({
			TEXT("animate_widget"),
			TEXT("Plays a Widget Animation on a widget of a Widget Blueprint - through the widget's Widget Animation Component (added if missing): from a motion recipe (made into a new WA_ asset) or an existing animation. when: on_construct (an entrance, default), on_visible (each time it's shown), loop (starts on construct and repeats; ping_pong plays back and forth), by_id (only when Blueprint calls Play Widget Animation with anim_id), out (an exit, played by Set Visibility Animated / Play Out)."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Str(TEXT("widget"), TEXT("The widget to animate (omit: the root)."))
				.Str(TEXT("recipe"), TEXT("Motion recipe (list_options motion_recipes): pop_in, bounce_in, slide_up_in, fade_in, pulse_loop, breathe_loop, float_loop..."))
				.Obj(TEXT("params"), TEXT("Recipe params: duration, delay, distance, scale, amount..."))
				.Str(TEXT("animation"), TEXT("An existing WA_ asset instead of a recipe."))
				.Str(TEXT("when"), TEXT("When it plays."), false, { TEXT("on_construct"), TEXT("on_visible"), TEXT("loop"), TEXT("by_id"), TEXT("out") })
				.Bool(TEXT("ping_pong"), TEXT("With loop: play back and forth (default true)."))
				.Int(TEXT("loop_count"), TEXT("With loop: how many times (0 = forever)."))
				.Str(TEXT("anim_id"), TEXT("Name to play it by from Blueprint (Play Widget Animation)."))
				.Num(TEXT("speed"), TEXT("Playback Speed of this use."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
				UWidget* Widget = Blueprint ? NameTBDAIHelpers::FindWidget(Blueprint, GetString(Args, TEXT("widget")), Error) : nullptr;
				if (!Widget)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}

				TArray<FString> Warnings;
				UWidgetAnimPresetAsset* Animation = nullptr;
				const FString RecipeName = GetString(Args, TEXT("recipe"));
				const FString When = GetString(Args, TEXT("when"), TEXT("on_construct"));
				if (!RecipeName.IsEmpty())
				{
					TSharedPtr<FJsonObject> Recipe = FindRecipe(RecipeName, Error);
					if (!Recipe.IsValid())
					{
						return FNameTBDAIToolResult::Fail(Error);
					}
					const FString Folder = FPackageName::GetLongPackagePath(Blueprint->GetOutermost()->GetName()) / TEXT("Animations");
					Animation = CreateAnimationAsset(Folder, FString::Printf(TEXT("%s_%s_%s"), *Blueprint->GetName().Replace(TEXT("WBP_"), TEXT("")), *Widget->GetName(), *RecipeName),
						GetString(Recipe, TEXT("about")), Context, Error);
					UWidgetAnimScriptAnimation* Script = Animation ? UWidgetAnimScriptLibrary::EditAnimationAsset(Animation) : nullptr;
					if (!Script)
					{
						return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString(TEXT("Couldn't make the animation.")) : Error);
					}
					Script->BeginEdit(TEXT("NameTBD AI: Build Animation"));
					ApplyRecipe(Script, Animation, Recipe, GetObject(Args, TEXT("params")), FString(), NameTBDAIHelpers::IsDark(FString()), Warnings);
					Script->EndEdit();
					NotifyAnimationChanged(Animation);
				}
				else
				{
					Animation = FindAnimation(GetString(Args, TEXT("animation")), Error);
					if (!Animation)
					{
						return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString(TEXT("Give a recipe or an animation.")) : Error);
					}
				}

				UWidgetAnimationComponent* Component = Cast<UWidgetAnimationComponent>(
					NameTBDAIHelpers::FindOrAddComponent(Blueprint, Widget, UWidgetAnimationComponent::StaticClass(), true, Error));
				if (!Component)
				{
					return FNameTBDAIToolResult::Fail(FString::Printf(TEXT("%s (the animation %s was made)."), *Error, *Animation->GetPathName()));
				}
				Component->Modify();

				FWidgetAnimEntry Entry;
				Entry.Animation = Animation;
				Entry.AnimId = FName(*GetString(Args, TEXT("anim_id")));
				Entry.PlaybackSpeed = static_cast<float>(GetNumber(Args, TEXT("speed"), 1.0));
				const bool bOut = When == TEXT("out");
				Entry.bPlayInAnimationOnConstruct = When == TEXT("on_construct") || When == TEXT("loop");
				Entry.bPlayAnimationOnVisibilityHidden = When == TEXT("on_visible") || bOut;
				if (When == TEXT("by_id"))
				{
					Entry.bPlayInAnimationOnConstruct = false;
					if (Entry.AnimId.IsNone())
					{
						Entry.AnimId = FName(*Animation->GetName().Replace(TEXT("WA_"), TEXT("")));
					}
				}
				if (When == TEXT("loop"))
				{
					Entry.bLoop = true;
					Entry.bPingPong = GetBool(Args, TEXT("ping_pong"), true);
					Entry.LoopCount = static_cast<int32>(GetNumber(Args, TEXT("loop_count"), 0));
				}
				Entry.SyncGroupTargets();

				TArray<FWidgetAnimEntry>& List = bOut ? Component->AnimOut : Component->AnimIn;
				List.Add(Entry);
				FProperty* ListProperty = FindFProperty<FProperty>(UWidgetAnimationComponent::StaticClass(), bOut ? TEXT("AnimOut") : TEXT("AnimIn"));
				FPropertyChangedEvent Event(ListProperty, EPropertyChangeType::ArrayAdd);
				Component->PostEditChangeProperty(Event);

				Context.Changes->NoteTouched(Blueprint, FString::Printf(TEXT("%s plays %s (%s)"), *Widget->GetName(), *Animation->GetName(), *When));
				FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
				TArray<FString> Problems;
				const bool bCompiled = NameTBDAIHelpers::Compile(Blueprint, Problems);
				Warnings.Append(Problems);

				UWidgetAnimScriptAnimation* Script = UWidgetAnimScriptLibrary::EditAnimationAsset(Animation);
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok(Script ? Summarize(Script, Animation) : MakeShared<FJsonObject>());
				Result.Data->SetStringField(TEXT("widget"), Widget->GetName());
				Result.Data->SetStringField(TEXT("when"), When);
				Result.Data->SetBoolField(TEXT("compiled"), bCompiled);
				if (!Entry.AnimId.IsNone())
				{
					Result.Data->SetStringField(TEXT("anim_id"), Entry.AnimId.ToString());
				}
				Result.Warnings = Warnings;
				NameTBDAITools::SetLastAsset(Blueprint);
				return Result;
			}
		});
	}
}
