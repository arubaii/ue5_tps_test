// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "Containers/Ticker.h"
#include "Editor.h"
#include "Misc/ConfigCacheIni.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "WidgetBlueprintEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Whether the user docked a panel - Widget Animations, Widget Animation Scripting, Widget Animation State Machine - kept in
 * the per-project editor settings (EditorPerProjectUserSettings.ini, [NameTBD.DockedPanels]), so every Widget Blueprint
 * editor opened after, in this session or the next, opens with it docked again. Dock in Layout sets it, with the editor mode
 * it was docked in (Designer or Graph); closing the docked tab by hand clears it. A tab taken down by its editor closing, the
 * editor quitting, or a switch between the Designer and the Graph (which lays the editor out afresh) leaves it as it was.
 *
 * Also: unique placeholder ids for tabs about to be put into a well (set through FDockTabLayoutIdAccess - the setter is protected). FTabManager::InsertNewDocumentTab ensures that no live
 * tab matches the new tab's layout id before giving it one - and a freshly made SDockTab's is empty, which matched whatever
 * else in the editor had none: the "! FindTabInLiveAreas(...)" ensure, and the hitch its callstack capture caused.
 */
namespace WidgetAnimDockPreference
{
	inline const TCHAR* const ConfigSection = TEXT("NameTBD.DockedPanels");

	/** The panels' keys in the settings. */
	inline const TCHAR* const WidgetAnimationsPanel = TEXT("WidgetAnimations");
	inline const TCHAR* const ScriptingPanel = TEXT("WidgetAnimationScripting");
	inline const TCHAR* const StateMachinePanel = TEXT("WidgetAnimationStateMachine");

	/** The mode a Widget Blueprint editor is in - Designer or Graph - or None for any other editor. */
	inline FName GetEditorMode(const TSharedPtr<FAssetEditorToolkit>& Toolkit)
	{
		static const FName WidgetBlueprintEditorName(TEXT("WidgetBlueprintEditor"));
		if (!Toolkit.IsValid() || Toolkit->GetToolkitFName() != WidgetBlueprintEditorName)
		{
			return NAME_None;
		}
		return static_cast<FWidgetBlueprintEditor*>(Toolkit.Get())->GetCurrentMode();
	}

	/** The mode Panel was docked in, or None when it isn't docked. */
	inline FName GetDockedMode(const TCHAR* Panel)
	{
		FString Mode;
		if (!GConfig || !GConfig->GetString(ConfigSection, Panel, Mode, GEditorPerProjectIni) || Mode.IsEmpty())
		{
			return NAME_None;
		}
		return FName(*Mode);
	}

	/** Whether Panel was docked in the mode this editor is in now - what an editor restores as it opens or comes back to that mode. */
	inline bool IsDockedIn(const TCHAR* Panel, const TSharedPtr<FAssetEditorToolkit>& Toolkit)
	{
		const FName Mode = GetDockedMode(Panel);
		return !Mode.IsNone() && Mode == GetEditorMode(Toolkit);
	}

	/** Docked in Mode, or (None) not docked any more. Written at once, so a crash doesn't lose it. */
	inline void SetDocked(const TCHAR* Panel, FName Mode)
	{
		if (!GConfig || GetDockedMode(Panel) == Mode)
		{
			return;
		}
		GConfig->SetString(ConfigSection, Panel, Mode.IsNone() ? TEXT("") : *Mode.ToString(), GEditorPerProjectIni);
		GConfig->Flush(/*bRemoveFromCache=*/false, GEditorPerProjectIni);
	}

	/** Whether the editor is still open - the asset editors still list it for what it edits. */
	inline bool IsEditorOpen(const TSharedPtr<FAssetEditorToolkit>& Toolkit)
	{
		const TArray<UObject*>* Edited = Toolkit.IsValid() ? Toolkit->GetObjectsCurrentlyBeingEdited() : nullptr;
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Edited || Edited->Num() == 0 || !(*Edited)[0] || !AssetEditors)
		{
			return false;
		}
		const IAssetEditorInstance* Instance = AssetEditors->FindEditorForAsset((*Edited)[0], /*bFocusIfOpen=*/false);
		return Instance == static_cast<const IAssetEditorInstance*>(Toolkit.Get());
	}

	/** A layout id no live tab has: what a tab is given right before it is put into a well. */
	inline FTabId MakePendingTabId()
	{
		static int32 Counter = 0;
		return FTabId(FName(TEXT("NameTBD.PendingDock"), ++Counter));
	}

	/**
	 * Reaches SDockTab::SetLayoutIdentifier, which is protected: a pointer to it is taken through a class deriving from
	 * SDockTab (which may name it), and called on the tab. Never made - only its static function is used.
	 */
	struct FDockTabLayoutIdAccess : public SDockTab
	{
		static void SetLayoutId(SDockTab& Tab, const FTabId& NewId)
		{
			void (SDockTab::*Setter)(const FTabId&) = &FDockTabLayoutIdAccess::SetLayoutIdentifier;
			(Tab.*Setter)(NewId);
		}
	};

	/** Puts Tab into a well beside another under PlaceholderId - with a placeholder id first, so the tab manager's check can't trip. */
	inline void InsertDocumentTab(FTabManager& TabManager, FName PlaceholderId, const FTabManager::FSearchPreference& SearchPreference, const TSharedRef<SDockTab>& Tab)
	{
		FDockTabLayoutIdAccess::SetLayoutId(*Tab, MakePendingTabId());
		TabManager.InsertNewDocumentTab(PlaceholderId, SearchPreference, Tab);
	}

	/**
	 * Wraps a panel's tab body: when the body goes - its tab closed - and the editor is still open, in the same mode, with no
	 * tab of the panel open again (Dock in Layout replacing a floating one), it was closed by hand: the panel isn't docked any
	 * more. Worked out a moment later, once whatever closed it is done.
	 */
	class SDockWatch : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDockWatch) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TCHAR* InPanel, const TWeakPtr<FAssetEditorToolkit>& InToolkit, TFunction<bool()> InIsPanelOpen)
		{
			Panel = InPanel;
			WatchedToolkit = InToolkit;
			IsPanelOpen = MoveTemp(InIsPanelOpen);
			ModeAtOpen = GetEditorMode(InToolkit.Pin());
			ChildSlot
			[
				InArgs._Content.Widget
			];
		}

		virtual ~SDockWatch() override
		{
			if (IsEngineExitRequested() || !Panel)
			{
				return;
			}
			const TCHAR* const ClosedPanel = Panel;
			const TWeakPtr<FAssetEditorToolkit> WeakToolkit = WatchedToolkit;
			const FName Mode = ModeAtOpen;
			TFunction<bool()> StillOpen = MoveTemp(IsPanelOpen);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([ClosedPanel, WeakToolkit, Mode, StillOpen](float) -> bool
			{
				const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
				if (IsEngineExitRequested() || !IsEditorOpen(Toolkit) || GetEditorMode(Toolkit) != Mode || (StillOpen && StillOpen()))
				{
					return false;
				}
				if (GetDockedMode(ClosedPanel) == Mode)
				{
					SetDocked(ClosedPanel, NAME_None);
				}
				return false;
			}), 0.25f);
		}

	private:
		const TCHAR* Panel = nullptr;
		TWeakPtr<FAssetEditorToolkit> WatchedToolkit;
		TFunction<bool()> IsPanelOpen;
		FName ModeAtOpen;
	};

	inline TSharedRef<SWidget> Watch(const TCHAR* Panel, const TWeakPtr<FAssetEditorToolkit>& Toolkit, TFunction<bool()> IsPanelOpen, const TSharedRef<SWidget>& Body)
	{
		return SNew(SDockWatch, Panel, Toolkit, MoveTemp(IsPanelOpen))
			[
				Body
			];
	}
}
