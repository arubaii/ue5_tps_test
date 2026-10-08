// Copyright AR GameSystems 2026. All rights reserved.

#include "StyledButtonsEditor.h"

#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Widgets/StyledButtons.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "StyledButtonsEditor"

namespace StyledButtonsEditorPrivate
{
	FDelegateHandle PreCompileHandle;
	FDelegateHandle AssetLoadedHandle;

	/** Blueprints whose leftover parts are swept on the next tick - never during their compile. */
	TArray<TWeakObjectPtr<UWidgetBlueprint>> PendingSweeps;
	bool bSweepScheduled = false;

	/** Blueprints that may hold dropped ready-made buttons waiting for their parts. */
	TArray<TWeakObjectPtr<UWidgetBlueprint>> PendingBuilds;
	bool bBuildScheduled = false;

	/** How far up a widget's parents are followed, in case a broken tree ever loops. */
	constexpr int32 MaxDepth = 256;

	/**
	 * The Widget Blueprint's name-to-GUID map, read through reflection: whether a widget has its GUID, and giving it one, need
	 * no API that may differ between engine versions.
	 */
	const FMapProperty* GetGuidMapProperty()
	{
		static const FMapProperty* Property = FindFProperty<FMapProperty>(UWidgetBlueprint::StaticClass(), TEXT("WidgetVariableNameToGuidMap"));
		return Property;
	}

	/** Gives Name a GUID in the blueprint's map if it has none. True if it added one. */
	bool EnsureGuid(UWidgetBlueprint* Blueprint, FName Name)
	{
		const FMapProperty* MapProperty = GetGuidMapProperty();
		if (!Blueprint || !MapProperty || Name.IsNone())
		{
			return false;
		}
		FScriptMapHelper Map(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Blueprint));
		if (Map.FindMapIndexWithKey(&Name) != INDEX_NONE)
		{
			return false;
		}
		const FGuid NewGuid = FGuid::NewGuid();
		Map.AddPair(&Name, &NewGuid);
		return true;
	}

	/** Adds the hidden parts of Context Menu Buttons among Widgets, and everything inside. */
	void AddHiddenParts(const TArray<UWidget*>& Widgets, TSet<UWidget*>& OutLive)
	{
		for (UWidget* Widget : Widgets)
		{
			const UContextMenuButton* Button = Cast<UContextMenuButton>(Widget);
			if (UWidget* Hidden = Button ? Button->GetHiddenContent() : nullptr)
			{
				OutLive.Add(Hidden);
				UWidgetTree::ForWidgetAndChildren(Hidden, [&OutLive](UWidget* Part)
				{
					OutLive.Add(Part);
				});
			}
		}
	}

	/** The widgets the hierarchy lists: under the tree's root and in its named slot bindings (read through reflection). */
	void GatherListedWidgets(UWidgetTree* Tree, TSet<UWidget*>& OutLive)
	{
		Tree->ForEachWidget([&OutLive](UWidget* Widget)
		{
			OutLive.Add(Widget);
		});

		static const FMapProperty* BindingsProperty = FindFProperty<FMapProperty>(UWidgetTree::StaticClass(), TEXT("NamedSlotBindings"));
		const FObjectPropertyBase* ValueProperty = BindingsProperty ? CastField<FObjectPropertyBase>(BindingsProperty->ValueProp) : nullptr;
		if (ValueProperty)
		{
			FScriptMapHelper Bindings(BindingsProperty, BindingsProperty->ContainerPtrToValuePtr<void>(Tree));
			for (int32 Index = 0; Index < Bindings.GetMaxIndex(); ++Index)
			{
				if (!Bindings.IsValidIndex(Index))
				{
					continue;
				}
				if (UWidget* Bound = Cast<UWidget>(ValueProperty->GetObjectPropertyValue(Bindings.GetValuePtr(Index))))
				{
					OutLive.Add(Bound);
					UWidgetTree::ForWidgetAndChildren(Bound, [&OutLive](UWidget* Widget)
					{
						OutLive.Add(Widget);
					});
				}
			}
		}
	}

	/** Every widget the tree really holds: the listed ones, and the parts buttons hide from the Hierarchy. */
	void GatherLiveWidgets(UWidgetTree* Tree, TSet<UWidget*>& OutLive)
	{
		GatherListedWidgets(Tree, OutLive);
		AddHiddenParts(OutLive.Array(), OutLive);
	}

	/**
	 * The ready-made button Widget is part of, following its parents (null when it isn't part of one), and the widget just
	 * below that button on the way - the root of the parts Widget belongs to.
	 */
	UStyledButtonBase* FindOwningReadyMade(UWidget* Widget, UWidget*& OutPartsRoot)
	{
		OutPartsRoot = Widget;
		UPanelWidget* Parent = Widget ? Widget->GetParent() : nullptr;
		for (int32 Depth = 0; Parent && Depth < MaxDepth; ++Depth)
		{
			if (UStyledButtonBase* Button = Cast<UStyledButtonBase>(Parent))
			{
				return Button;
			}
			OutPartsRoot = Parent;
			Parent = Parent->GetParent();
		}
		return nullptr;
	}

	/** Every widget made in the tree (its direct inners), listed or not - what the Widget Blueprint compiler wants GUIDs for. */
	void GatherTreeWidgets(UWidgetTree* Tree, TArray<UWidget*>& OutWidgets)
	{
		ForEachObjectWithOuter(Tree, [&OutWidgets](UObject* Object)
		{
			if (UWidget* Widget = Cast<UWidget>(Object))
			{
				if (IsValid(Widget))
				{
					OutWidgets.Add(Widget);
				}
			}
		}, /*bIncludeNestedObjects=*/false);
	}

	/**
	 * Makes the blueprint's GUID map match the widgets its tree owns - which the Widget Blueprint compiler checks both ways, an
	 * ensure with a full callstack per mismatch: every widget the tree owns, listed in the Hierarchy or not, has a GUID ("was
	 * added but did not get a GUID"), and every GUID names one ("was deleted but still has a GUID"). "Owns" is the compiler's
	 * own rule (UWidgetBlueprint::ForEachSourceWidget): every widget inside the tree whose nearest widget tree is this one - so
	 * not the parts a button hides in a tree of its own. Only for blueprints holding a Context Menu Button; a no-op where the
	 * designer keeps it right itself. Run before every compile, and before each structural change made here. True when it
	 * changed the map.
	 */
	bool SyncGuids(UWidgetBlueprint* Blueprint)
	{
		UWidgetTree* Tree = Blueprint ? Blueprint->WidgetTree.Get() : nullptr;
		const FMapProperty* MapProperty = GetGuidMapProperty();
		if (!Tree || !MapProperty)
		{
			return false;
		}
		// Every widget the tree owns, as the compiler counts them (garbage not yet collected included).
		TArray<UWidget*> Widgets;
		ForEachObjectWithOuter(Tree, [&Widgets, Tree](UObject* Object)
		{
			UWidget* Widget = Cast<UWidget>(Object);
			if (Widget && Widget->GetTypedOuter<UWidgetTree>() == Tree)
			{
				Widgets.Add(Widget);
			}
		}, /*bIncludeNestedObjects=*/true);
		if (!Widgets.ContainsByPredicate([](const UWidget* Candidate) { return Candidate->IsA<UContextMenuButton>(); }))
		{
			return false;
		}

		bool bChanged = false;
		TSet<FName> WidgetNames;
		for (const UWidget* Widget : Widgets)
		{
			WidgetNames.Add(Widget->GetFName());
			bChanged |= EnsureGuid(Blueprint, Widget->GetFName());
		}

		FScriptMapHelper Map(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Blueprint));
		TArray<FName> Stale;
		for (int32 Index = 0; Index < Map.GetMaxIndex(); ++Index)
		{
			if (Map.IsValidIndex(Index))
			{
				const FName Key = *reinterpret_cast<const FName*>(Map.GetKeyPtr(Index));
				if (!WidgetNames.Contains(Key))
				{
					Stale.Add(Key);
				}
			}
		}
		for (const FName StaleKey : Stale)
		{
			Map.RemovePair(&StaleKey);
		}
		bChanged |= Stale.Num() > 0;

		if (bChanged)
		{
			Blueprint->MarkPackageDirty();
		}
		return bChanged;
	}

	/**
	 * Takes out of the tree the ready-made buttons' parts nothing holds any more:
	 *   - parts of a button the designer itself has moved out of the tree (a discarded drop preview, a deleted button);
	 *   - parts a button holding other content of its own no longer holds (left behind by the fix6-fix7 hidden parts).
	 * A button still in the tree without content (an undone build that a redo may bring back) keeps everything.
	 */
	void SweepLeftoverParts(UWidgetBlueprint* Blueprint)
	{
		UWidgetTree* Tree = Blueprint ? Blueprint->WidgetTree.Get() : nullptr;
		if (!Tree)
		{
			return;
		}

		TSet<UWidget*> Live;
		GatherLiveWidgets(Tree, Live);

		TArray<UWidget*> Widgets;
		GatherTreeWidgets(Tree, Widgets);

		TArray<UWidget*> Leftovers;
		for (UWidget* Widget : Widgets)
		{
			if (Live.Contains(Widget))
			{
				continue;
			}
			UWidget* PartsRoot = nullptr;
			const UStyledButtonBase* Owner = FindOwningReadyMade(Widget, PartsRoot);
			if (!Owner)
			{
				continue;
			}
			const bool bOwnerGone = !Live.Contains(Owner) && Owner->GetOuter() != Tree;
			const bool bReplaced = Live.Contains(Owner) && Owner->GetChildrenCount() > 0 && Owner->GetChildAt(0) != PartsRoot
				&& Owner->GetHiddenContent() != PartsRoot;
			if (bOwnerGone || bReplaced)
			{
				Leftovers.Add(Widget);
			}
		}

		for (UWidget* Leftover : Leftovers)
		{
			Leftover->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			Leftover->MarkAsGarbage();
		}
		if (Leftovers.Num() > 0)
		{
			SyncGuids(Blueprint);
			Blueprint->MarkPackageDirty();
			UE_LOG(LogTemp, Log, TEXT("NameTBD: removed %d leftover ready-made button part(s) from %s."), Leftovers.Num(), *Blueprint->GetName());
		}
	}

	void ScheduleSweep(UWidgetBlueprint* Blueprint)
	{
		PendingSweeps.AddUnique(Blueprint);
		if (bSweepScheduled)
		{
			return;
		}
		bSweepScheduled = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			bSweepScheduled = false;
			TArray<TWeakObjectPtr<UWidgetBlueprint>> Sweeps = MoveTemp(PendingSweeps);
			PendingSweeps.Reset();
			for (const TWeakObjectPtr<UWidgetBlueprint>& WeakBlueprint : Sweeps)
			{
				if (UWidgetBlueprint* Blueprint = WeakBlueprint.Get())
				{
					// Never in the middle of a compile.
					if (!Blueprint->bBeingCompiled)
					{
						SweepLeftoverParts(Blueprint);
					}
				}
			}
			return false;
		}));
	}

	/**
	 * Builds the parts of every ready-made button of the blueprint's hierarchy still waiting for them (one undoable step), and
	 * puts back any legacy hidden parts; then syncs the GUIDs and marks the blueprint structurally modified - only when
	 * something changed, so it never feeds itself. Works on the Widget Blueprint's own tree - never on the designer's preview
	 * copies, which is what the Details panel shows. True when it couldn't run yet (a compile in progress).
	 */
	bool BuildPendingInBlueprint(UWidgetBlueprint* Blueprint)
	{
		UWidgetTree* Tree = Blueprint ? Blueprint->WidgetTree.Get() : nullptr;
		if (!Tree)
		{
			return false;
		}
		if (Blueprint->bBeingCompiled)
		{
			return true;
		}

		// Only buttons really in the hierarchy: a drop preview that was discarded isn't.
		TSet<UWidget*> Listed;
		GatherListedWidgets(Tree, Listed);

		bool bChanged = false;
		TArray<UContextMenuButton*> Waiting;
		for (UWidget* Widget : Listed)
		{
			UContextMenuButton* Button = Cast<UContextMenuButton>(Widget);
			if (!Button || Button->GetOuter() != Tree)
			{
				continue;
			}

			// Hidden parts checked: a button's own are kept, a pasted copy's (still the original's) or fix6-fix7's are put back
			// or rebuilt - see RestoreHiddenParts.
			if (Button->HasHiddenParts())
			{
				bChanged |= Button->RestoreHiddenParts();
			}

			// A ready-made button made before parts could be hidden (or whose hidden parts were just put back) hides them, once;
			// from then on it is the Show / Hide Parts choice. Outside any transaction: an upgrade, not an edit to undo.
			if (Button->IsA<UStyledButtonBase>() && !Button->IsPartsVisibilityDecided() && !Button->HasPendingDefaultContent())
			{
				TArray<UWidget*> Moved;
				bChanged |= Button->SetPartsHidden(true, Moved);
				Button->MarkPartsVisibilityDecided();
			}

			if (Button->HasPendingDefaultContent())
			{
				Waiting.Add(Button);
			}
		}

		// Taken before the transaction opens: undoing the build then leaves a button empty instead of building it again.
		for (UContextMenuButton* Button : Waiting)
		{
			Button->ConsumePendingDefaultContent();
		}

		if (Waiting.Num() > 0)
		{
			const FScopedTransaction Transaction(LOCTEXT("BuildReadyMadeButton", "Build Ready-Made Button"));
			Blueprint->Modify();
			Tree->Modify();
			for (UContextMenuButton* Button : Waiting)
			{
				Button->Modify();
				TArray<UWidget*> Parts;
				Button->BuildDefaultContentForEditor(Parts);

				// A ready-made button's parts start hidden from the Hierarchy (Show Parts In Hierarchy, in Style, lists them).
				if (Parts.Num() > 0 && Button->IsA<UStyledButtonBase>())
				{
					TArray<UWidget*> Moved;
					Button->SetPartsHidden(true, Moved);
					Button->MarkPartsVisibilityDecided();
				}
			}
		}

		if (Waiting.Num() == 0 && !bChanged)
		{
			return false;
		}
		SyncGuids(Blueprint);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return false;
	}

	/**
	 * Builds pending buttons of Blueprint on a later tick: once no drag is in progress (a drag's drop previews are never built)
	 * and no compile is running.
	 */
	void ScheduleBuild(UWidgetBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return;
		}
		PendingBuilds.AddUnique(Blueprint);
		if (bBuildScheduled)
		{
			return;
		}
		bBuildScheduled = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			if (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping())
			{
				return true;
			}

			TArray<TWeakObjectPtr<UWidgetBlueprint>> Builds = MoveTemp(PendingBuilds);
			PendingBuilds.Reset();
			for (const TWeakObjectPtr<UWidgetBlueprint>& WeakBlueprint : Builds)
			{
				if (BuildPendingInBlueprint(WeakBlueprint.Get()))
				{
					PendingBuilds.AddUnique(WeakBlueprint);
				}
			}
			bBuildScheduled = PendingBuilds.Num() > 0;
			return bBuildScheduled;
		}));
	}

	/** The Widget Blueprint a button belongs to: its own outer, or - for the designer's preview copy - its generated class's. */
	UWidgetBlueprint* FindWidgetBlueprint(const UContextMenuButton* Button)
	{
		if (!Button)
		{
			return nullptr;
		}
		if (UWidgetBlueprint* Direct = Button->GetTypedOuter<UWidgetBlueprint>())
		{
			return Direct;
		}
#if WITH_EDITORONLY_DATA
		if (const UClass* GeneratedClass = Button->WidgetGeneratedByClass.Get())
		{
			return Cast<UWidgetBlueprint>(GeneratedClass->ClassGeneratedBy);
		}
#endif
		return nullptr;
	}

	/** The Widget Blueprint's own copy of Button (the Details panel shows the designer's preview copy), or null. */
	UContextMenuButton* FindTemplate(UContextMenuButton* Button, UWidgetBlueprint*& OutBlueprint)
	{
		OutBlueprint = FindWidgetBlueprint(Button);
		UWidgetTree* Tree = OutBlueprint ? OutBlueprint->WidgetTree.Get() : nullptr;
		if (!Tree)
		{
			return nullptr;
		}
		if (Button->GetOuter() == Tree)
		{
			return Button;
		}
		return Cast<UContextMenuButton>(Tree->FindWidget(Button->GetFName()));
	}

	/**
	 * Before a Widget Blueprint compiles: its GUIDs match its widgets. Just after: leftovers are swept, and a button dropped
	 * from the Palette (a drop always compiles) gets its parts.
	 */
	void HandleBlueprintPreCompile(UBlueprint* Blueprint)
	{
		UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return;
		}
		SyncGuids(WidgetBlueprint);
		ScheduleSweep(WidgetBlueprint);
		ScheduleBuild(WidgetBlueprint);
	}

	/** A Widget Blueprint just loaded: its GUIDs match its widgets before anything compiles it (one saved by fix6-fix7 may not). */
	void HandleAssetLoaded(UObject* Asset)
	{
		if (UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Asset))
		{
			SyncGuids(WidgetBlueprint);
		}
	}
}

namespace StyledButtonsEditor
{
	void Register()
	{
		using namespace StyledButtonsEditorPrivate;
		if (GEditor && !PreCompileHandle.IsValid())
		{
			PreCompileHandle = GEditor->OnBlueprintPreCompile().AddStatic(&HandleBlueprintPreCompile);
		}
		if (!AssetLoadedHandle.IsValid())
		{
			AssetLoadedHandle = FCoreUObjectDelegates::OnAssetLoaded.AddStatic(&HandleAssetLoaded);
		}
	}

	void Unregister()
	{
		using namespace StyledButtonsEditorPrivate;
		if (GEditor && PreCompileHandle.IsValid())
		{
			GEditor->OnBlueprintPreCompile().Remove(PreCompileHandle);
		}
		PreCompileHandle.Reset();
		FCoreUObjectDelegates::OnAssetLoaded.Remove(AssetLoadedHandle);
		AssetLoadedHandle.Reset();
		PendingSweeps.Reset();
		PendingBuilds.Reset();
	}

	bool ArePartsShown(const TArray<TWeakObjectPtr<UContextMenuButton>>& Buttons)
	{
		using namespace StyledButtonsEditorPrivate;
		for (const TWeakObjectPtr<UContextMenuButton>& WeakButton : Buttons)
		{
			UWidgetBlueprint* Blueprint = nullptr;
			const UContextMenuButton* Template = WeakButton.IsValid() ? FindTemplate(WeakButton.Get(), Blueprint) : nullptr;
			if (Template && Template->ArePartsHidden())
			{
				return false;
			}
		}
		return true;
	}

	bool CanToggleParts(const TArray<TWeakObjectPtr<UContextMenuButton>>& Buttons)
	{
		using namespace StyledButtonsEditorPrivate;
		for (const TWeakObjectPtr<UContextMenuButton>& WeakButton : Buttons)
		{
			UWidgetBlueprint* Blueprint = nullptr;
			const UContextMenuButton* Template = WeakButton.IsValid() ? FindTemplate(WeakButton.Get(), Blueprint) : nullptr;
			if (Template && (Template->GetChildrenCount() > 0 || Template->ArePartsHidden()) && !Blueprint->bBeingCompiled)
			{
				return true;
			}
		}
		return false;
	}

	void ToggleParts(const TArray<TWeakObjectPtr<UContextMenuButton>>& Buttons)
	{
		using namespace StyledButtonsEditorPrivate;

		// Shown if any is hidden, else all hidden - so a mixed selection ends up alike.
		const bool bShow = !ArePartsShown(Buttons);

		TArray<TPair<UWidgetBlueprint*, UContextMenuButton*>> Templates;
		for (const TWeakObjectPtr<UContextMenuButton>& WeakButton : Buttons)
		{
			UWidgetBlueprint* Blueprint = nullptr;
			UContextMenuButton* Template = WeakButton.IsValid() ? FindTemplate(WeakButton.Get(), Blueprint) : nullptr;
			if (Template && !Blueprint->bBeingCompiled)
			{
				Templates.AddUnique(TPair<UWidgetBlueprint*, UContextMenuButton*>(Blueprint, Template));
			}
		}

		TArray<UWidgetBlueprint*> Changed;
		{
			const FScopedTransaction Transaction(bShow
				? LOCTEXT("ShowParts", "Show Button Parts In Hierarchy")
				: LOCTEXT("HideParts", "Hide Button Parts From Hierarchy"));
			for (const TPair<UWidgetBlueprint*, UContextMenuButton*>& Entry : Templates)
			{
				UWidgetBlueprint* Blueprint = Entry.Key;
				UContextMenuButton* Template = Entry.Value;
				Blueprint->Modify();

				// The same widgets, moved between the Widget Blueprint's tree and the button's own: nothing on them changes.
				TArray<UWidget*> Moved;
				if (Template->SetPartsHidden(!bShow, Moved))
				{
					Template->MarkPartsVisibilityDecided();
					Changed.AddUnique(Blueprint);
				}
			}

			// Listed parts get their GUIDs, hidden ones give theirs up - inside the transaction, so undo puts both back.
			for (UWidgetBlueprint* Blueprint : Changed)
			{
				SyncGuids(Blueprint);
			}
		}
		for (UWidgetBlueprint* Blueprint : Changed)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
	}

	void OnButtonsShown(const TArray<TWeakObjectPtr<UContextMenuButton>>& Buttons)
	{
		using namespace StyledButtonsEditorPrivate;

		// The Details panel shows the designer's preview copy: the build and the sweep go to the Widget Blueprint behind it.
		for (const TWeakObjectPtr<UContextMenuButton>& WeakButton : Buttons)
		{
			if (UWidgetBlueprint* Blueprint = FindWidgetBlueprint(WeakButton.Get()))
			{
				ScheduleSweep(Blueprint);
				ScheduleBuild(Blueprint);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
