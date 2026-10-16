// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimBuilderToolbar.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBDColors.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/SWindow.h"
#include "SWidgetAnimTimeline.h"
#include "SWidgetAnimScriptPanel.h"
#include "SWidgetAnimStateMachineEditor.h"
#include "WidgetAnimScripting.h"
#include "WidgetAnimPanelSource.h"
#include "WidgetAnimTimelines.h"
#include "WidgetAnimDockPreference.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimScripting, Log, All);

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
	// track groups, their layers, tracks and keys, and a Selection panel for whatever is picked. Dock in
	// Layout opens the same panel as a tab in the editor's layout instead.
	//
	// In a WidgetAnimationBuilder it edits the builder's working copy, for generic animations saved with
	// Create. In any other Widget Blueprint it edits a WA_ asset directly, each Track Group playing on one of
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
	 * What Dock in Layout puts the panel beside: the first of these that is open - the native Animations tab under
	 * the names it has gone by, then the bottom well's other tabs. Never opened for the purpose.
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

		/** The Widget Animation Scripting drawer's script area, once built - kept for the editor's life, like the panel. */
		TSharedRef<TSharedPtr<SWidget>> ScriptingDrawerContent = MakeShared<TSharedPtr<SWidget>>();

		/**
		 * Whether Dock in Layout was used in this editor. Every Widget Blueprint editor shares one saved layout, so a scripting
		 * tab left open in one is restored into all of them; an editor that never docked it closes it again (see
		 * MakeScriptingTabBody).
		 */
		bool bScriptingDocked = false;
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
	/** Whether this editor has the Widget Animations tab open - docked by Dock in Layout, or in its own slot. */
	bool IsWidgetAnimationsTabOpen(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
		const TSharedPtr<FTabManager> TabManager = Toolkit.IsValid() ? Toolkit->GetTabManager() : nullptr;
		return TabManager.IsValid()
			&& (TabManager->FindExistingLiveTab(FTabId(WidgetAnimationsTabId)).IsValid() || TabManager->FindExistingLiveTab(FTabId(WidgetAnimationsDockedTabId)).IsValid());
	}

	/** The panel as a tab's body - watched, so closing the tab by hand means it isn't docked any more (WidgetAnimDockPreference). */
	TSharedRef<SWidget> MakeWidgetAnimationsTabBody(const TSharedRef<IWidgetAnimPanelSource>& PanelSource, const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		return WidgetAnimDockPreference::Watch(WidgetAnimDockPreference::WidgetAnimationsPanel, WeakToolkit,
			[WeakToolkit]() { return IsWidgetAnimationsTabOpen(WeakToolkit); },
			SNew(SWidgetAnimTimeline).Source(PanelSource));
	}

	TSharedRef<SDockTab> MakeWidgetAnimationsTab(const TSharedRef<IWidgetAnimPanelSource>& PanelSource, const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::PanelTab)
			.Label(LOCTEXT("WidgetAnimationsTab", "Widget Animations"))
			[
				MakeWidgetAnimationsTabBody(PanelSource, WeakToolkit)
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
			TabIcon = FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::WidgetAnimationsIcon);
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

			return MakeWidgetAnimationsTabBody(GetPanelSource(Toolkit.ToSharedRef()), TWeakPtr<FAssetEditorToolkit>(Toolkit));
		}
	};

	/** Registers the panel's tab spawner on an editor's tab manager by hand, unless its mode already has. */
	void EnsureTabSpawner(const TSharedPtr<FTabManager>& TabManager, const TSharedRef<IWidgetAnimPanelSource>& PanelSource, const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		if (!TabManager.IsValid() || TabManager->HasTabSpawner(WidgetAnimationsTabId))
		{
			return;
		}

		const FSlateIcon Icon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::WidgetAnimationsIcon);
		TabManager->RegisterTabSpawner(WidgetAnimationsTabId, FOnSpawnTab::CreateLambda([PanelSource, WeakToolkit](const FSpawnTabArgs&)
		{
			return MakeWidgetAnimationsTab(PanelSource, WeakToolkit);
		}))
		.SetDisplayName(LOCTEXT("WidgetAnimationsTabName", "Widget Animations"))
		.SetTooltipText(LOCTEXT("WidgetAnimationsTabTooltip", "Build the widget animation: its tracks, keys and preview settings."))
		.SetIcon(Icon);
	}

	/**
	 * Docks the panel in the editor's bottom tab well, where the native Animations panel's Dock in Layout puts that
	 * one. Already open, it is brought forward.
	 *
	 * First choice: beside a tab of that well that is already open - the native Animations tab, or the Compiler
	 * Results - put straight into its well under a spawner-less document id, which is the only way a tab can be put
	 * next to another. Nothing is opened to find one: this used to open the native Animations tab and the Compiler
	 * Results in the hope of docking beside them, and then judged every tab to be outside the editor's window - a
	 * tab inside an asset editor reports no window of its own - so it closed the panel it had just docked and left
	 * those two open instead.
	 *
	 * Failing that: the tab's own slot, the one the layout extension gave it beside the native Animations tab -
	 * wherever the layout has it, or floating if the layout has none. Dragged into place once, the layout keeps it.
	 *
	 * The drawer is dismissed first, as the native Dock in Layout dismisses its own. It slides up over the very
	 * place the tab opens, so a tab docked behind an open drawer looks as if nothing happened at all.
	 *
	 * Docked, the choice is kept (WidgetAnimDockPreference): every Widget Blueprint editor opened after - this session or the
	 * next - docks it again as it opens, with bRestore: quietly, nothing brought forward, the drawer left alone.
	 */
	void DockInLayout(TWeakPtr<FAssetEditorToolkit> WeakToolkit, TSharedRef<IWidgetAnimPanelSource> PanelSource, bool bRestore = false)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
		const TSharedPtr<FTabManager> TabManager = Toolkit.IsValid() ? Toolkit->GetTabManager() : nullptr;
		if (!TabManager.IsValid())
		{
			return;
		}

		if (UStatusBarSubsystem* StatusBars = !bRestore && GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr)
		{
			StatusBars->ForceDismissDrawer();
		}

		EnsureTabSpawner(TabManager, PanelSource, WeakToolkit);

		auto BringForward = [&TabManager, bRestore](const TSharedRef<SDockTab>& Tab)
		{
			if (!bRestore)
			{
				Tab->ActivateInParent(ETabActivationCause::SetDirectly);
				TabManager->DrawAttention(Tab);
			}
		};
		auto RememberDocked = [&Toolkit]()
		{
			WidgetAnimDockPreference::SetDocked(WidgetAnimDockPreference::WidgetAnimationsPanel, WidgetAnimDockPreference::GetEditorMode(Toolkit));
		};

		// Open already - docked by this, or put somewhere by hand: there.
		for (const FName OpenId : { WidgetAnimationsTabId, WidgetAnimationsDockedTabId })
		{
			if (const TSharedPtr<SDockTab> Live = TabManager->FindExistingLiveTab(FTabId(OpenId)))
			{
				BringForward(Live.ToSharedRef());
				RememberDocked();
				return;
			}
		}

		// Beside an open tab of the bottom well - only one that is open already, never one opened for the purpose.
		for (const FName NativeTabId : DockBesideTabIds)
		{
			if (TabManager->FindExistingLiveTab(FTabId(NativeTabId)).IsValid())
			{
				const TSharedRef<SDockTab> PanelTab = MakeWidgetAnimationsTab(PanelSource, WeakToolkit);
				WidgetAnimDockPreference::InsertDocumentTab(*TabManager, WidgetAnimationsDockedTabId, FTabManager::FLiveTabSearch(NativeTabId), PanelTab);
				BringForward(PanelTab);
				RememberDocked();
				return;
			}
		}

		// Its own slot in the layout, wherever that is.
		if (const TSharedPtr<SDockTab> Opened = TabManager->TryInvokeTab(FTabId(WidgetAnimationsTabId)))
		{
			BringForward(Opened.ToSharedRef());
			RememberDocked();
		}
	}

	/** The drawer's panel: built on first use and kept, so its selection and zoom survive the drawer closing. */
	TSharedRef<SWidget> GetOrMakeDrawerPanel(const TSharedRef<TSharedPtr<SWidget>>& DrawerContent, const TSharedRef<IWidgetAnimPanelSource>& PanelSource, const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		if (!DrawerContent->IsValid())
		{
			*DrawerContent = SNew(SWidgetAnimTimeline)
				.Source(PanelSource)
				.OnDockInLayout(FSimpleDelegate::CreateLambda([WeakToolkit, PanelSource]() { DockInLayout(WeakToolkit, PanelSource); }));
		}
		return DrawerContent->ToSharedRef();
	}

	// ---------------------------------------------------------------------------------------------
	// Widget Animation Scripting drawer, and the tab it docks into
	//
	// The Script area of the Widget Animations panel, as a drawer of its own on the status bar beside Widget
	// Animations: Python that makes and changes tracks, run on the animation open in this editor's panel. Dock in
	// Layout opens it as a tab beside the Details panel, with the output under the code. The area belongs to the
	// editor rather than to one panel - it asks for the panel each time it runs (FindScriptedPanel) - so the drawer,
	// the tab and the area under the Track Editor all script the same animation.
	// ---------------------------------------------------------------------------------------------

	const FName WidgetAnimationScriptingDrawerId(TEXT("WidgetAnimBuilder.WidgetAnimationScripting"));

	/** The scripting tab: Window > Widget Animation Scripting, and what Dock in Layout opens when there is no Details tab to put it beside. */
	const FName WidgetAnimationScriptingTabId(TEXT("WidgetAnimBuilder.WidgetAnimationScriptingPanel"));

	/** The id a scripting tab put beside the Details tab is given. Without a spawner: a tab can only be put beside another under such an id. */
	const FName WidgetAnimationScriptingDockedTabId(TEXT("WidgetAnimBuilder.WidgetAnimationScriptingDocked"));

	/** The Details tab the layout gives the scripting tab a closed slot beside - the Designer's. */
	const FName ScriptingLayoutBesideTabId(TEXT("WidgetDetails"));

	/** The ids Details tabs go by: only a tie-break - the tab is found among the tabs on screen (FindScriptingDockDetailsTab). */
	const FName ScriptingDetailsTabIds[] = { TEXT("WidgetDetails"), TEXT("Inspector"), TEXT("DetailsTab"), TEXT("Details"), TEXT("PropertiesTab") };

	/** Right after Widget Animations. */
	constexpr int32 WidgetAnimationScriptingDrawerSlot = 3;

	/** The code icon - one icon for the drawer button, the docked tab and the Window menu entry. */
	FSlateIcon GetScriptingSlateIcon()
	{
		for (const FName Candidate : { FName(TEXT("Icons.Code")), FName(TEXT("MainFrame.OpenSourceCodeEditor")) })
		{
			if (FAppStyle::Get().GetOptionalBrush(Candidate, nullptr, nullptr))
			{
				return FSlateIcon(FAppStyle::GetAppStyleSetName(), Candidate);
			}
		}
		return FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::WidgetAnimationsIcon);
	}

	const FSlateBrush* GetScriptingIcon()
	{
		return GetScriptingSlateIcon().GetIcon();
	}

	/** The editor of the Widget Blueprint, among those with the drawers. */
	TSharedPtr<FAssetEditorToolkit> FindToolkitFor(const UWidgetBlueprint* WidgetBlueprint)
	{
		if (!WidgetBlueprint)
		{
			return nullptr;
		}
		for (const FEditorWidgetAnimations& Entry : GetEditorsWithDrawer())
		{
			const TSharedPtr<FAssetEditorToolkit> Toolkit = Entry.Toolkit.Pin();
			if (Toolkit.IsValid() && GetEditedWidgetBlueprint(*Toolkit) == WidgetBlueprint)
			{
				return Toolkit;
			}
		}
		return nullptr;
	}

	/**
	 * The Widget Animations panel the script runs on in this editor: the one worked in last - the drawer's or the docked
	 * tab's - else the drawer's, built for the purpose if it hasn't been opened yet.
	 */
	TSharedPtr<SWidgetAnimTimeline> FindScriptedPanel(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
		if (!Toolkit.IsValid())
		{
			return nullptr;
		}

		// The source first: making it may tidy the entry list, which would leave a reference into it pointing elsewhere.
		const TSharedRef<IWidgetAnimPanelSource> PanelSource = GetPanelSource(Toolkit.ToSharedRef());
		if (const TSharedPtr<SWidgetAnimTimeline> Used = WidgetAnimScripting::FindLastUsedPanelFor(&PanelSource.Get()))
		{
			return Used;
		}

		const TSharedRef<TSharedPtr<SWidget>> DrawerContent = FindOrAddEditor(Toolkit.ToSharedRef()).DrawerContent;
		return StaticCastSharedRef<SWidgetAnimTimeline>(GetOrMakeDrawerPanel(DrawerContent, PanelSource, WeakToolkit));
	}

	/** This editor's panel source - what its Widget Animations panels edit - without building a panel. Null once the editor is gone. */
	TSharedPtr<IWidgetAnimPanelSource> FindScriptedSource(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
		return Toolkit.IsValid() ? TSharedPtr<IWidgetAnimPanelSource>(GetPanelSource(Toolkit.ToSharedRef())) : nullptr;
	}

	/** Hands the tab manager the very tab to dock beside, rather than an id for it to look up - a missed lookup floats the new tab in a window. */
	class FScriptingDockBesideTab : public FTabManager::FSearchPreference
	{
	public:
		explicit FScriptingDockBesideTab(const TSharedRef<SDockTab>& InBeside) : Beside(InBeside) {}

		virtual TSharedPtr<SDockTab> Search(const FTabManager& Manager, FName PlaceholderId, const TSharedRef<SDockTab>& UnmanagedTab) const override
		{
			return Beside;
		}

	private:
		TSharedRef<SDockTab> Beside;
	};

	/** Every tab under a widget. Tabs are their wells' children, so a Details tab behind another in its well is found too. */
	void GatherScriptingDockTabs(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SDockTab>>& OutTabs, int32 Depth)
	{
		if (Depth > 256)
		{
			return;
		}
		if (Widget->GetType() == FName(TEXT("SDockTab")))
		{
			OutTabs.Add(StaticCastSharedRef<SDockTab>(Widget));
		}
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
			{
				GatherScriptingDockTabs(Children->GetChildAt(ChildIndex), OutTabs, Depth + 1);
			}
		}
	}

	/** -1 when the tab isn't a Details tab, else where its id falls in ScriptingDetailsTabIds (past the end for one only named so). */
	int32 GetScriptingDetailsRank(const TSharedRef<SDockTab>& Tab)
	{
		const FName TabType = Tab->GetLayoutIdentifier().TabType;
		if (Tab->GetTabRole() == ETabRole::MajorTab || TabType == WidgetAnimationScriptingTabId || TabType == WidgetAnimationScriptingDockedTabId)
		{
			return -1;
		}
		const int32 NumIds = static_cast<int32>(UE_ARRAY_COUNT(ScriptingDetailsTabIds));
		for (int32 IdIndex = 0; IdIndex < NumIds; ++IdIndex)
		{
			if (TabType == ScriptingDetailsTabIds[IdIndex])
			{
				return IdIndex;
			}
		}
		const FString TypeString = TabType.ToString();
		if (TypeString.Contains(TEXT("Details")) || TypeString.Contains(TEXT("Inspector")) || Tab->GetTabLabel().ToString().Equals(TEXT("Details")))
		{
			return NumIds;
		}
		return -1;
	}

	/**
	 * Where the scripting tab may go, best first: this editor's Details tab (found among the tabs inside the editor itself, so
	 * it never depends on the id this engine version gives it), then one asked of the tab manager by id, then the tab furthest
	 * to the right in the editor - the right-hand column Details lives in. Every tab seen is logged, so a dock that still goes
	 * wrong says what it saw.
	 */
	TArray<TSharedRef<SDockTab>> FindScriptingDockTargets(const TSharedRef<FAssetEditorToolkit>& Toolkit, const TSharedPtr<FTabManager>& TabManager)
	{
		TArray<TSharedRef<SDockTab>> Tabs;
		TSharedPtr<SWidget> HostWidget;
		if (Toolkit->IsHosted())
		{
			HostWidget = Toolkit->GetToolkitHost()->GetParentWidget();
			GatherScriptingDockTabs(HostWidget.ToSharedRef(), Tabs, 0);
		}
		// The host showed none: the window it is in.
		if (Tabs.Num() == 0 && HostWidget.IsValid())
		{
			if (const TSharedPtr<SWindow> HostWindow = FSlateApplication::Get().FindWidgetWindow(HostWidget.ToSharedRef()))
			{
				GatherScriptingDockTabs(HostWindow.ToSharedRef(), Tabs, 0);
			}
		}
		UE_LOG(LogWidgetAnimScripting, Log, TEXT("Dock in Layout: %d tabs in the editor."), Tabs.Num());

		TArray<TPair<int32, TSharedRef<SDockTab>>> Details;
		TSharedPtr<SDockTab> Rightmost;
		float RightmostEdge = -FLT_MAX;
		for (const TSharedRef<SDockTab>& Tab : Tabs)
		{
			const bool bEditorsOwn = TabManager.IsValid() && Tab->GetTabManagerPtr() == TabManager;
			const FGeometry& Geometry = Tab->GetTickSpaceGeometry();
			const float RightEdge = Geometry.GetAbsolutePosition().X + Geometry.GetAbsoluteSize().X;
			const int32 Rank = GetScriptingDetailsRank(Tab);
			UE_LOG(LogWidgetAnimScripting, Log, TEXT("Dock in Layout:   tab '%s' (%s)%s%s, right edge %.0f."), *Tab->GetTabLabel().ToString(),
				*Tab->GetLayoutIdentifier().ToString(), bEditorsOwn ? TEXT(", this editor's own") : TEXT(""), Rank >= 0 ? TEXT(", a Details tab") : TEXT(""), RightEdge);

			const FName TabType = Tab->GetLayoutIdentifier().TabType;
			if (Tab->GetTabRole() == ETabRole::MajorTab || TabType == WidgetAnimationScriptingTabId || TabType == WidgetAnimationScriptingDockedTabId)
			{
				continue;
			}
			if (Rank >= 0)
			{
				Details.Emplace((bEditorsOwn ? 1000 : 0) + (100 - Rank), Tab);
			}
			if (Geometry.GetAbsoluteSize().X > 0.f && RightEdge > RightmostEdge)
			{
				Rightmost = Tab;
				RightmostEdge = RightEdge;
			}
		}

		Details.StableSort([](const TPair<int32, TSharedRef<SDockTab>>& A, const TPair<int32, TSharedRef<SDockTab>>& B) { return A.Key > B.Key; });
		TArray<TSharedRef<SDockTab>> Targets;
		for (const TPair<int32, TSharedRef<SDockTab>>& Entry : Details)
		{
			Targets.AddUnique(Entry.Value);
		}
		if (TabManager.IsValid())
		{
			for (const FName CandidateId : ScriptingDetailsTabIds)
			{
				if (const TSharedPtr<SDockTab> ById = TabManager->FindExistingLiveTab(FTabId(CandidateId)))
				{
					Targets.AddUnique(ById.ToSharedRef());
				}
			}
		}
		if (Rightmost.IsValid())
		{
			Targets.AddUnique(Rightmost.ToSharedRef());
		}
		return Targets;
	}

	TSharedPtr<SDockTab> FindLiveScriptingTab(const TSharedPtr<FTabManager>& TabManager);

	/** The script that belongs to this editor's Widget Blueprint - every widget keeps its own. */
	FString GetScriptingKey(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
		return Toolkit.IsValid() ? SWidgetAnimScriptPanel::MakeScriptKey(GetEditedWidgetBlueprint(*Toolkit)) : FString();
	}

	/** Whether Dock in Layout was used in this editor - see FEditorWidgetAnimations::bScriptingDocked. */
	bool WasScriptingDocked(const TSharedPtr<FAssetEditorToolkit>& Toolkit)
	{
		const FEditorWidgetAnimations* Entry = GetEditorsWithDrawer().FindByPredicate([&Toolkit](const FEditorWidgetAnimations& Candidate)
		{
			return Candidate.Toolkit.Pin() == Toolkit;
		});
		return (Entry && Entry->bScriptingDocked) || WidgetAnimDockPreference::IsDockedIn(WidgetAnimDockPreference::ScriptingPanel, Toolkit);
	}

	/**
	 * Takes Tab out of its well at once. A close request alone can leave it among the live tabs a moment longer, and the next
	 * insert under the same id then trips the tab manager's ensure (FindTabInLiveAreas) - a callstack capture, a hitch.
	 */
	void CloseTabNow(const TSharedRef<SDockTab>& Tab)
	{
		const TSharedPtr<FTabManager> Manager = Tab->GetTabManagerPtr();
		Tab->RequestCloseTab();
		if (Manager.IsValid() && Manager->FindExistingLiveTab(Tab->GetLayoutIdentifier()) == Tab)
		{
			Tab->RemoveTabFromParent();
		}
	}

	/** Closes this editor's scripting tab, wherever it is. */
	void CloseScriptingTab(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
		if (const TSharedPtr<SDockTab> Live = Toolkit.IsValid() ? FindLiveScriptingTab(Toolkit->GetTabManager()) : nullptr)
		{
			Live->RequestCloseTab();
		}
	}

	/**
	 * The script area for a tab: the output under the code, and its close button closes the tab - the one docked beside Details
	 * or the one the Window menu opens. (Asked of the tab manager: a tab's content isn't inside the tab widget, so the tab
	 * can't be found by walking up from it.)
	 *
	 * The saved layout is shared by every Widget Blueprint editor, so a scripting tab docked in one comes back open in every
	 * other one opened after it. Such a tab - in an editor where Dock in Layout was never used - closes itself on the next
	 * frame, once it is in place.
	 */
	TSharedRef<SWidget> MakeScriptingTabBody(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		if (!WasScriptingDocked(WeakToolkit.Pin()))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakToolkit](float) -> bool
			{
				if (!WasScriptingDocked(WeakToolkit.Pin()))
				{
					CloseScriptingTab(WeakToolkit);
				}
				return false;
			}));
		}

		// Watched: closing the tab by hand means it isn't docked any more (WidgetAnimDockPreference).
		return WidgetAnimDockPreference::Watch(WidgetAnimDockPreference::ScriptingPanel, WeakToolkit,
			[WeakToolkit]() { const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin(); return Toolkit.IsValid() && FindLiveScriptingTab(Toolkit->GetTabManager()).IsValid(); },
			SNew(SWidgetAnimScriptPanel)
			.FindPanel([WeakToolkit]() { return FindScriptedPanel(WeakToolkit); })
			.FindSource([WeakToolkit]() { return FindScriptedSource(WeakToolkit); })
			.ScriptKey(GetScriptingKey(WeakToolkit))
			.Docked(true)
			.OnClose_Lambda([WeakToolkit]() { CloseScriptingTab(WeakToolkit); }));
	}

	TSharedRef<SDockTab> MakeScriptingTab(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		const TSharedRef<SDockTab> Tab = SNew(SDockTab)
			.TabRole(ETabRole::PanelTab)
			.Label(LOCTEXT("WidgetAnimationScriptingTab", "Widget Animation Scripting"))
			.ToolTipText(NameTBD::Tooltips::WidgetAnimEditor::ScriptTabTooltip());
		Tab->SetTabIcon(GetScriptingIcon());
		Tab->SetContent(MakeScriptingTabBody(WeakToolkit));
		return Tab;
	}

	/** The scripting tab as one of the Widget Blueprint editor's own: in its Window menu, and opened in the slot the layout gives it. */
	class FWidgetAnimationScriptingTabSummoner : public FWorkflowTabFactory
	{
	public:

		explicit FWidgetAnimationScriptingTabSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp)
			: FWorkflowTabFactory(WidgetAnimationScriptingTabId, InHostingApp)
		{
			TabLabel = LOCTEXT("WidgetAnimationScriptingTab", "Widget Animation Scripting");
			TabIcon = GetScriptingSlateIcon();
			bIsSingleton = true;
			ViewMenuDescription = LOCTEXT("WidgetAnimationScriptingTabName", "Widget Animation Scripting");
			ViewMenuTooltip = NameTBD::Tooltips::WidgetAnimEditor::ScriptingDrawerTooltip();
		}

		virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override
		{
			return MakeScriptingTabBody(TWeakPtr<FAssetEditorToolkit>(HostingApp.Pin()));
		}
	};

	/** Registers the scripting tab's spawner on an editor's tab manager by hand, unless its mode already has. */
	void EnsureScriptingTabSpawner(const TSharedPtr<FTabManager>& TabManager, const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		if (!TabManager.IsValid() || TabManager->HasTabSpawner(WidgetAnimationScriptingTabId))
		{
			return;
		}

		TabManager->RegisterTabSpawner(WidgetAnimationScriptingTabId, FOnSpawnTab::CreateLambda([WeakToolkit](const FSpawnTabArgs&)
		{
			return MakeScriptingTab(WeakToolkit);
		}))
		.SetDisplayName(LOCTEXT("WidgetAnimationScriptingTabName", "Widget Animation Scripting"))
		.SetTooltipText(NameTBD::Tooltips::WidgetAnimEditor::ScriptingDrawerTooltip())
		.SetIcon(GetScriptingSlateIcon());
	}

	/** This editor's scripting tab, if it is open - docked beside Details, or opened from the Window menu. */
	TSharedPtr<SDockTab> FindLiveScriptingTab(const TSharedPtr<FTabManager>& TabManager)
	{
		if (!TabManager.IsValid())
		{
			return nullptr;
		}
		for (const FName OpenId : { WidgetAnimationScriptingTabId, WidgetAnimationScriptingDockedTabId })
		{
			if (const TSharedPtr<SDockTab> Live = TabManager->FindExistingLiveTab(FTabId(OpenId)))
			{
				return Live;
			}
		}
		return nullptr;
	}

	/**
	 * Docks Widget Animation Scripting in the editor's layout as a tab beside the Details panel - or brings it forward, open
	 * already. As Widget Animations' own Dock in Layout does: the drawer is dismissed first, and the tab goes into the Details
	 * tab's well under a spawner-less document id. Failing that, the tab's own slot in the layout - beside Details, where the
	 * layout extension put it - or floating if the layout has none. Returns the tab, or null when there is no editor.
	 */
	TSharedPtr<SDockTab> DockScriptingInLayout(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit, bool bRestore)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
		const TSharedPtr<FTabManager> TabManager = Toolkit.IsValid() ? Toolkit->GetTabManager() : nullptr;
		if (!TabManager.IsValid())
		{
			UE_LOG(LogWidgetAnimScripting, Warning, TEXT("Dock in Layout: the editor has no tab manager."));
			return nullptr;
		}

		if (UStatusBarSubsystem* StatusBars = !bRestore && GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr)
		{
			StatusBars->ForceDismissDrawer();
		}

		// This editor's from now on: a tab the layout restores here stays (see MakeScriptingTabBody).
		FindOrAddEditor(Toolkit.ToSharedRef()).bScriptingDocked = true;

		EnsureScriptingTabSpawner(TabManager, WeakToolkit);

		// Docked: brought forward (not when restored as the editor opens), and the choice kept for every editor opened after.
		auto BringForward = [&Toolkit, bRestore](const TSharedRef<SDockTab>& Tab)
		{
			WidgetAnimDockPreference::SetDocked(WidgetAnimDockPreference::ScriptingPanel, WidgetAnimDockPreference::GetEditorMode(Toolkit));
			if (bRestore)
			{
				return;
			}
			Tab->ActivateInParent(ETabActivationCause::SetDirectly);
			if (const TSharedPtr<FTabManager> Manager = Tab->GetTabManagerPtr())
			{
				Manager->DrawAttention(Tab);
			}
		};

		if (const TSharedPtr<SDockTab> Live = FindLiveScriptingTab(TabManager))
		{
			// Floating in a window of its own (where an earlier dock left it): closed, and docked properly below. A tab inside
			// the editor reports no window - only a floating tab's area has one.
			if (!Live->GetParentWindow().IsValid())
			{
				BringForward(Live.ToSharedRef());
				return Live;
			}
			UE_LOG(LogWidgetAnimScripting, Log, TEXT("Dock in Layout: the tab was floating in a window of its own - docking it in the editor instead."));
			CloseTabNow(Live.ToSharedRef());
		}

		// Beside the Details tab - else the next place on the list.
		for (const TSharedRef<SDockTab>& Beside : FindScriptingDockTargets(Toolkit.ToSharedRef(), TabManager))
		{
			const TSharedPtr<FTabManager> DockManager = Beside->GetTabManagerPtr().IsValid() ? Beside->GetTabManagerPtr() : TabManager;

			// One docked already under this manager - which the editor's own manager doesn't list when the well is another
			// manager's: brought forward. One left floating there: closed first, so no two tabs ever share the id.
			if (const TSharedPtr<SDockTab> Existing = DockManager->FindExistingLiveTab(FTabId(WidgetAnimationScriptingDockedTabId)))
			{
				if (!Existing->GetParentWindow().IsValid())
				{
					BringForward(Existing.ToSharedRef());
					return Existing;
				}
				CloseTabNow(Existing.ToSharedRef());
			}

			const TSharedRef<SDockTab> Tab = MakeScriptingTab(WeakToolkit);
			WidgetAnimDockPreference::InsertDocumentTab(*DockManager, WidgetAnimationScriptingDockedTabId, FScriptingDockBesideTab(Beside), Tab);

			const TSharedPtr<SWidget> TabWell = Tab->GetParentWidget();
			if ((TabWell.IsValid() && TabWell == Beside->GetParentWidget()) || DockManager->FindExistingLiveTab(Tab->GetLayoutIdentifier()) == Tab)
			{
				BringForward(Tab);
				UE_LOG(LogWidgetAnimScripting, Log, TEXT("Dock in Layout: docked beside '%s' (%s)."), *Beside->GetTabLabel().ToString(), *Beside->GetLayoutIdentifier().ToString());
				return Tab;
			}
			UE_LOG(LogWidgetAnimScripting, Warning, TEXT("Dock in Layout: couldn't put the tab beside '%s' (%s)."), *Beside->GetTabLabel().ToString(), *Beside->GetLayoutIdentifier().ToString());
			CloseTabNow(Tab);
		}

		// Its own slot in the layout, wherever that is.
		if (const TSharedPtr<SDockTab> Opened = TabManager->TryInvokeTab(FTabId(WidgetAnimationScriptingTabId)))
		{
			BringForward(Opened.ToSharedRef());
			UE_LOG(LogWidgetAnimScripting, Warning, TEXT("Dock in Layout: nowhere to dock beside - opened in the tab's own slot of the layout."));
			return Opened;
		}
		return nullptr;
	}

	/** The drawer's script area: Dock in Layout docks it beside Details, close closes the drawer. */
	TSharedRef<SWidget> MakeScriptingDrawerContent(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
	{
		auto DismissDrawer = []()
		{
			if (UStatusBarSubsystem* StatusBars = GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr)
			{
				StatusBars->ForceDismissDrawer();
			}
		};

		return SNew(SWidgetAnimScriptPanel)
			.FindPanel([WeakToolkit]() { return FindScriptedPanel(WeakToolkit); })
			.FindSource([WeakToolkit]() { return FindScriptedSource(WeakToolkit); })
			.ScriptKey(GetScriptingKey(WeakToolkit))
			.InDrawer(true)
			.OnClose_Lambda(DismissDrawer)
			.OnDock_Lambda([WeakToolkit]() { DockScriptingInLayout(WeakToolkit, /*bRestore=*/false); });
	}

	/** Puts this editor's Widget Animation Scripting drawer on its status bar, right after Widget Animations - replacing it if it is there. */
	void PlaceScriptingDrawer(const TSharedRef<FAssetEditorToolkit>& Toolkit, const FEditorWidgetAnimations& Entry)
	{
		UStatusBarSubsystem* StatusBars = GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr;
		if (!StatusBars || !Toolkit->IsHosted())
		{
			return;
		}

		const TSharedRef<TSharedPtr<SWidget>> DrawerContent = Entry.ScriptingDrawerContent;
		const TWeakPtr<FAssetEditorToolkit> WeakToolkit(Toolkit);

		FWidgetDrawerConfig Config(WidgetAnimationScriptingDrawerId);
		Config.ButtonText = LOCTEXT("WidgetAnimationScriptingDrawer", "Widget Animation Scripting");
		Config.ToolTipText = NameTBD::Tooltips::WidgetAnimEditor::ScriptingDrawerTooltip();
		Config.Icon = GetScriptingIcon();

		// Built on first open and kept, so the code editor and the output survive the drawer closing.
		Config.GetDrawerContentDelegate.BindLambda([DrawerContent, WeakToolkit]() -> TSharedRef<SWidget>
		{
			if (!DrawerContent->IsValid())
			{
				*DrawerContent = MakeScriptingDrawerContent(WeakToolkit);
			}
			return DrawerContent->ToSharedRef();
		});

		const FName StatusBarName = Toolkit->GetToolkitHost()->GetStatusBarName();
		StatusBars->UnregisterDrawer(StatusBarName, WidgetAnimationScriptingDrawerId);
		StatusBars->RegisterDrawer(StatusBarName, MoveTemp(Config), WidgetAnimationScriptingDrawerSlot);
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
		Config.ToolTipText = LOCTEXT("WidgetAnimationsDrawerTooltip", "Build widget animations: track groups for each widget they move, their tracks and keys.");
		Config.Icon = FNameTBDStyle::Get().GetBrush(FNameTBDStyle::WidgetAnimationsIcon);

		// The panel is built on first open and kept, so its selection and zoom survive the drawer closing -
		// and the drawer being taken down and put back.
		Config.GetDrawerContentDelegate.BindLambda([DrawerContent, PanelSource, WeakToolkit]() -> TSharedRef<SWidget>
		{
			return GetOrMakeDrawerPanel(DrawerContent, PanelSource, WeakToolkit);
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
			EnsureTabSpawner(LiveToolkit->GetTabManager(), PanelSource, LiveToolkit);
			EnsureScriptingTabSpawner(LiveToolkit->GetTabManager(), LiveToolkit);

			PlaceDrawer(LiveToolkit.ToSharedRef(), *Entry);
			PlaceScriptingDrawer(LiveToolkit.ToSharedRef(), *Entry);

			// The Widget Animation State Machine editor's drawer, right after Widget Animation Scripting - its tab spawner too.
			WidgetAnimStateMachineEditorHooks::PlaceDrawer(LiveToolkit.ToSharedRef(), WidgetAnimationScriptingDrawerSlot + 1);

			// The panels docked last time - in this session or an earlier one, in this mode - docked again, quietly. A moment
			// later, once the editor's layout (rebuilt with each switch between the Designer and the Graph) has its tabs.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakToolkit, PanelSource](float) -> bool
			{
				const TSharedPtr<FAssetEditorToolkit> RestoreToolkit = WeakToolkit.Pin();
				if (!RestoreToolkit.IsValid() || !RestoreToolkit->IsHosted())
				{
					return false;
				}
				if (WidgetAnimDockPreference::IsDockedIn(WidgetAnimDockPreference::WidgetAnimationsPanel, RestoreToolkit))
				{
					DockInLayout(WeakToolkit, PanelSource, /*bRestore=*/true);
				}
				if (WidgetAnimDockPreference::IsDockedIn(WidgetAnimDockPreference::ScriptingPanel, RestoreToolkit))
				{
					DockScriptingInLayout(WeakToolkit, /*bRestore=*/true);
				}
				WidgetAnimStateMachineEditorHooks::RestoreDock(RestoreToolkit.ToSharedRef());
				return false;
			}), 0.3f);
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

const FName FWidgetAnimBuilderToolbar::SelectedWidgetSectionName(TEXT("WidgetAnimPreview"));

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
				TabFactories.RegisterFactory(MakeShared<FWidgetAnimationScriptingTabSummoner>(ApplicationMode.GetBlueprintEditor()));
				WidgetAnimStateMachineEditorHooks::RegisterTabFactory(ApplicationMode, TabFactories);
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

				// Widget Animation Scripting's slot: in the Details panel's well.
				Extender.ExtendLayout(FTabId(ScriptingLayoutBesideTabId), ELayoutExtensionPosition::After, FTabManager::FTab(FTabId(WidgetAnimationScriptingTabId), ETabState::ClosedTab));

				// Widget Animation State Machine's slot: in the bottom well, after Animations.
				WidgetAnimStateMachineEditorHooks::ExtendLayout(Extender);
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

	FToolMenuSection& Section = Menu->FindOrAddSection(SelectedWidgetSectionName);

	// Dynamic, because it runs once per asset editor window and the context that says which asset that window
	// is editing only exists at that point. It adds no buttons - the toolbar stays as UMG built it - it is
	// only the per-editor hook the Widget Animations drawer and tab are registered from.
	Section.AddDynamicEntry("WidgetAnimPreviewControls", FNewToolMenuSectionDelegate::CreateStatic(&BuildEntries));
}

TSharedPtr<SDockTab> FWidgetAnimBuilderToolbar::DockScripting(const UWidgetBlueprint* WidgetBlueprint)
{
	const TSharedPtr<FAssetEditorToolkit> Toolkit = FindToolkitFor(WidgetBlueprint);
	return Toolkit.IsValid() ? DockScriptingInLayout(Toolkit, /*bRestore=*/false) : nullptr;
}

bool FWidgetAnimBuilderToolbar::IsScriptingDocked(const UWidgetBlueprint* WidgetBlueprint)
{
	const TSharedPtr<FAssetEditorToolkit> Toolkit = FindToolkitFor(WidgetBlueprint);
	return Toolkit.IsValid() && FindLiveScriptingTab(Toolkit->GetTabManager()).IsValid();
}

bool FWidgetAnimBuilderToolbar::OpenAnimationInPanel(const UWidgetBlueprint* WidgetBlueprint, UWidgetAnimPresetAsset* Animation)
{
	const TSharedPtr<FAssetEditorToolkit> Toolkit = FindToolkitFor(WidgetBlueprint);
	if (!Toolkit.IsValid() || !Animation)
	{
		return false;
	}

	const TSharedRef<IWidgetAnimPanelSource> PanelSource = GetPanelSource(Toolkit.ToSharedRef());

	// One of this Widget Blueprint's own timelines - a state of its own state machine can play one as it is - opens as that
	// timeline, in Animation Timeline.
	UWidgetBlueprint* TimelineBlueprint = nullptr;
	int32 TimelineIndex = INDEX_NONE;
	if (WidgetAnimTimelineAssets::FindTimeline(Animation, TimelineBlueprint, TimelineIndex))
	{
		const UWidgetAnimTimelinesExtension* Timelines = TimelineBlueprint == WidgetBlueprint ? UWidgetAnimTimelinesExtension::Find(TimelineBlueprint) : nullptr;
		if (!Timelines || !Timelines->Timelines.IsValidIndex(TimelineIndex))
		{
			return false;
		}
		PanelSource->SetMode(IWidgetAnimPanelSource::EMode::Timeline);
		if (!PanelSource->OpenTimeline(Timelines->Timelines[TimelineIndex].Name))
		{
			return false;
		}
		DockInLayout(Toolkit, PanelSource);
		return true;
	}

	// The panel edits the asset itself - Edit Existing Asset - and is docked, or brought forward if it is open already.
	PanelSource->SetMode(IWidgetAnimPanelSource::EMode::EditExistingAsset);
	PanelSource->SetAsset(Animation);
	if (PanelSource->GetAsset() != Animation)
	{
		// A builder's panel edits its own working copy, never an asset.
		return false;
	}
	DockInLayout(Toolkit, PanelSource);
	return true;
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
	WidgetAnimStateMachineEditorHooks::Shutdown();
}

#undef LOCTEXT_NAMESPACE