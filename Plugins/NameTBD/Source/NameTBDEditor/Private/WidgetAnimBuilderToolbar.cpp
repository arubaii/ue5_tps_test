// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimBuilderToolbar.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBDColors.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/SWindow.h"
#include "SWidgetAnimTimeline.h"
#include "WidgetAnimPanelSource.h"
#include "StatusBarSubsystem.h"
#include "Styling/AppStyle.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "Toolkits/IToolkitHost.h"
#include "ToolMenus.h"
#include "UMGEditorModule.h"
#include "BlueprintModes/WidgetBlueprintApplicationMode.h"
#include "BlueprintModes/WidgetBlueprintApplicationModes.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "WidgetDrawerConfig.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/UObjectIterator.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

#define LOCTEXT_NAMESPACE "WidgetAnimBuilderToolbar"

namespace
{
	/**
	 * The first of Candidates the app style actually has a brush for.
	 *
	 * Icon names move between engine versions - Icons.Pause is absent in 5.7 and logs as missing, which is
	 * what left the button blank - and GetBrush on a name that isn't there draws nothing. GetOptionalBrush
	 * with a null default answers the question without the warning, and the answer is settled once.
	 */
	const FSlateBrush* PickBrush(std::initializer_list<FName> Candidates)
	{
		const ISlateStyle& Style = FAppStyle::Get();

		for (const FName Candidate : Candidates)
		{
			if (const FSlateBrush* Brush = Style.GetOptionalBrush(Candidate, nullptr, nullptr))
			{
				return Brush;
			}
		}

		return nullptr;
	}

	// ---------------------------------------------------------------------------------------------
	// Widget Animations drawer, and the tab it docks into
	//
	// A drawer on every Widget Blueprint editor's status bar, beside Animations, holding SWidgetAnimTimeline:
	// layer groups, their layers, tracks and keys, and a Selection panel for whatever is picked. Dock in
	// Layout opens the same panel as a tab in the editor's layout instead.
	//
	// In a WidgetAnimationBuilder it edits the builder's working copy, for generic animations saved with
	// Create. In any other Widget Blueprint it edits a WA_ asset directly, each Layer Group playing on one of
	// this widget's own widgets - see IWidgetAnimPanelSource.
	//
	// Either way it edits in a details view of its own. Editing through the designer's Details panel edits
	// the designer's preview instance, which UMG rebuilds - and the whole Details panel with it - on every
	// committed edit; UMG never hears about an edit made in the panel, so nothing flickers.
	// ---------------------------------------------------------------------------------------------

	/** The drawer's id on the status bar. One per editor window; every builder editor uses the same one. */
	const FName WidgetAnimationsDrawerId(TEXT("WidgetAnimBuilder.WidgetAnimations"));

	/**
	 * The panel's tab: Window > Widget Animations, what Dock in Layout opens, and what the layout restores.
	 *
	 * Registered two ways. The Widget Blueprint editor's own modes register it, through UMG's tab hook, so it is
	 * there before the layout is restored - and that editor's layout carries a closed slot for it right beside
	 * the native Animations tab, through UMG's layout hook: opening it restores it there, in the tab well along
	 * the bottom of the editor, exactly where the native Animations panel's Dock in Layout puts that one. And,
	 * should a mode ever not register it, EnsureTabSpawner registers it by hand.
	 */
	//
	// A new id, rather than the one earlier versions used: a layout saved while the tab had been opened
	// somewhere else - floating, or in another tab well - keeps that place over the slot below, and the old id
	// is in exactly those saved layouts.
	const FName WidgetAnimationsTabId(TEXT("WidgetAnimBuilder.WidgetAnimationsPanel"));

	/**
	 * The native Animations tab - what the panel's tab is docked beside: the tab well along the bottom of the
	 * Designer, across the editor's whole width. Only the one id, so the layout never holds two slots for the
	 * panel and restores it into the wrong one.
	 */
	const FName NativeAnimationsTabIds[] = { TEXT("Animations") };

	/**
	 * Where the panel goes by hand when the layout has no slot for it: beside the first of these the editor has
	 * - the native Animations tab under the names it has gone by, then the bottom well's other tabs.
	 */
	const FName DockBesideTabIds[] = { TEXT("Animations"), TEXT("WidgetAnimSequencer"), TEXT("CompilerResults") };

	/** The id a panel placed by hand is given. Deliberately without a spawner: a tab can only be put beside another under such an id. */
	const FName WidgetAnimationsDockedTabId(TEXT("WidgetAnimBuilder.WidgetAnimationsDocked"));

	/** Where the drawer goes: after Content Drawer (0) and Animations (1), so ahead of Output Log. */
	constexpr int32 WidgetAnimationsDrawerSlot = 2;

	/**
	 * One editor window's Widget Animations: what the panel edits, and the drawer's panel once built - kept for
	 * the editor's life, so the drawer can be put back on the status bar as often as it needs to be without
	 * losing anything.
	 */
	struct FEditorWidgetAnimations
	{
		TWeakPtr<FAssetEditorToolkit> Toolkit;
		TSharedPtr<IWidgetAnimPanelSource> PanelSource;
		TSharedRef<TSharedPtr<SWidget>> DrawerContent = MakeShared<TSharedPtr<SWidget>>();
	};

	TArray<FEditorWidgetAnimations>& GetEditorsWithDrawer()
	{
		static TArray<FEditorWidgetAnimations> Editors;
		return Editors;
	}

	/** The Widget Blueprint an editor window is editing, builder or not. */
	UWidgetBlueprint* GetEditedWidgetBlueprint(const FAssetEditorToolkit& Toolkit)
	{
		const TArray<UObject*>* EditedObjects = Toolkit.GetObjectsCurrentlyBeingEdited();
		if (!EditedObjects)
		{
			return nullptr;
		}

		for (UObject* EditedObject : *EditedObjects)
		{
			if (UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(EditedObject))
			{
				return WidgetBlueprint;
			}
		}

		return nullptr;
	}

	/**
	 * What the panel edits in this editor: a builder's working copy, or - in any other Widget Blueprint - a
	 * WA_ asset played on this widget. One per editor, shared by its drawer and its docked tab, so both show
	 * the same asset and the same playhead.
	 */
	TSharedRef<IWidgetAnimPanelSource> MakePanelSource(UWidgetBlueprint* WidgetBlueprint)
	{
		const bool bIsBuilder = WidgetBlueprint
			&& WidgetBlueprint->GeneratedClass
			&& WidgetBlueprint->GeneratedClass->IsChildOf(UWidgetAnimBuilder::StaticClass());

		if (bIsBuilder)
		{
			return MakeShared<FWidgetAnimBuilderPanelSource>(WidgetBlueprint);
		}

		return MakeShared<FWidgetAnimAssetPanelSource>(WidgetBlueprint);
	}

	/** An editor's entry - made the first time anything asks for it, by the drawer or by the tab. */
	FEditorWidgetAnimations& FindOrAddEditor(const TSharedRef<FAssetEditorToolkit>& Toolkit)
	{
		TArray<FEditorWidgetAnimations>& Editors = GetEditorsWithDrawer();
		Editors.RemoveAll([](const FEditorWidgetAnimations& Entry) { return !Entry.Toolkit.IsValid(); });

		FEditorWidgetAnimations* Entry = Editors.FindByPredicate([&Toolkit](const FEditorWidgetAnimations& Candidate)
		{
			return Candidate.Toolkit.Pin() == Toolkit;
		});
		if (!Entry)
		{
			Entry = &Editors.AddDefaulted_GetRef();
			Entry->Toolkit = Toolkit;
		}
		return *Entry;
	}

	/** An editor's panel source, made on first use - shared by its drawer and its tab. */
	TSharedRef<IWidgetAnimPanelSource> GetPanelSource(const TSharedRef<FAssetEditorToolkit>& Toolkit)
	{
		FEditorWidgetAnimations& Entry = FindOrAddEditor(Toolkit);
		if (!Entry.PanelSource.IsValid())
		{
			Entry.PanelSource = MakePanelSource(GetEditedWidgetBlueprint(*Toolkit));
		}
		return Entry.PanelSource.ToSharedRef();
	}

	/** The panel as a tab of the editor's layout. */
	TSharedRef<SDockTab> MakeWidgetAnimationsTab(const TSharedRef<IWidgetAnimPanelSource>& PanelSource)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::PanelTab)
			.Label(LOCTEXT("WidgetAnimationsTab", "Widget Animations"))
			[
				SNew(SWidgetAnimTimeline)
				.Source(PanelSource)
			];
	}

	/**
	 * The panel's tab as one of the Widget Blueprint editor's own - registered by its modes, so the tab exists
	 * whenever the layout is restored, and opens in the slot the layout extension gave it.
	 */
	class FWidgetAnimationsTabSummoner : public FWorkflowTabFactory
	{
	public:

		explicit FWidgetAnimationsTabSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp)
			: FWorkflowTabFactory(WidgetAnimationsTabId, InHostingApp)
		{
			TabLabel = LOCTEXT("WidgetAnimationsTab", "Widget Animations");
			TabIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details");
			bIsSingleton = true;
			ViewMenuDescription = LOCTEXT("WidgetAnimationsTabName", "Widget Animations");
			ViewMenuTooltip = LOCTEXT("WidgetAnimationsTabTooltip", "Build the widget animation: its tracks, keys and preview settings.");
		}

		virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override
		{
			const TSharedPtr<FAssetEditorToolkit> Toolkit = HostingApp.Pin();
			if (!Toolkit.IsValid())
			{
				return SNullWidget::NullWidget;
			}

			return SNew(SWidgetAnimTimeline)
				.Source(GetPanelSource(Toolkit.ToSharedRef()));
		}
	};

	/** Registers the panel's tab spawner on an editor's tab manager by hand, unless its mode already has. */
	void EnsureTabSpawner(const TSharedPtr<FTabManager>& TabManager, const TSharedRef<IWidgetAnimPanelSource>& PanelSource)
	{
		if (!TabManager.IsValid() || TabManager->HasTabSpawner(WidgetAnimationsTabId))
		{
			return;
		}

		const FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details");
		TabManager->RegisterTabSpawner(WidgetAnimationsTabId, FOnSpawnTab::CreateLambda([PanelSource](const FSpawnTabArgs&)
		{
			return MakeWidgetAnimationsTab(PanelSource);
		}))
		.SetDisplayName(LOCTEXT("WidgetAnimationsTabName", "Widget Animations"))
		.SetTooltipText(LOCTEXT("WidgetAnimationsTabTooltip", "Build the widget animation: its tracks, keys and preview settings."))
		.SetIcon(Icon);
	}

	/**
	 * Docks the panel where the native Animations panel's Dock in Layout puts that one: in its tab well, along
	 * the bottom of the editor.
	 *
	 * That is the slot the layout extension gave the panel's tab beside the native Animations tab, so opening
	 * the tab restores it there - or, once it has been docked and dragged somewhere else, wherever the layout
	 * last had it. Already open, it is brought forward.
	 *
	 * The drawer is dismissed first, as the native Dock in Layout dismisses its own. It slides up over the very
	 * place the tab opens, so a tab docked behind an open drawer looks as if nothing happened at all.
	 */
	void DockInLayout(TWeakPtr<FAssetEditorToolkit> WeakToolkit, TSharedRef<IWidgetAnimPanelSource> PanelSource)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
		const TSharedPtr<FTabManager> TabManager = Toolkit.IsValid() ? Toolkit->GetTabManager() : nullptr;
		if (!TabManager.IsValid())
		{
			return;
		}

		if (UStatusBarSubsystem* StatusBars = GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr)
		{
			StatusBars->ForceDismissDrawer();
		}

		EnsureTabSpawner(TabManager, PanelSource);

		auto BringForward = [&TabManager](const TSharedRef<SDockTab>& Tab)
		{
			Tab->ActivateInParent(ETabActivationCause::SetDirectly);
			TabManager->DrawAttention(Tab);
		};

		// Open already - docked by this, or put somewhere by hand: there.
		for (const FName OpenId : { WidgetAnimationsTabId, WidgetAnimationsDockedTabId })
		{
			if (const TSharedPtr<SDockTab> Live = TabManager->FindExistingLiveTab(FTabId(OpenId)))
			{
				BringForward(Live.ToSharedRef());
				return;
			}
		}

		// Whether a tab ended up in the editor's own window rather than a floating one of its own.
		const TSharedPtr<SDockTab> OwnerTab = TabManager->GetOwnerTab();
		const TSharedPtr<SWindow> EditorWindow = OwnerTab.IsValid() ? OwnerTab->GetParentWindow() : nullptr;
		auto IsInEditorWindow = [&EditorWindow](const TSharedPtr<SDockTab>& Tab)
		{
			return Tab.IsValid() && EditorWindow.IsValid() && Tab->GetParentWindow() == EditorWindow;
		};

		// The slot the layout extension gave it beside the native Animations tab. If the layout has no such slot
		// - the native tab goes by another name in this engine version - the tab opens floating instead; that is
		// taken back, and the tab is placed by hand below.
		const TSharedPtr<SDockTab> Opened = TabManager->TryInvokeTab(FTabId(WidgetAnimationsTabId));
		if (IsInEditorWindow(Opened))
		{
			BringForward(Opened.ToSharedRef());
			return;
		}
		if (Opened.IsValid())
		{
			Opened->RequestCloseTab();
		}

		// By hand: straight into the native Animations tab's well - opened just long enough to be found, if it
		// isn't open - under a spawner-less document id, which is the only way a tab can be put next to another.
		for (const FName NativeTabId : DockBesideTabIds)
		{
			TSharedPtr<SDockTab> NativeTab = TabManager->FindExistingLiveTab(FTabId(NativeTabId));
			const bool bNativeWasOpen = NativeTab.IsValid();
			if (!bNativeWasOpen && TabManager->HasTabSpawner(NativeTabId))
			{
				NativeTab = TabManager->TryInvokeTab(FTabId(NativeTabId));
			}
			if (!IsInEditorWindow(NativeTab))
			{
				continue;
			}

			const TSharedRef<SDockTab> PanelTab = MakeWidgetAnimationsTab(PanelSource);
			TabManager->InsertNewDocumentTab(WidgetAnimationsDockedTabId, FTabManager::FLiveTabSearch(NativeTabId), PanelTab);

			if (!bNativeWasOpen)
			{
				NativeTab->RequestCloseTab();
			}
			BringForward(PanelTab);
			return;
		}

		// Nowhere to dock it: floating it is, and the log says why.
		UE_LOG(LogTemp, Warning, TEXT("Widget Animations: couldn't find the native Animations tab to dock beside, so the panel opened in its own window. Drag it into place once and the layout keeps it there."));
		if (const TSharedPtr<SDockTab> Floating = TabManager->TryInvokeTab(FTabId(WidgetAnimationsTabId)))
		{
			BringForward(Floating.ToSharedRef());
		}
	}

	// Handles on UMG's hooks, so Unregister can take them off again.
	FDelegateHandle RegisterTabsHandle;
	FDelegateHandle RegisterLayoutHandle;

	/** Puts this editor's Widget Animations drawer on its status bar - replacing it if it is already there. */
	void PlaceDrawer(const TSharedRef<FAssetEditorToolkit>& Toolkit, const FEditorWidgetAnimations& Entry)
	{
		UStatusBarSubsystem* StatusBars = GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr;
		if (!StatusBars || !Toolkit->IsHosted() || !Entry.PanelSource.IsValid())
		{
			return;
		}

		const TSharedRef<IWidgetAnimPanelSource> PanelSource = Entry.PanelSource.ToSharedRef();
		const TSharedRef<TSharedPtr<SWidget>> DrawerContent = Entry.DrawerContent;
		const TWeakPtr<FAssetEditorToolkit> WeakToolkit(Toolkit);

		FWidgetDrawerConfig Config(WidgetAnimationsDrawerId);
		Config.ButtonText = LOCTEXT("WidgetAnimationsDrawer", "Widget Animations");
		Config.ToolTipText = LOCTEXT("WidgetAnimationsDrawerTooltip", "Build widget animations: layer groups for each widget they move, their tracks and keys.");
		Config.Icon = PickBrush({ "LevelEditor.Tabs.Details", "Icons.Details", "UMGEditor.AnimTabIcon" });

		// The panel is built on first open and kept, so its selection and zoom survive the drawer closing -
		// and the drawer being taken down and put back.
		Config.GetDrawerContentDelegate.BindLambda([DrawerContent, PanelSource, WeakToolkit]() -> TSharedRef<SWidget>
		{
			if (!DrawerContent->IsValid())
			{
				*DrawerContent = SNew(SWidgetAnimTimeline)
					.Source(PanelSource)
					.OnDockInLayout(FSimpleDelegate::CreateLambda([WeakToolkit, PanelSource]() { DockInLayout(WeakToolkit, PanelSource); }));
			}
			return DrawerContent->ToSharedRef();
		});

		// Taken down first, so putting it back never doubles it; a status bar without it simply ignores this.
		const FName StatusBarName = Toolkit->GetToolkitHost()->GetStatusBarName();
		StatusBars->UnregisterDrawer(StatusBarName, WidgetAnimationsDrawerId);
		StatusBars->RegisterDrawer(StatusBarName, MoveTemp(Config), WidgetAnimationsDrawerSlot);
	}

	/**
	 * Adds the Widget Animations drawer to this editor's status bar, and makes sure its tab can be opened.
	 *
	 * The panel's source is made once per editor. The drawer is put back every time the toolbar is rebuilt,
	 * which happens on every switch between the Designer and the Graph: the Widget Blueprint editor rebuilds its
	 * status bar for each mode, and a rebuilt status bar has only the drawers the editor itself registers -
	 * which is what made the drawer vanish after a trip to the Graph.
	 */
	void RegisterWidgetAnimationsDrawer(const FToolMenuSection& Section)
	{
		const UAssetEditorToolkitMenuContext* Context = Section.FindContext<UAssetEditorToolkitMenuContext>();
		const TSharedPtr<FAssetEditorToolkit> Toolkit = Context ? Context->Toolkit.Pin() : nullptr;
		if (!Toolkit.IsValid())
		{
			return;
		}

		FindOrAddEditor(Toolkit.ToSharedRef());

		// On the next tick rather than now. The toolbar can be built while the editor window - or, after a
		// mode switch, its new status bar - is still being assembled, and a drawer registered before its
		// status bar exists is dropped silently.
		const TWeakPtr<FAssetEditorToolkit> WeakToolkit(Toolkit);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakToolkit](float) -> bool
		{
			const TSharedPtr<FAssetEditorToolkit> LiveToolkit = WeakToolkit.Pin();
			if (!LiveToolkit.IsValid() || !LiveToolkit->IsHosted())
			{
				return false;
			}

			FEditorWidgetAnimations* Entry = GetEditorsWithDrawer().FindByPredicate([&LiveToolkit](const FEditorWidgetAnimations& Candidate)
			{
				return Candidate.Toolkit.Pin() == LiveToolkit;
			});
			if (!Entry)
			{
				return false;
			}

			// Once per editor: what the panel edits - shared by the drawer and the docked tab, so both show the
			// same animation and playhead.
			const TSharedRef<IWidgetAnimPanelSource> PanelSource = GetPanelSource(LiveToolkit.ToSharedRef());

			// Normally the editor's mode has registered the tab already (see FWidgetAnimationsTabSummoner); every
			// time rather than once in case it hasn't, since switching between the Designer and the Graph clears
			// every spawner on the editor's tab manager.
			EnsureTabSpawner(LiveToolkit->GetTabManager(), PanelSource);

			PlaceDrawer(LiveToolkit.ToSharedRef(), *Entry);
			return false; // one shot
		}));
	}

	// ---------------------------------------------------------------------------------------------
	// The selected widget's animations
	//
	// In an ordinary Widget Blueprint, selecting a widget that has animations - a Widget Animation Component
	// on it, or on a widget inside it, as on a placed widget of your own - brings up Play In, Play Out, Reset,
	// the timer, Loop, Ping Pong, Invert, Speed and a scrubber in the toolbar, for a quick look at how that
	// widget moves where it has been put. Select anything without animations, or nothing, and they go away.
	//
	// They drive that widget's components through each component's own designer preview - the same player
	// its Designer Preview setting drives - so what plays is exactly what that widget does, with the
	// toolbar's Loop / Ping Pong / Invert / Speed standing in for its entries' own. Only the selection plays;
	// the rest of the designer is left alone.
	// ---------------------------------------------------------------------------------------------

	const FLinearColor& PlayTint = NameTBD::Colors::WidgetAnimEditor::PlayTint;
	const FLinearColor& PauseTint = NameTBD::Colors::WidgetAnimEditor::PauseTint;
	const FName WidgetBlueprintEditorName(TEXT("WidgetBlueprintEditor"));

	/** The components animating the widget selected in one Widget Blueprint editor, and the toolbar's choices for them. */
	class FSelectedWidgetAnimations
	{
	public:

		explicit FSelectedWidgetAnimations(TWeakPtr<FWidgetBlueprintEditor> InEditor)
			: Editor(InEditor)
		{
		}

		/** Whether the selected widget has animations to play - which is whether the controls show. */
		bool HasAnimations() const
		{
			Refresh();
			return Components.Num() > 0;
		}

		/** The selected widget's name, for the label in front of the controls. */
		FText GetLabel() const
		{
			Refresh();
			const UWidget* Selected = SelectedWidget.Get();
			return Selected ? FText::FromString(Selected->GetDisplayLabel().IsEmpty() ? Selected->GetName() : Selected->GetDisplayLabel()) : FText::GetEmpty();
		}

		void Play(bool bOut)
		{
			ForEach([this, bOut](UWidgetAnimationComponent& Component) { Component.PlayDesignerPreviewOverride(bOut, Options); });
		}

		void Reset()
		{
			ForEach([](UWidgetAnimationComponent& Component) { Component.ResetDesignerPreviewOverride(); });
		}

		void Scrub(float Alpha)
		{
			ForEach([this, Alpha](UWidgetAnimationComponent& Component) { Component.ScrubDesignerPreviewOverride(Alpha, Options); });
		}

		/** Loop, Ping Pong, Invert or Speed changed: applied to whatever is showing, where it stands. */
		void PushOptions()
		{
			ForEach([this](UWidgetAnimationComponent& Component) { Component.UpdateDesignerPreviewOptions(Options); });
		}

		/** Readouts come from the first component; a widget's components play on one clock from the same Play. */
		float GetSeconds() const { const UWidgetAnimationComponent* First = GetFirst(); return First ? First->GetDesignerPreviewSeconds() : 0.f; }
		float GetDuration() const { const UWidgetAnimationComponent* First = GetFirst(); return First ? First->GetDesignerPreviewDuration() : 0.f; }
		float GetAlpha() const { const UWidgetAnimationComponent* First = GetFirst(); return First ? First->GetDesignerPreviewAlpha() : 0.f; }
		bool IsPlaying() const { const UWidgetAnimationComponent* First = GetFirst(); return First && First->IsDesignerPreviewPlaying(); }

		FWidgetAnimDesignerPlayback Options;

	private:

		const UWidgetAnimationComponent* GetFirst() const
		{
			Refresh();
			return Components.Num() > 0 ? Components[0].Get() : nullptr;
		}

		void ForEach(TFunctionRef<void(UWidgetAnimationComponent&)> Visit)
		{
			Refresh(/*bForce=*/true);
			for (const TWeakObjectPtr<UWidgetAnimationComponent>& Component : Components)
			{
				if (UWidgetAnimationComponent* Live = Component.Get())
				{
					Visit(*Live);
				}
			}
		}

		/**
		 * Finds the selected widget's components. At once when the selection changes; otherwise at most four
		 * times a second, since it walks every component - the designer rebuilds its preview freely, so what
		 * was found last time is never trusted for long.
		 */
		void Refresh(bool bForce = false) const
		{
			const TSharedPtr<FWidgetBlueprintEditor> Pinned = Editor.Pin();
			UWidget* Selected = nullptr;
			if (Pinned.IsValid() && Pinned->GetSelectedWidgets().Num() == 1)
			{
				Selected = Pinned->GetSelectedWidgets().CreateConstIterator()->GetPreview();
			}

			const double Now = FPlatformTime::Seconds();
			const bool bSelectionChanged = Selected != SelectedWidget.Get();
			if (!bForce && !bSelectionChanged && LastRefreshTime >= 0.0 && Now - LastRefreshTime < 0.25)
			{
				return;
			}

			LastRefreshTime = Now;
			SelectedWidget = Selected;
			Components.Reset();

			if (!Selected)
			{
				return;
			}

			// A component on the widget itself, or on any widget inside it - a placed widget of your own keeps
			// its component on its own root, inside its tree.
			for (TObjectIterator<UWidgetAnimationComponent> It(RF_ClassDefaultObject | RF_ArchetypeObject, /*bIncludeDerivedClasses=*/true, EInternalObjectFlags::Garbage); It; ++It)
			{
				UWidgetAnimationComponent* Component = *It;
				const UWidget* Animated = Component->GetAnimatedWidget();
				if (Animated && Animated->IsDesignTime() && (Animated == Selected || Animated->IsIn(Selected)) && Component->HasDesignerPreviewAnimations())
				{
					Components.Add(Component);
				}
			}
		}

		TWeakPtr<FWidgetBlueprintEditor> Editor;
		mutable TWeakObjectPtr<UWidget> SelectedWidget;
		mutable TArray<TWeakObjectPtr<UWidgetAnimationComponent>> Components;
		mutable double LastRefreshTime = -1.0;
	};

	TSharedRef<SWidget> MakeFixedToolbarIcon(TAttribute<const FSlateBrush*> Icon, TAttribute<FSlateColor> Tint)
	{
		return SNew(SBox)
			.WidthOverride(16.f)
			.HeightOverride(16.f)
			[
				SNew(SImage)
				.Image(Icon)
				.ColorAndOpacity(Tint)
			];
	}

	TSharedRef<SWidget> MakeToolbarButton(TAttribute<const FSlateBrush*> Icon, const FText& Label, const FText& ToolTip, TAttribute<FSlateColor> Tint, TFunction<void()> OnClicked)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.f, 2.f))
			.VAlign(VAlign_Center)
			.ToolTipText(ToolTip)
			.OnClicked_Lambda([OnClicked]() { OnClicked(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeFixedToolbarIcon(Icon, Tint)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(STextBlock).Text(Label)
				]
			];
	}

	TSharedRef<SWidget> MakeToolbarToggle(const FText& Label, const FText& ToolTip, bool FWidgetAnimDesignerPlayback::* Flag,
		TSharedRef<FSelectedWidgetAnimations> Selection, TAttribute<bool> IsEnabled = true)
	{
		return SNew(SCheckBox)
			.ToolTipText(ToolTip)
			.IsEnabled(IsEnabled)
			.IsChecked_Lambda([Selection, Flag]() { return Selection->Options.*Flag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Selection, Flag](ECheckBoxState State)
			{
				Selection->Options.*Flag = State == ECheckBoxState::Checked;
				Selection->PushOptions();
			})
			[
				SNew(STextBlock).Text(Label)
			];
	}

	/** The selected widget's controls, collapsed away whenever it has no animations. */
	TSharedRef<SWidget> MakeSelectedAnimationControls(TSharedRef<FSelectedWidgetAnimations> Selection)
	{
		const FSlateBrush* PlayBrush = PickBrush({ "Icons.Play", "PlayWorld.ResumePlaySession", "GenericPlay" });
		const FSlateBrush* PauseBrush = PickBrush({ "Icons.Pause", "PlayWorld.PausePlaySession", "GenericPause" });
		const FSlateBrush* ReverseBrush = PickBrush({ "Icons.Reverse", "Icons.ArrowLeft", "Animation.Backward" });
		const FSlateBrush* ResetBrush = PickBrush({ "Icons.Refresh", "GenericCommands.Undo" });

		return SNew(SHorizontalBox)
			.Visibility_Lambda([Selection]() { return Selection->HasAnimations() ? EVisibility::Visible : EVisibility::Collapsed; })

			// Which widget the controls are for.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.f, 0.f, 4.f, 0.f))
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.Text_Lambda([Selection]() { return Selection->GetLabel(); })
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeToolbarButton(
					TAttribute<const FSlateBrush*>::CreateLambda([Selection, PlayBrush, PauseBrush]() { return Selection->IsPlaying() ? PauseBrush : PlayBrush; }),
					LOCTEXT("PlayIn", "Play In"),
					LOCTEXT("PlayInTooltip", "Play the selected widget's entrance, as it would play in game. While anything is playing, pause it where it is."),
					TAttribute<FSlateColor>::CreateLambda([Selection]() { return FSlateColor(Selection->IsPlaying() ? PauseTint : PlayTint); }),
					[Selection]()
					{
						// Pausing is freezing on the moment it has reached - the scrubber's own freeze - so the
						// scrubber and the timer pick up from exactly there.
						if (Selection->IsPlaying())
						{
							Selection->Scrub(Selection->GetAlpha());
						}
						else
						{
							Selection->Play(/*bOut=*/false);
						}
					})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeToolbarButton(
					ReverseBrush,
					LOCTEXT("PlayOut", "Play Out"),
					LOCTEXT("PlayOutTooltip", "Play the selected widget's exit, from its settled pose."),
					FSlateColor(PlayTint),
					[Selection]() { Selection->Play(/*bOut=*/true); })
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeToolbarButton(
					ResetBrush,
					LOCTEXT("Reset", "Reset"),
					LOCTEXT("ResetTooltip", "Put the selected widget back as authored."),
					FSlateColor::UseForeground(),
					[Selection]() { Selection->Reset(); })
			]

			// The timer: where the preview is, and how long one pass takes.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.f, 0.f))
			[
				SNew(SBox)
				.MinDesiredWidth(76.f)
				[
					SNew(STextBlock)
					.Text_Lambda([Selection]()
					{
						return FText::FromString(FString::Printf(TEXT("%.2f / %.2f"), Selection->GetSeconds(), Selection->GetDuration()));
					})
				]
			]

			// The scrubber: drag to freeze on any moment of one pass.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(140.f)
				[
					SNew(SSlider)
					.ToolTipText(LOCTEXT("ScrubTooltip", "Drag to freeze the selected widget on any moment of its animation - of the exit, after Play Out."))
					.MinValue(0.f)
					.MaxValue(1.f)
					.Value_Lambda([Selection]() { return Selection->GetAlpha(); })
					.OnValueChanged_Lambda([Selection](float Value) { Selection->Scrub(Value); })
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
			[
				MakeToolbarToggle(LOCTEXT("Loop", "Loop"),
					LOCTEXT("LoopTooltip", "Loop what plays, as an entry with Loop set would - for as long as it is left playing."),
					&FWidgetAnimDesignerPlayback::bLoop, Selection)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
			[
				MakeToolbarToggle(LOCTEXT("PingPong", "Ping Pong"),
					LOCTEXT("PingPongTooltip", "Play back to the start between loops instead of snapping there. Needs Loop."),
					&FWidgetAnimDesignerPlayback::bPingPong, Selection,
					TAttribute<bool>::CreateLambda([Selection]() { return Selection->Options.bLoop; }))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
			[
				MakeToolbarToggle(LOCTEXT("Invert", "Invert"),
					LOCTEXT("InvertTooltip", "Play the animations the other way round from how their entries play them. Flipping it keeps the preview where it is."),
					&FWidgetAnimDesignerPlayback::bInvert, Selection)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(10.f, 0.f, 4.f, 0.f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Speed", "Speed"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(60.f)
				[
					SNew(SSpinBox<float>)
					.ToolTipText(LOCTEXT("SpeedTooltip", "Playback speed, on top of each entry's own."))
					.MinValue(0.01f)
					.MaxValue(10.f)
					.MinSliderValue(0.1f)
					.MaxSliderValue(4.f)
					.Delta(0.05f)
					.Value_Lambda([Selection]() { return Selection->Options.Speed; })
					.OnValueChanged_Lambda([Selection](float Value)
					{
						Selection->Options.Speed = FMath::Max(Value, 0.01f);
						Selection->PushOptions();
					})
				]
			];
	}

	/**
	 * Runs once per asset editor window. Adds nothing to the toolbar itself: the preview's controls - Play,
	 * the scrubber, Loop, Ping Pong, Invert, Speed - all live in the Widget Animations panel now, beside the
	 * tracks they drive. For a builder's editor it registers that panel's drawer and tab.
	 */
	/**
	 * Runs once per asset editor window. For any Widget Blueprint's editor, it registers the Widget
	 * Animations panel's drawer and tab - a WidgetAnimationBuilder's builds a generic animation, every other
	 * one builds animations against its own widgets - and, outside a builder, the selected widget's controls.
	 * The animation being built is played from the panel's own transport, not the toolbar.
	 */
	void BuildEntries(FToolMenuSection& Section)
	{
		const UAssetEditorToolkitMenuContext* Context = Section.FindContext<UAssetEditorToolkitMenuContext>();
		const TSharedPtr<FAssetEditorToolkit> Toolkit = Context ? Context->Toolkit.Pin() : nullptr;
		if (!Toolkit.IsValid() || !GetEditedWidgetBlueprint(*Toolkit))
		{
			// Not a Widget Blueprint's editor. Every other asset editor is left untouched.
			return;
		}

		RegisterWidgetAnimationsDrawer(Section);

		// The selected widget's controls: only in an ordinary Widget Blueprint's editor - a builder's own preview
		// is the panel's - and even there only while the selected widget has animations.
		UWidgetBlueprint* WidgetBlueprint = GetEditedWidgetBlueprint(*Toolkit);
		const bool bIsBuilder = WidgetBlueprint->GeneratedClass && WidgetBlueprint->GeneratedClass->IsChildOf(UWidgetAnimBuilder::StaticClass());
		if (!bIsBuilder && Toolkit->GetToolkitFName() == WidgetBlueprintEditorName)
		{
			const TSharedRef<FSelectedWidgetAnimations> Selection = MakeShared<FSelectedWidgetAnimations>(StaticCastSharedPtr<FWidgetBlueprintEditor>(Toolkit));
			Section.AddEntry(FToolMenuEntry::InitWidget("WidgetAnimSelectedPreview", MakeSelectedAnimationControls(Selection), FText::GetEmpty(), /*bNoIndent=*/true));
		}
	}
}

void FWidgetAnimBuilderToolbar::Register()
{
	// The panel's tab, as one of the Widget Blueprint editor's own - see FWidgetAnimationsTabSummoner - and its
	// closed slot beside the native Animations tab, which is where Dock in Layout opens it. Both have to be in
	// place before an editor opens, which is why they are hooked here rather than per editor.
	if (IUMGEditorModule* UMGEditor = FModuleManager::LoadModulePtr<IUMGEditorModule>(TEXT("UMGEditor")))
	{
		if (!RegisterTabsHandle.IsValid())
		{
			RegisterTabsHandle = UMGEditor->OnRegisterTabsForEditor().AddLambda([](const FWidgetBlueprintApplicationMode& ApplicationMode, FWorkflowAllowedTabSet& TabFactories)
			{
				TabFactories.RegisterFactory(MakeShared<FWidgetAnimationsTabSummoner>(ApplicationMode.GetBlueprintEditor()));
			});
		}

		if (!RegisterLayoutHandle.IsValid())
		{
			RegisterLayoutHandle = UMGEditor->OnRegisterLayoutExtensions().AddLambda([](FLayoutExtender& Extender)
			{
				for (const FName NativeTabId : NativeAnimationsTabIds)
				{
					Extender.ExtendLayout(FTabId(NativeTabId), ELayoutExtensionPosition::After, FTabManager::FTab(FTabId(WidgetAnimationsTabId), ETabState::ClosedTab));
				}
			});
		}
	}

	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	// The Widget Blueprint editor's own toolbar - built once per editor window, which is what makes it the
	// hook. AssetEditorToolbar.CommonActions is the fallback, the one menu every asset editor builds.
	UToolMenu* Menu = ToolMenus->ExtendMenu("AssetEditor.WidgetBlueprintEditor.ToolBar");
	if (!Menu)
	{
		Menu = ToolMenus->ExtendMenu("AssetEditorToolbar.CommonActions");
	}

	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection("WidgetAnimPreview");

	// Dynamic, because it runs once per asset editor window and the context that says which asset that window
	// is editing only exists at that point. It adds no buttons - the toolbar stays as UMG built it - it is
	// only the per-editor hook the Widget Animations drawer and tab are registered from.
	Section.AddDynamicEntry("WidgetAnimPreviewControls", FNewToolMenuSectionDelegate::CreateStatic(&BuildEntries));
}

void FWidgetAnimBuilderToolbar::Unregister()
{
	if (IUMGEditorModule* UMGEditor = FModuleManager::GetModulePtr<IUMGEditorModule>(TEXT("UMGEditor")))
	{
		UMGEditor->OnRegisterTabsForEditor().Remove(RegisterTabsHandle);
		UMGEditor->OnRegisterLayoutExtensions().Remove(RegisterLayoutHandle);
	}
	RegisterTabsHandle.Reset();
	RegisterLayoutHandle.Reset();
}

#undef LOCTEXT_NAMESPACE