// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimationComponentDetails.h"

#include "SWidgetAnimTimeline.h"
#include "WidgetAnimPanelSource.h"

#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBDColors.h"
#include "NameTBDTooltips.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "IDetailPropertyRow.h"
#include "Misc/ConfigCacheIni.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/ObjectKey.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimationComponentDetails"

// What the inline editor edits, and where its edits are written, told once per list - "Log LogWidgetAnimInlineEditor
// Verbose" adds a line for every commit.
DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimInlineEditor, Log, All);

namespace WidgetAnimTips = NameTBD::Tooltips::WidgetAnimEditor;

namespace
{
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;

	// ---------------------------------------------------------------------------------------------------
	// How tall each list's editor is - dragged from its grip, remembered across sessions
	// ---------------------------------------------------------------------------------------------------

	const TCHAR* EditorHeightConfigSection = TEXT("NameTBD.WidgetAnimations");
	const TCHAR* EditorHeightInKey = TEXT("AdditiveLayersInEditorHeight");
	const TCHAR* EditorHeightOutKey = TEXT("AdditiveLayersOutEditorHeight");

	constexpr float DefaultEditorHeight = 220.f;
	constexpr float MinEditorHeight = 110.f;
	constexpr float MaxEditorHeight = 1000.f;

	float& EditorHeight(bool bOut)
	{
		static float Heights[2] = { -1.f, -1.f };
		float& Height = Heights[bOut ? 1 : 0];
		if (Height < 0.f)
		{
			Height = DefaultEditorHeight;
			float Saved = DefaultEditorHeight;
			if (GConfig && GConfig->GetFloat(EditorHeightConfigSection, bOut ? EditorHeightOutKey : EditorHeightInKey, Saved, GEditorPerProjectIni))
			{
				Height = FMath::Clamp(Saved, MinEditorHeight, MaxEditorHeight);
			}
		}
		return Height;
	}

	void SaveEditorHeight(bool bOut)
	{
		if (GConfig)
		{
			GConfig->SetFloat(EditorHeightConfigSection, bOut ? EditorHeightOutKey : EditorHeightInKey, EditorHeight(bOut), GEditorPerProjectIni);
		}
	}

	/** The lists unfolded this session, per component. Every list starts folded. */
	TSet<TPair<FObjectKey, FName>>& GetUnfoldedLists()
	{
		static TSet<TPair<FObjectKey, FName>> Unfolded;
		return Unfolded;
	}

	// ---------------------------------------------------------------------------------------------------
	// The grip along an editor's bottom
	// ---------------------------------------------------------------------------------------------------

	DECLARE_DELEGATE_OneParam(FOnGripDragged, float /*DeltaY*/);

	/** A thin bar that makes the editor above it taller or shorter as it is dragged up and down. */
	class SWidgetAnimHeightGrip : public SCompoundWidget
	{
	public:

		SLATE_BEGIN_ARGS(SWidgetAnimHeightGrip) {}
			SLATE_EVENT(FOnGripDragged, OnDragged)
			SLATE_EVENT(FSimpleDelegate, OnDragEnded)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OnDragged = InArgs._OnDragged;
			OnDragEnded = InArgs._OnDragEnded;

			SetToolTipText(WidgetAnimTips::AdditiveLayersResizeTooltip());

			ChildSlot
			[
				SNew(SBox)
				.HeightOverride(7.f)
				.Padding(FMargin(0.f, 2.f))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("WhiteBrush"))
					.ColorAndOpacity_Lambda([this]()
					{
						return FSlateColor(bDragging || IsHovered() ? Palette::InlineEditorGripHovered : Palette::InlineEditorGrip);
					})
				]
			];
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}

			bDragging = true;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (!bDragging || !HasMouseCapture())
			{
				return FReply::Unhandled();
			}

			// The cursor moves in screen pixels; the editor's height is in the Details panel's own units.
			OnDragged.ExecuteIfBound(MouseEvent.GetCursorDelta().Y / FMath::Max(MyGeometry.Scale, 0.01f));
			return FReply::Handled();
		}

		virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (!bDragging)
			{
				return FReply::Unhandled();
			}

			bDragging = false;
			OnDragEnded.ExecuteIfBound();
			return FReply::Handled().ReleaseMouseCapture();
		}

		virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
		{
			if (bDragging)
			{
				bDragging = false;
				OnDragEnded.ExecuteIfBound();
			}
		}

		virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
		{
			return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
		}

	private:

		FOnGripDragged OnDragged;
		FSimpleDelegate OnDragEnded;
		bool bDragging = false;
	};

	// ---------------------------------------------------------------------------------------------------
	// Finding the widget blueprint, and which of its widgets the component is on
	// ---------------------------------------------------------------------------------------------------

	/** Whether a component is a live one on the designer's preview - not a template, and animating a design-time widget. */
	bool IsLiveDesignerComponent(const UWidgetAnimationComponent* Component)
	{
		const UWidget* Animated = Component ? Component->GetAnimatedWidget() : nullptr;
		return Animated && Animated->IsDesignTime() && !Component->IsTemplate();
	}

	/** The Widget Blueprint a component belongs to: the one it is stored in, or - for a live one - the one its preview was made from. */
	UWidgetBlueprint* FindOwningWidgetBlueprint(const UWidgetAnimationComponent* Component)
	{
		if (!Component)
		{
			return nullptr;
		}

		if (UWidgetBlueprint* Stored = Component->GetTypedOuter<UWidgetBlueprint>())
		{
			return Stored;
		}

		const UWidget* Animated = Component->GetAnimatedWidget();
		const UUserWidget* Root = Animated ? Animated->GetTypedOuter<UUserWidget>() : nullptr;
		if (const UUserWidget* AsUserWidget = Cast<UUserWidget>(Animated))
		{
			Root = AsUserWidget;
		}
		return Root ? Cast<UWidgetBlueprint>(UBlueprint::GetBlueprintFromClass(Root->GetClass())) : nullptr;
	}

	/**
	 * The name of the widget selected in the Widget Blueprint's editor - the one whose Details are showing this
	 * component, so the widget the component is on. None when the editor isn't open, or several are selected.
	 */
	FName FindSelectedWidgetName(UWidgetBlueprint* Blueprint)
	{
		UAssetEditorSubsystem* AssetEditors = GEditor && Blueprint ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		IAssetEditorInstance* Instance = AssetEditors ? AssetEditors->FindEditorForAsset(Blueprint, /*bFocusIfOpen=*/false) : nullptr;
		if (!Instance || Instance->GetEditorName() != FName(TEXT("WidgetBlueprintEditor")))
		{
			return NAME_None;
		}

		FWidgetBlueprintEditor* Editor = static_cast<FWidgetBlueprintEditor*>(Instance);
		const TSet<FWidgetReference>& Selected = Editor->GetSelectedWidgets();
		if (Selected.Num() != 1)
		{
			return NAME_None;
		}

		const UWidget* Template = Selected.CreateConstIterator()->GetTemplate();
		return Template ? Template->GetFName() : NAME_None;
	}

	/**
	 * The name of the widget a UI component is kept for, read off whatever holds it: any array of structs on its outer
	 * with one entry holding the component and a name beside it - how UMG's component container pairs each component
	 * with its widget. Found by reflection, so nothing here depends on the container's private layout.
	 */
	FName FindHeldUnderName(const UObject* Held)
	{
		UObject* Holder = Held ? Held->GetOuter() : nullptr;
		if (!Holder)
		{
			return NAME_None;
		}

		for (TFieldIterator<FArrayProperty> It(Holder->GetClass()); It; ++It)
		{
			const FArrayProperty* ArrayProperty = *It;
			const FStructProperty* Inner = CastField<FStructProperty>(ArrayProperty->Inner);
			if (!Inner)
			{
				continue;
			}

			FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Holder));
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				const uint8* Element = Helper.GetRawPtr(Index);
				bool bHoldsIt = false;
				FName Name;
				for (TFieldIterator<FProperty> FieldIt(Inner->Struct); FieldIt; ++FieldIt)
				{
					if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(*FieldIt))
					{
						bHoldsIt |= ObjectProperty->GetObjectPropertyValue_InContainer(Element) == Held;
					}
					else if (const FNameProperty* NameProperty = CastField<FNameProperty>(*FieldIt))
					{
						Name = NameProperty->GetPropertyValue_InContainer(Element);
					}
				}

				if (bHoldsIt)
				{
					return Name;
				}
			}
		}
		return NAME_None;
	}

	/** Whether a component is one stored in an asset - not a live preview, not a class's own copy, not a class default. */
	bool IsStoredComponent(const UWidgetAnimationComponent* Candidate)
	{
		return Candidate
			&& IsValid(Candidate)
			&& Candidate->GetOutermost() != GetTransientPackage()
			&& !Candidate->HasAnyFlags(RF_ClassDefaultObject)
			&& !Candidate->GetTypedOuter<UClass>()
			&& !IsLiveDesignerComponent(Candidate);
	}

	FString DescribeComponent(const UObject* Object)
	{
		if (!Object)
		{
			return TEXT("none");
		}
		return FString::Printf(TEXT("%s (flags 0x%08x%s)"), *Object->GetPathName(), static_cast<uint32>(Object->GetFlags()),
			Object->GetOutermost() == GetTransientPackage() ? TEXT(", transient") : TEXT(""));
	}

	// ---------------------------------------------------------------------------------------------------
	// The source: a component's Additive Anim Layers In or Out
	// ---------------------------------------------------------------------------------------------------

	/**
	 * One of a Widget Animation Component's Additive Anim Layers lists, edited in place on the component these
	 * Details show, and previewed on the widget that component animates in the designer.
	 *
	 * The component in these Details and the one playing in the designer may not be the same object: UMG keeps a
	 * template in the Widget Blueprint, and the designer's preview is an instance made from it - rebuilt freely.
	 * FindPreviewWidget finds the live one; OnPreviewReleased copies what was edited over to it, so the designer's
	 * own preview and the toolbar's Play play the new tracks without waiting for a compile.
	 */
	class FWidgetAnimComponentLayersPanelSource : public FWidgetAnimLayerArrayPanelSource
	{
	public:

		FWidgetAnimComponentLayersPanelSource(UWidgetAnimationComponent* InComponent, FName InPropertyName, bool bInOut)
			: FWidgetAnimLayerArrayPanelSource(InComponent, InPropertyName, bInOut)
			, Component(InComponent)
		{
		}

		virtual ~FWidgetAnimComponentLayersPanelSource() override
		{
			// Here rather than in the base: its OnPreviewReleased is this class's, which is gone by then.
			RestorePreview();
		}

		/** The widget the component was last seen selected on, and its Widget Blueprint - refreshed whenever its Details are laid out. */
		void SetOwnerHint(FName InOwnerWidgetName, UWidgetBlueprint* InBlueprint)
		{
			if (!InOwnerWidgetName.IsNone())
			{
				OwnerWidgetName = InOwnerWidgetName;
			}
			if (InBlueprint)
			{
				Blueprint = InBlueprint;
			}
		}

		FName GetOwnerWidgetName() const { return OwnerWidgetName; }

		/** Called once a list has been handed over to an entry - the Details showing the entries are stale by then. */
		TFunction<void()> OnEntriesChanged;

		virtual bool CanReplaceWithEntry() const override
		{
			return Component.IsValid();
		}

		virtual FText GetReplaceWithEntryLabel() const override
		{
			return PlaysAsOut()
				? LOCTEXT("ReplaceWithOutEntry", "Save and Use as an Anim Out Entry")
				: LOCTEXT("ReplaceWithInEntry", "Save and Use as an Anim In Entry");
		}

		virtual UWidgetAnimPresetAsset* SaveAndReplaceWithEntry() override
		{
			UWidgetAnimPresetAsset* Asset = SaveAsNewAsset();
			UWidgetAnimationComponent* Edited = Component.Get();
			TArray<FWidgetAnimLayer>* Layers = GetLayers();
			if (!Asset || !Edited || !Layers)
			{
				return Asset;
			}

			{
				// Undoable as one step: the entry added, the tracks cleared. The asset itself stays - it is saved to
				// disk, and an undo taking it back out of the Content Browser would be more surprising than useful.
				const FScopedTransaction Transaction(PlaysAsOut()
					? LOCTEXT("ReplaceWithOutEntryTransaction", "Replace Additive Anim Layers Out with an Entry")
					: LOCTEXT("ReplaceWithInEntryTransaction", "Replace Additive Anim Layers In with an Entry"));

				Edited->Modify();

				FWidgetAnimEntry Entry;
				Entry.Animation = Asset;
				(PlaysAsOut() ? Edited->AnimOut : Edited->AnimIn).Add(Entry);
				Layers->Reset();

				// The entry list told as a Details edit of it would be: its Target Widgets sized to the asset's one
				// group, its trigger settings shown. Then the cleared list, which also marks the Blueprint dirty.
				const FName EntriesName = PlaysAsOut()
					? GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, AnimOut)
					: GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, AnimIn);
				FPropertyChangedEvent EntriesEvent(FindFProperty<FProperty>(Edited->GetClass(), EntriesName), EPropertyChangeType::ArrayAdd);
				Edited->PostEditChangeProperty(EntriesEvent);

				bEntriesChanged = true;
				NotifyGroupsChanged(EPropertyChangeType::ArrayClear);
			}

			CopyOntoLiveComponent(GetPreviewWidget(), /*bRefresh=*/true);

			if (OnEntriesChanged)
			{
				OnEntriesChanged();
			}
			return Asset;
		}

	protected:

		/**
		 * The widget the component animates on the designer's preview. The component itself, when these Details show
		 * the live one; else, in order:
		 *
		 *  - a live component made from this one - its archetype chain reaches the component shown here;
		 *  - the component on the widget of that name in the designer's preview - the name of the widget selected in
		 *    the Widget Blueprint editor when these Details were laid out.
		 */
		virtual UWidget* FindPreviewWidget() const override
		{
			const UWidgetAnimationComponent* Edited = Component.Get();
			if (!Edited)
			{
				return nullptr;
			}

			if (IsLiveDesignerComponent(Edited))
			{
				return Edited->GetAnimatedWidget();
			}

			for (TObjectIterator<UWidgetAnimationComponent> It(RF_ClassDefaultObject | RF_ArchetypeObject, /*bIncludeDerivedClasses=*/true, EInternalObjectFlags::Garbage); It; ++It)
			{
				const UWidgetAnimationComponent* Candidate = *It;
				if (Candidate == Edited || !IsLiveDesignerComponent(Candidate))
				{
					continue;
				}

				for (const UObject* Archetype = Candidate->GetArchetype(); Archetype; Archetype = Archetype->GetArchetype())
				{
					if (Archetype == Edited)
					{
						return Candidate->GetAnimatedWidget();
					}
					if (Archetype->HasAnyFlags(RF_ClassDefaultObject))
					{
						break;
					}
				}
			}

			UUserWidget* Root = !OwnerWidgetName.IsNone() ? FindWidgetAnimDesignerPreview(Blueprint.Get()) : nullptr;
			if (!Root)
			{
				return nullptr;
			}

			UWidget* Found = Root->GetFName() == OwnerWidgetName
				? Root
				: (Root->WidgetTree ? Root->WidgetTree->FindWidget(OwnerWidgetName) : nullptr);
			return Found && UWidgetAnimationComponent::FindWidgetAnimationComponent(Found) ? Found : nullptr;
		}

		/**
		 * The live component brought up to date with what these Details hold, and its own designer preview put back
		 * as authored and held there - so only these tracks pose the widget.
		 */
		virtual void OnPreviewTaken(UWidget* Widget) override
		{
			CopyOntoLiveComponent(Widget, /*bRefresh=*/false);
			if (UWidgetAnimationComponent* Live = UWidgetAnimationComponent::FindWidgetAnimationComponent(Widget))
			{
				Live->ResetDesignerPreviewOverride();
			}
		}

		/**
		 * The Target Widget choices the component's own entries offer - listed even from the template, which has no widget
		 * to ask - with their (This Widget) as the None a track's own Target Widget uses for the same thing.
		 */
		virtual TArray<FString> GetLayerTargetOptions() const override
		{
			const UWidgetAnimationComponent* Edited = Component.Get();
			if (!Edited)
			{
				return FWidgetAnimLayerArrayPanelSource::GetLayerTargetOptions();
			}

			TArray<FString> Options = Edited->GetAnimTargetWidgetOptions();
			for (FString& Option : Options)
			{
				if (Option == FWidgetAnimGroupTarget::ThisWidget.ToString())
				{
					Option = TEXT("None");
				}
			}
			return Options;
		}

		// ---------------------------------------------------------------------------------------------
		// When the list starts - see FWidgetAnimAdditiveStart
		// ---------------------------------------------------------------------------------------------

		virtual bool SupportsStartOptions() const override
		{
			return Component.IsValid();
		}

		/** Every entry the list can start within: this direction's own first, then the other list's mirrored into it. */
		virtual void GetStartOptions(TArray<FStartOption>& OutOptions) const override
		{
			const UWidgetAnimationComponent* Edited = Component.Get();
			if (!Edited)
			{
				return;
			}

			for (const bool bInAnimOut : { PlaysAsOut(), !PlaysAsOut() })
			{
				const TArray<FWidgetAnimEntry>& List = bInAnimOut ? Edited->AnimOut : Edited->AnimIn;
				for (int32 Index = 0; Index < List.Num(); ++Index)
				{
					if (!Edited->CanHostAdditiveList(PlaysAsOut(), bInAnimOut, Index))
					{
						continue;
					}

					const FWidgetAnimEntry& Entry = List[Index];
					const FText AssetName = FText::FromString(Entry.Animation->GetName());
					FText Label = Entry.AnimId.IsNone() ? AssetName : FText::Format(LOCTEXT("StartOptionWithId", "{0} ({1})"), AssetName, FText::FromName(Entry.AnimId));
					if (bInAnimOut != PlaysAsOut())
					{
						Label = FText::Format(LOCTEXT("StartOptionMirrored", "{0} - mirrored from {1}"), Label,
							bInAnimOut ? LOCTEXT("AnimOutListName", "Anim Out") : LOCTEXT("AnimInListName", "Anim In"));
					}

					FStartOption& Option = OutOptions.AddDefaulted_GetRef();
					Option.Label = Label;
					Option.ToolTip = FText::Format(WidgetAnimTips::AdditiveStartWithinTooltip(), AssetName);
					Option.Id = MakeStartId(bInAnimOut, Index);
				}
			}
		}

		virtual int32 GetStartOption() const override
		{
			const UWidgetAnimationComponent* Edited = Component.Get();
			bool bInAnimOut = false;
			const int32 Index = Edited ? Edited->ResolveAdditiveStart(PlaysAsOut(), bInAnimOut) : INDEX_NONE;
			return Index == INDEX_NONE ? INDEX_NONE : MakeStartId(bInAnimOut, Index);
		}

		virtual void SetStartOption(int32 Id) override
		{
			UWidgetAnimationComponent* Edited = Component.Get();
			if (!Edited || Id == GetStartOption())
			{
				return;
			}

			const FScopedTransaction Transaction(LOCTEXT("SetAdditiveStartTransaction", "Set When Additive Tracks Start"));
			Edited->Modify();

			FWidgetAnimAdditiveStart& Start = GetStart(*Edited);
			Start.Reset();
			if (Id != INDEX_NONE)
			{
				const bool bInAnimOut = (Id & StartIdOutBit) != 0;
				const int32 Index = Id & ~StartIdOutBit;
				if (Edited->CanHostAdditiveList(PlaysAsOut(), bInAnimOut, Index))
				{
					Start.bInAnimOut = bInAnimOut;
					Start.EntryIndex = Index;
					Start.Animation = (bInAnimOut ? Edited->AnimOut : Edited->AnimIn)[Index].Animation;
				}
			}

			FPropertyChangedEvent Event(FindFProperty<FProperty>(Edited->GetClass(), GetStartPropertyName()), EPropertyChangeType::ValueSet);
			Edited->PostEditChangeProperty(Event);

			// Told as an edit of the list: copied to the saved and live components, and the preview posed again.
			NotifyGroupsChanged(EPropertyChangeType::ValueSet);
		}

		/** The entry's animation, read-only above the list. */
		virtual const TArray<FWidgetAnimLayer>* GetContextLayers() const override
		{
			const FWidgetAnimEntry* Entry = FindStartEntry();
			return Entry && Entry->Animation ? &Entry->Animation->Layers : nullptr;
		}

		/** Its tracks at the speed its animation plays - the entry's own Playback Speed scales the list with it, so it doesn't count. */
		virtual float GetContextTimeScale() const override
		{
			const FWidgetAnimEntry* Entry = FindStartEntry();
			return Entry && Entry->Animation ? 1.f / FMath::Max(Entry->Animation->AnimationSpeedMultiplier, 0.01f) : 1.f;
		}

		virtual bool GetContextInvert() const override
		{
			const FWidgetAnimEntry* Entry = FindStartEntry();
			return Entry && Entry->bInvertAnimation;
		}

		/** The entry's animation in the preview, before the list, on the widgets it targets - as it plays at runtime. */
		virtual void AppendContextSettings(UWidget* Widget, TArray<FWidgetAnimSettings>& OutSettings) const override
		{
			const FWidgetAnimEntry* Entry = FindStartEntry();
			if (!Entry || !Widget)
			{
				return;
			}

			const UWidgetAnimationComponent* Live = UWidgetAnimationComponent::FindWidgetAnimationComponent(Widget);
			const int32 First = OutSettings.Num();
			Entry->BuildSettingsResolved(OutSettings, [this, Live, Widget](FName Path) -> UWidget*
			{
				if (Path.IsNone())
				{
					return nullptr;
				}
				return Live ? Live->ResolveAnimTarget(Path) : ResolveLayerTarget(Widget, Path);
			});

			// At the list's timing - the entry's Playback Speed taken off, as it scales the list too - with the preview's
			// Invert on top of the entry's own, and looping left to the preview's clock.
			const float EntrySpeed = FMath::Max(Entry->PlaybackSpeed, 0.01f);
			for (int32 Index = First; Index < OutSettings.Num(); ++Index)
			{
				FWidgetAnimSettings& Settings = OutSettings[Index];
				Settings.PlaybackSpeed /= EntrySpeed;
				Settings.bInvertAnimation = Settings.bInvertAnimation != GetInvert();
				Settings.bLoop = false;
				Settings.LoopCount = 0;
			}
		}

		/** A track's Target Widget, resolved by the live component exactly as it resolves it at runtime. */
		virtual UWidget* ResolveLayerTarget(UWidget* Widget, FName Path) const override
		{
			if (const UWidgetAnimationComponent* Live = UWidgetAnimationComponent::FindWidgetAnimationComponent(Widget))
			{
				return Path.IsNone() ? nullptr : Live->ResolveAnimTarget(Path);
			}
			return FWidgetAnimLayerArrayPanelSource::ResolveLayerTarget(Widget, Path);
		}

		virtual void OnPreviewReleased(UWidget* Widget, bool bEdited) override
		{
			if (bEdited)
			{
				CopyOntoLiveComponent(Widget, /*bRefresh=*/true);
			}
		}

		/**
		 * Every copy of the list brought into line before the edits are told: the component saved in the Widget
		 * Blueprint - when these Details show some other copy of it - and the live one in the designer. Whichever way
		 * UMG then copies between them (its preview onto its template, as it does for a Details edit), they agree.
		 */
		virtual void PrepareCommit() override
		{
			UWidgetAnimationComponent* Edited = Component.Get();
			if (!Edited)
			{
				return;
			}

			UWidgetAnimationComponent* Stored = FindStoredComponent();
			UWidgetAnimationComponent* Live = GetPreviewWidget() ? UWidgetAnimationComponent::FindWidgetAnimationComponent(GetPreviewWidget()) : nullptr;

			if (!bLoggedWhere)
			{
				bLoggedWhere = true;
				UE_LOG(LogWidgetAnimInlineEditor, Log, TEXT("%s: editing %s; saved component %s; live component %s; widget '%s'."),
					*GetArrayPropertyName().ToString(), *DescribeComponent(Edited), *DescribeComponent(Stored), *DescribeComponent(Live), *OwnerWidgetName.ToString());
			}

			CopyListOnto(Stored);
			if (Live && Live != Edited && Live != Stored)
			{
				CopyListOnto(Live);
			}
			bEntriesChanged = false;

			UE_LOG(LogWidgetAnimInlineEditor, Verbose, TEXT("%s: %d tracks committed."), *GetArrayPropertyName().ToString(), GetLayers() ? GetLayers()->Num() : 0);
		}

		/** Whatever the Details did with the edit, the Widget Blueprint holding the saved component is left dirty and out of date. */
		virtual void FinishCommit(bool bThroughHandle) override
		{
			UWidgetAnimationComponent* Stored = FindStoredComponent();
			UBlueprint* Owning = Stored ? Stored->GetTypedOuter<UBlueprint>() : Blueprint.Get();
			if (Owning)
			{
				Owning->MarkPackageDirty();
				if (Owning->Status == BS_UpToDate || Owning->Status == BS_UpToDateWithWarnings)
				{
					Owning->Status = BS_Dirty;
				}
			}
		}

		virtual FString GetNewAssetBaseName() const override
		{
			// WBP_Card, a component on Title -> WA_Card_Title_In.
			FString BaseName = FWidgetAnimLayerArrayPanelSource::GetNewAssetBaseName();
			BaseName.RemoveFromEnd(TEXT("_Tracks"));
			if (!OwnerWidgetName.IsNone())
			{
				BaseName += TEXT("_") + OwnerWidgetName.ToString();
			}
			BaseName += PlaysAsOut() ? TEXT("_Out") : TEXT("_In");
			return BaseName;
		}

	private:

		/** How a start option's Id tells an Anim Out entry from an Anim In one: this bit set, the entry's index below it. */
		static constexpr int32 StartIdOutBit = 1 << 30;

		static int32 MakeStartId(bool bInAnimOut, int32 Index)
		{
			return (bInAnimOut ? StartIdOutBit : 0) | Index;
		}

		FWidgetAnimAdditiveStart& GetStart(UWidgetAnimationComponent& Target) const
		{
			return PlaysAsOut() ? Target.AdditiveAnimLayersOutStart : Target.AdditiveAnimLayersInStart;
		}

		FName GetStartPropertyName() const
		{
			return PlaysAsOut()
				? GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, AdditiveAnimLayersOutStart)
				: GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, AdditiveAnimLayersInStart);
		}

		/** The entry the list starts within, on the edited component - null when it starts right away, or the entry is gone. */
		const FWidgetAnimEntry* FindStartEntry() const
		{
			const UWidgetAnimationComponent* Edited = Component.Get();
			bool bInAnimOut = false;
			const int32 Index = Edited ? Edited->ResolveAdditiveStart(PlaysAsOut(), bInAnimOut) : INDEX_NONE;
			if (Index == INDEX_NONE)
			{
				return nullptr;
			}

			const TArray<FWidgetAnimEntry>& List = bInAnimOut ? Edited->AnimOut : Edited->AnimIn;
			return List.IsValidIndex(Index) ? &List[Index] : nullptr;
		}

		/**
		 * The component saved in the Widget Blueprint that the edited one stands for: the edited one itself when it is
		 * stored there; else, among the components stored in the Blueprint's package, the one kept for the widget these
		 * Details were laid out for - or the only one there is. Null when none can be told apart.
		 */
		UWidgetAnimationComponent* FindStoredComponent() const
		{
			UWidgetAnimationComponent* Edited = Component.Get();
			if (IsStoredComponent(Edited))
			{
				return Edited;
			}

			if (UWidgetAnimationComponent* Cached = CachedStored.Get())
			{
				if (IsStoredComponent(Cached))
				{
					return Cached;
				}
			}

			const UWidgetBlueprint* WidgetBlueprint = Blueprint.Get();
			if (!WidgetBlueprint)
			{
				return nullptr;
			}

			TArray<UWidgetAnimationComponent*> Candidates;
			ForEachObjectWithPackage(WidgetBlueprint->GetOutermost(), [&Candidates](UObject* Object)
			{
				UWidgetAnimationComponent* Candidate = Cast<UWidgetAnimationComponent>(Object);
				if (Candidate && IsStoredComponent(Candidate))
				{
					Candidates.Add(Candidate);
				}
				return true;
			});

			UWidgetAnimationComponent* Found = nullptr;
			if (Candidates.Num() == 1)
			{
				Found = Candidates[0];
			}
			else if (!OwnerWidgetName.IsNone())
			{
				for (UWidgetAnimationComponent* Candidate : Candidates)
				{
					if (FindHeldUnderName(Candidate) == OwnerWidgetName)
					{
						Found = Candidate;
						break;
					}
				}
			}

			if (!Found && Candidates.Num() > 1)
			{
				UE_LOG(LogWidgetAnimInlineEditor, Warning, TEXT("%s: %d Widget Animation Components are stored in %s and none could be matched to widget '%s'; edits stay on %s."),
					*GetArrayPropertyName().ToString(), Candidates.Num(), *WidgetBlueprint->GetName(), *OwnerWidgetName.ToString(), *DescribeComponent(Edited));
			}

			CachedStored = Found;
			return Found;
		}

		/** This list - and, after a hand-over to an entry, the entries - copied from the edited component onto Target. */
		void CopyListOnto(UWidgetAnimationComponent* Target)
		{
			UWidgetAnimationComponent* Edited = Component.Get();
			if (!Target || !Edited || Target == Edited)
			{
				return;
			}

			// Outside any transaction: undo steps are the edits themselves, on the edited component. After an undo the
			// next commit copies the undone list across just the same.
			Target->Modify();
			if (PlaysAsOut())
			{
				Target->AdditiveAnimLayersOut = Edited->AdditiveAnimLayersOut;
				Target->AdditiveAnimLayersOutStart = Edited->AdditiveAnimLayersOutStart;
			}
			else
			{
				Target->AdditiveAnimLayersIn = Edited->AdditiveAnimLayersIn;
				Target->AdditiveAnimLayersInStart = Edited->AdditiveAnimLayersInStart;
			}

			if (bEntriesChanged)
			{
				Target->AnimIn = Edited->AnimIn;
				Target->AnimOut = Edited->AnimOut;
			}

			FProperty* ListProperty = FindFProperty<FProperty>(Target->GetClass(), GetArrayPropertyName());
			FPropertyChangedEvent Event(ListProperty, EPropertyChangeType::ValueSet);
			Target->PostEditChangeProperty(Event);
		}

		/**
		 * What these Details edit, copied onto the live component on Widget when that is another object - its lists
		 * and entries - and, with bRefresh, its preview rebuilt from them. Once this source has taken the widget over,
		 * the live component stays held as authored until played again, or rebuilt by the designer.
		 */
		void CopyOntoLiveComponent(UWidget* Widget, bool bRefresh) const
		{
			const UWidgetAnimationComponent* Edited = Component.Get();
			UWidgetAnimationComponent* Live = Widget ? UWidgetAnimationComponent::FindWidgetAnimationComponent(Widget) : nullptr;
			if (!Edited || !Live || !IsLiveDesignerComponent(Live))
			{
				return;
			}

			if (Live != Edited)
			{
				Live->AdditiveAnimLayersIn = Edited->AdditiveAnimLayersIn;
				Live->AdditiveAnimLayersOut = Edited->AdditiveAnimLayersOut;
				Live->AdditiveAnimLayersInStart = Edited->AdditiveAnimLayersInStart;
				Live->AdditiveAnimLayersOutStart = Edited->AdditiveAnimLayersOutStart;
				Live->AnimIn = Edited->AnimIn;
				Live->AnimOut = Edited->AnimOut;
			}

			if (bRefresh)
			{
				Live->NotifyOwnerDesignTimeRefresh();
			}
		}

		TWeakObjectPtr<UWidgetAnimationComponent> Component;
		FName OwnerWidgetName;
		TWeakObjectPtr<UWidgetBlueprint> Blueprint;

		/** The saved component found last - see FindStoredComponent. */
		mutable TWeakObjectPtr<UWidgetAnimationComponent> CachedStored;

		/** A hand-over to an entry changed the entries too, not yet copied to the saved component. */
		bool bEntriesChanged = false;

		/** Where the edits go has been told to the log once. */
		bool bLoggedWhere = false;
	};

	/**
	 * One source per list, shared by its editor in the Details and its Expand window - so both show the same
	 * playhead. Kept for as long as the component exists, not just while a panel shows it: telling an edit through
	 * the Details makes them lay themselves out again, and the new panel picks up the same source - its playhead,
	 * its Loop and Speed, and (see SWidgetAnimTimeline::SaveCompactState) what was selected.
	 */
	TSharedRef<FWidgetAnimComponentLayersPanelSource> FindOrMakeSource(UWidgetAnimationComponent* Component, FName PropertyName, bool bOut)
	{
		static TMap<TPair<FObjectKey, FName>, TSharedPtr<FWidgetAnimComponentLayersPanelSource>> Sources;

		for (auto It = Sources.CreateIterator(); It; ++It)
		{
			if (!It.Value().IsValid() || !It.Value()->GetOwner())
			{
				It.RemoveCurrent();
			}
		}

		const TPair<FObjectKey, FName> Key(FObjectKey(Component), PropertyName);
		if (const TSharedPtr<FWidgetAnimComponentLayersPanelSource>* Existing = Sources.Find(Key))
		{
			return Existing->ToSharedRef();
		}

		const TSharedRef<FWidgetAnimComponentLayersPanelSource> Made = MakeShared<FWidgetAnimComponentLayersPanelSource>(Component, PropertyName, bOut);
		Sources.Add(Key, Made);
		return Made;
	}

	/** Each list's Expand window, while open - pressing Expand again brings it to the front rather than opening another. */
	TMap<TPair<FObjectKey, FName>, TWeakPtr<SWindow>>& GetExpandWindows()
	{
		static TMap<TPair<FObjectKey, FName>, TWeakPtr<SWindow>> Windows;
		return Windows;
	}

	void OpenExpandWindow(const TSharedRef<FWidgetAnimComponentLayersPanelSource>& Source, const FText& ListName, bool bOut)
	{
		UObject* Owner = Source->GetOwner();
		if (!Owner)
		{
			return;
		}

		const TPair<FObjectKey, FName> Key(FObjectKey(Owner), Source->GetArrayPropertyName());
		if (const TSharedPtr<SWindow> Open = GetExpandWindows().FindRef(Key).Pin())
		{
			Open->BringToFront();
			return;
		}

		// Named after the list and the widget it is on, so two open windows can be told apart.
		const FName OwnerWidgetName = Source->GetOwnerWidgetName();
		const FText Where = OwnerWidgetName.IsNone() ? FText::FromString(Owner->GetName()) : FText::FromName(OwnerWidgetName);
		const FText Title = FText::Format(WidgetAnimTips::AdditiveLayersWindowTitle(), ListName, Where);

		const TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(Title)
			.ClientSize(FVector2D(1100.0, 520.0))
			.SupportsMaximize(true)
			.SupportsMinimize(true)
			[
				// The window holds the source, so the list keeps its playhead even once the Details move on.
				SNew(SWidgetAnimTimeline)
				.Source(Source)
				.Compact(true)
				.Title(ListName)
				.TitleToolTip(bOut ? WidgetAnimTips::AdditiveLayersOutEditorTooltip() : WidgetAnimTips::AdditiveLayersInEditorTooltip())
			];

		GetExpandWindows().Add(Key, Window);

		if (const TSharedPtr<SWindow> Root = FGlobalTabmanager::Get()->GetRootWindow())
		{
			FSlateApplication::Get().AddWindowAsNativeChild(Window, Root.ToSharedRef());
		}
		else
		{
			FSlateApplication::Get().AddWindow(Window);
		}
	}
}

// =====================================================================================================
// FWidgetAnimationComponentDetails
// =====================================================================================================

TSharedRef<IDetailCustomization> FWidgetAnimationComponentDetails::MakeInstance()
{
	return MakeShared<FWidgetAnimationComponentDetails>();
}

void FWidgetAnimationComponentDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	WeakDetailBuilder = DetailBuilder;
	if (DetailBuilder.IsValid())
	{
		CustomizeDetails(*DetailBuilder);
	}
}

void FWidgetAnimationComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	// Every Animation setting added here, in the order it is declared, so the two lists' editors sit right under
	// the lists they edit - a category shows what is added to it before what is left to it.
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Animation"));

	Category.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, bPlayOnlyOnConstructAndDestruct)));
	Category.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, AnimIn)));
	Category.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, AnimOut)));

	AddListEditor(DetailBuilder, Category, GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, AdditiveAnimLayersIn), /*bOut=*/false, Objects);
	AddListEditor(DetailBuilder, Category, GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, AdditiveAnimLayersOut), /*bOut=*/true, Objects);

	// The Value Names its value-driven tracks use, listed by the component itself - shown only when there are some.
	const TSharedRef<IPropertyHandle> ValuesHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, ValueOverrides));
	uint32 NumValues = 0;
	const TSharedPtr<IPropertyHandleArray> ValuesArray = ValuesHandle->AsArray();
	const bool bKnownCount = ValuesArray.IsValid() && ValuesArray->GetNumElements(NumValues) == FPropertyAccess::Success;
	if (bKnownCount && NumValues == 0)
	{
		DetailBuilder.HideProperty(ValuesHandle);
	}
	else
	{
		Category.AddProperty(ValuesHandle);
	}

	Category.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, bPreserveAuthoredTransforms)));
}

void FWidgetAnimationComponentDetails::AddListEditor(IDetailLayoutBuilder& DetailBuilder, IDetailCategoryBuilder& Category, FName PropertyName, bool bOut,
	const TArray<TWeakObjectPtr<UObject>>& Objects)
{
	const TSharedRef<IPropertyHandle> Handle = DetailBuilder.GetProperty(PropertyName);
	const FText ListName = Handle->GetPropertyDisplayName();
	const FText ListToolTip = bOut ? WidgetAnimTips::AdditiveLayersOutEditorTooltip() : WidgetAnimTips::AdditiveLayersInEditorTooltip();

	// Several components at once: the editor edits one list, so they keep their plain fields where they were.
	UWidgetAnimationComponent* Component = Objects.Num() == 1 ? Cast<UWidgetAnimationComponent>(Objects[0].Get()) : nullptr;
	if (!Component)
	{
		Category.AddProperty(Handle);
		if (Objects.Num() > 1)
		{
			Category.AddCustomRow(ListName)
				.WholeRowContent()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(IDetailLayoutBuilder::GetDetailFontItalic())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(WidgetAnimTips::AdditiveLayersMultipleObjects())
				];
		}
		return;
	}

	const TSharedRef<FWidgetAnimComponentLayersPanelSource> Source = FindOrMakeSource(Component, PropertyName, bOut);
	UWidgetBlueprint* WidgetBlueprint = FindOwningWidgetBlueprint(Component);
	Source->SetOwnerHint(FindSelectedWidgetName(WidgetBlueprint), WidgetBlueprint);

	// Settled edits are told through this row, as an edit of the plain fields would be - see SetArrayHandle.
	Source->SetArrayHandle(Handle);

	const TPair<FObjectKey, FName> ListKey(FObjectKey(Component), PropertyName);
	auto IsFolded = [ListKey]() { return !GetUnfoldedLists().Contains(ListKey); };

	// The box below, once made - folding changes its height, which it is told of.
	const TSharedRef<TWeakPtr<SBox>> BoxHolder = MakeShared<TWeakPtr<SBox>>();

	// Handing the list over to an entry adds a row to Anim In or Anim Out, which these Details only show once laid
	// out again - on the next tick, never under the button that did it.
	const TWeakPtr<IDetailLayoutBuilder> WeakBuilder = WeakDetailBuilder;
	Source->OnEntriesChanged = [WeakBuilder]()
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakBuilder](float)
		{
			if (const TSharedPtr<IDetailLayoutBuilder> Builder = WeakBuilder.Pin())
			{
				Builder->ForceRefreshDetails();
			}
			return false;
		}));
	};

	// The list's name and how many tracks it has, as its row; the editor right under it.
	const TWeakPtr<FWidgetAnimComponentLayersPanelSource> WeakSource = Source;
	const TAttribute<FText> Title = TAttribute<FText>::CreateLambda([WeakSource, ListName]()
	{
		const TSharedPtr<FWidgetAnimComponentLayersPanelSource> Pinned = WeakSource.Pin();
		const TArray<FWidgetAnimLayer>* Layers = Pinned.IsValid() ? Pinned->GetLayers() : nullptr;
		if (!Layers)
		{
			return ListName;
		}
		return FText::Format(LOCTEXT("ListTitleWithCount", "{0}  ({1} {1}|plural(one=track,other=tracks))"), ListName, Layers->Num());
	});

	// Made first, on its own, so the grip below and the fold arrow can hold on to it. Folded, it is as tall as its
	// header strip; unfolded, as tall as it was dragged to be.
	const TSharedRef<SBox> EditorBox = SNew(SBox)
		.HeightOverride_Lambda([bOut, IsFolded]() { return IsFolded() ? FOptionalSize() : FOptionalSize(EditorHeight(bOut)); })
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(Palette::InlineEditorBorder)
			.Padding(FMargin(1.f))
			[
				SNew(SWidgetAnimTimeline)
				.Source(Source)
				.Compact(true)
				.Inline(true)
				.IsCollapsed_Lambda(IsFolded)
				.OnToggleCollapsed(FSimpleDelegate::CreateLambda([ListKey, BoxHolder]()
				{
					TSet<TPair<FObjectKey, FName>>& Unfolded = GetUnfoldedLists();
					if (Unfolded.Contains(ListKey))
					{
						Unfolded.Remove(ListKey);
					}
					else
					{
						Unfolded.Add(ListKey);
					}

					if (const TSharedPtr<SBox> Box = BoxHolder->Pin())
					{
						Box->Invalidate(EInvalidateWidgetReason::Layout);
					}
				}))
				.Title(Title)
				.TitleToolTip(ListToolTip)
				.OnExpand(FSimpleDelegate::CreateLambda([WeakSource, ListName, bOut]()
				{
					if (const TSharedPtr<FWidgetAnimComponentLayersPanelSource> Pinned = WeakSource.Pin())
					{
						OpenExpandWindow(Pinned.ToSharedRef(), ListName, bOut);
					}
				}))
			]
		];

	const TWeakPtr<SBox> WeakBox = EditorBox;
	*BoxHolder = EditorBox;
	const TSharedRef<SWidget> Editor = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 2.f, 0.f, 0.f))
		[
			EditorBox
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SWidgetAnimHeightGrip)
			.Visibility_Lambda([IsFolded]() { return IsFolded() ? EVisibility::Collapsed : EVisibility::Visible; })
			.OnDragged(FOnGripDragged::CreateLambda([bOut, WeakBox](float DeltaY)
			{
				float& Height = EditorHeight(bOut);
				Height = FMath::Clamp(Height + DeltaY, MinEditorHeight, MaxEditorHeight);
				if (const TSharedPtr<SBox> Box = WeakBox.Pin())
				{
					Box->Invalidate(EInvalidateWidgetReason::Layout);
				}
			}))
			.OnDragEnded(FSimpleDelegate::CreateLambda([bOut]() { SaveEditorHeight(bOut); }))
		];

	Category.AddCustomRow(ListName)
		.WholeRowContent()
		.HAlign(HAlign_Fill)
		[
			Editor
		];

	// The plain fields stay reachable - for a value the editor doesn't show, or a copy and paste of the whole list.
	Category.AddProperty(Handle, EPropertyLocation::Advanced)
		.ToolTip(WidgetAnimTips::AdditiveLayersRawFieldsNote());
}

#undef LOCTEXT_NAMESPACE