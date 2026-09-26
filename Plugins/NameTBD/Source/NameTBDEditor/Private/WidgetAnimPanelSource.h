// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"

class UUserWidget;
class UWidget;
class UWidgetAnimBuilder;
class UWidgetAnimPresetAsset;
class UWidgetBlueprint;

/**
 * What the Widget Animations panel edits, and how it plays it.
 *
 * Two kinds, one panel:
 *
 *  - FWidgetAnimBuilderPanelSource, in a WidgetAnimationBuilder's editor: the builder's own working copy on
 *    its Class Defaults, saved as a new WA_ asset - or an existing asset, edited in place. The place for
 *    generic animations meant for many widgets.
 *
 *  - FWidgetAnimAssetPanelSource, in every other Widget Blueprint's editor: a new animation, or a WA_ asset
 *    edited directly, each Layer Group playing on a widget of this Widget Blueprint - its Preview Target -
 *    in the designer. The place for an animation that moves several parts of one sophisticated widget.
 *
 * The panel only talks to this interface, so layers, groups, keys, the transport and the Selection panel
 * behave the same in both.
 */
class IWidgetAnimPanelSource : public TSharedFromThis<IWidgetAnimPanelSource>
{
public:

	virtual ~IWidgetAnimPanelSource() = default;

	// ---------------------------------------------------------------------------------------------
	// The animation
	// ---------------------------------------------------------------------------------------------

	/** The object holding the LayerGroups being edited - Modify()'d for every edit, and what the Selection panel shows. Null when there is none. */
	virtual UObject* GetEditedObject() const = 0;

	/** The groups on screen. Editable ones, or - for a builder previewing an asset - that asset's, read-only. */
	virtual TArray<FWidgetAnimLayerGroup>* GetGroups() const = 0;

	/** The layers on screen, in sequence, each naming its group. From the same object as GetGroups. */
	virtual TArray<FWidgetAnimLayer>* GetLayers() const = 0;

	/** Whether the groups on screen can be edited here. Says nothing of the panel's lock - see IsReadOnly. */
	virtual bool CanEdit() const = 0;

	/**
	 * The panel's lock: the animation is shown and played but not edited - a read-only preview. Kept here rather
	 * than on the panel, so the drawer and the docked tab of one editor are always locked or unlocked together.
	 */
	bool IsReadOnly() const { return bReadOnly; }
	void SetReadOnly(bool bInReadOnly) { bReadOnly = bInReadOnly; }

	/** After an edit made outside the property system: keys brought in step with curves, previews refreshed. */
	virtual void NotifyGroupsChanged(EPropertyChangeType::Type ChangeType) = 0;

	/** Names of the LayerGroups and Layers properties on GetEditedObject's class - the same on a builder and an asset. */
	static FName GetGroupsPropertyName() { return TEXT("LayerGroups"); }
	static FName GetLayersPropertyName() { return TEXT("Layers"); }

	// ---------------------------------------------------------------------------------------------
	// The widget it plays on
	// ---------------------------------------------------------------------------------------------

	/** The designer's preview of this Widget Blueprint, whose tree Preview Targets are paths into. Null while there is none. */
	virtual UUserWidget* GetPreviewRoot() const = 0;

	/** Every widget a group can target, as paths, "None" first. */
	TArray<FString> GetTargetOptions() const;

	/** The widget a group's Preview Target resolves to on the preview, for labelling. Null for "the widget itself". */
	UWidget* ResolvePreviewTarget(FName Path) const;

	// ---------------------------------------------------------------------------------------------
	// Playing it
	// ---------------------------------------------------------------------------------------------

	/** Where the preview is, as a fraction of one pass. */
	virtual float GetPlayheadAlpha() const = 0;

	/** Freeze the preview at Alpha of one pass. */
	virtual void ScrubToAlpha(float Alpha) = 0;

	/** Freeze the preview on key KeyIndex of flattened layer FlatLayerIndex (groups in order, then layers). */
	virtual void FocusKey(int32 FlatLayerIndex, int32 KeyIndex) = 0;

	virtual bool IsPlaying() const = 0;

	/** Play forward, first frame to last - or freeze, if playing. */
	virtual void TogglePlay() = 0;

	virtual bool GetLoop() const = 0;
	virtual void SetLoop(bool bValue) = 0;
	virtual bool GetPingPong() const = 0;
	virtual void SetPingPong(bool bValue) = 0;
	virtual bool GetInvert() const = 0;
	virtual void SetInvert(bool bValue) = 0;
	virtual float GetSpeed() const = 0;
	virtual void SetSpeed(float Speed) = 0;

	/** Called by the panel every frame it is on screen. A source that drives the preview itself uses it to know it's wanted. */
	virtual void NotifyPanelActive() {}

	// ---------------------------------------------------------------------------------------------
	// New animation, or an existing asset
	// ---------------------------------------------------------------------------------------------

	/**
	 * What the panel is working on. New Animation is a working copy - the builder's own layers, or in any
	 * other Widget Blueprint a scratch animation kept for the session - saved with Save as Animation Asset.
	 * Edit Existing Asset edits the picked WA_ asset in place; Update Animation Asset saves it.
	 */
	enum class EMode : uint8
	{
		NewAnimation,
		EditExistingAsset
	};

	virtual EMode GetMode() const = 0;
	virtual void SetMode(EMode Mode) = 0;

	/** The asset being edited in Edit Existing Asset - null until one is picked, and in New Animation. */
	virtual UWidgetAnimPresetAsset* GetAsset() const = 0;
	virtual void SetAsset(UWidgetAnimPresetAsset* Asset) = 0;

	/** New Animation: saves the working copy as a new WA_ asset and switches to editing that asset. Null if nothing was saved. */
	virtual UWidgetAnimPresetAsset* SaveAsNewAsset() = 0;

	/** Edit Existing Asset: saves the asset's changes to disk. */
	void UpdateAsset();

	/**
	 * The edited animation's Animation Speed Multiplier - how much faster than authored it plays wherever it is
	 * used. The panel shows time as played, so everything on the timeline is divided by it.
	 */
	float GetAnimationSpeedMultiplier() const;

	/** Whether there is something to save as a new asset, or unsaved changes to update the asset with. */
	bool CanSaveAsNewAsset() const;
	bool CanUpdateAsset() const;

	virtual bool IsBuilder() const { return false; }
	virtual UWidgetAnimBuilder* GetBuilder() const { return nullptr; }

private:

	bool bReadOnly = false;
};

/**
 * The designer's preview of a Widget Blueprint: a live instance of exactly its class, at design time, that is
 * not sitting inside some other widget's tree. Looking is a walk over every User Widget, so callers cache it.
 */
UUserWidget* FindWidgetAnimDesignerPreview(const UWidgetBlueprint* Blueprint);

/**
 * In a WidgetAnimationBuilder's editor: the builder's working copy - or, while its Preview Animation is set,
 * that asset, edited in place - played by the builder's own designer preview. Opening a WA_ asset's Edit in
 * Widget Animation Builder lands here, editing it.
 */
class FWidgetAnimBuilderPanelSource : public IWidgetAnimPanelSource
{
public:

	explicit FWidgetAnimBuilderPanelSource(TWeakObjectPtr<UWidgetBlueprint> InBlueprint);

	virtual UObject* GetEditedObject() const override;
	virtual TArray<FWidgetAnimLayerGroup>* GetGroups() const override;
	virtual TArray<FWidgetAnimLayer>* GetLayers() const override;
	virtual bool CanEdit() const override;
	virtual void NotifyGroupsChanged(EPropertyChangeType::Type ChangeType) override;
	virtual UUserWidget* GetPreviewRoot() const override;

	virtual float GetPlayheadAlpha() const override;
	virtual void ScrubToAlpha(float Alpha) override;
	virtual void FocusKey(int32 FlatLayerIndex, int32 KeyIndex) override;
	virtual bool IsPlaying() const override;
	virtual void TogglePlay() override;
	virtual bool GetLoop() const override;
	virtual void SetLoop(bool bValue) override;
	virtual bool GetPingPong() const override;
	virtual void SetPingPong(bool bValue) override;
	virtual bool GetInvert() const override;
	virtual void SetInvert(bool bValue) override;
	virtual float GetSpeed() const override;
	virtual void SetSpeed(float Speed) override;

	virtual EMode GetMode() const override;
	virtual void SetMode(EMode Mode) override;

	/** The asset being edited: the builder's Preview Animation, which View Animation Preview on an asset also sets. */
	virtual UWidgetAnimPresetAsset* GetAsset() const override;
	virtual void SetAsset(UWidgetAnimPresetAsset* Asset) override;

	/** The builder's Create: Animation Name and Save Folder decide where it goes. */
	virtual UWidgetAnimPresetAsset* SaveAsNewAsset() override;

	virtual bool IsBuilder() const override { return true; }
	virtual UWidgetAnimBuilder* GetBuilder() const override;

private:

	UClass* GetBuilderClass() const;

	/** Sets the builder's Preview Animation - the asset being edited, or none - and tells every live preview. */
	void SetPreviewAnimation(UWidgetAnimPresetAsset* Asset);

	/** Edit Existing Asset picked, before an asset has been. The builder only records the asset itself. */
	bool bEditExistingPicked = false;

	/** Through the live previews when there are any - their setters write the Class Defaults too - or straight onto the Class Defaults. */
	void ApplyToBuilders(TFunctionRef<void(UWidgetAnimBuilder&)> Apply);

	TWeakObjectPtr<UWidgetBlueprint> Blueprint;
};

/**
 * In any other Widget Blueprint's editor: a new animation built on a working copy kept for the session and
 * saved as a WA_ asset, or an existing WA_ asset edited directly - either played on the designer's preview
 * of this Widget Blueprint with each group on its Preview Target.
 *
 * Plays it itself - there is no builder here to do it. Every frame the panel is on screen, the animation is
 * rebuilt from the asset and posed at the playhead; when the panel goes away (the drawer closed, the tab
 * hidden) every widget it moved is put back as authored within a few frames, so nothing is left posed
 * while the widget is being designed.
 */
class FWidgetAnimAssetPanelSource : public IWidgetAnimPanelSource
{
public:

	explicit FWidgetAnimAssetPanelSource(TWeakObjectPtr<UWidgetBlueprint> InBlueprint);
	virtual ~FWidgetAnimAssetPanelSource() override;

	virtual UObject* GetEditedObject() const override;
	virtual TArray<FWidgetAnimLayerGroup>* GetGroups() const override;
	virtual TArray<FWidgetAnimLayer>* GetLayers() const override;
	virtual bool CanEdit() const override;
	virtual void NotifyGroupsChanged(EPropertyChangeType::Type ChangeType) override;
	virtual UUserWidget* GetPreviewRoot() const override;

	virtual float GetPlayheadAlpha() const override;
	virtual void ScrubToAlpha(float Alpha) override;
	virtual void FocusKey(int32 FlatLayerIndex, int32 KeyIndex) override;
	virtual bool IsPlaying() const override { return bPlaying; }
	virtual void TogglePlay() override;
	virtual bool GetLoop() const override { return bLoop; }
	virtual void SetLoop(bool bValue) override { bLoop = bValue; }
	virtual bool GetPingPong() const override { return bPingPong; }
	virtual void SetPingPong(bool bValue) override { bPingPong = bValue; }
	virtual bool GetInvert() const override { return bInvert; }
	virtual void SetInvert(bool bValue) override { bInvert = bValue; }
	virtual float GetSpeed() const override { return Speed; }
	virtual void SetSpeed(float InSpeed) override { Speed = FMath::Max(InSpeed, 0.01f); }

	virtual void NotifyPanelActive() override;

	virtual EMode GetMode() const override { return Mode; }
	virtual void SetMode(EMode InMode) override;
	virtual UWidgetAnimPresetAsset* GetAsset() const override { return Mode == EMode::EditExistingAsset ? Asset.Get() : nullptr; }
	virtual void SetAsset(UWidgetAnimPresetAsset* InAsset) override;
	virtual UWidgetAnimPresetAsset* SaveAsNewAsset() override;

private:

	/** The ticker: advances the clock and poses the preview while the panel is showing, restores it once it isn't. */
	bool Tick(float DeltaTime);

	/** The animation being worked on: the working copy in New Animation, the picked asset in Edit Existing Asset. */
	UWidgetAnimPresetAsset* GetEditedAnimation() const;

	/** This Widget Blueprint's working copy for New Animation, made on first use and kept for the session. */
	UWidgetAnimPresetAsset* GetWorkingCopy() const;

	/** The animation as settings for this Widget Blueprint's preview: each group on its Preview Target. Invert stamped on, loops left to the clock here. */
	void BuildFlattened(UUserWidget* Root, TArray<FWidgetAnimSettings>& OutSettings) const;

	/** Every widget moved put back as authored, and forgotten. */
	void RestorePreview();

	TWeakObjectPtr<UWidgetBlueprint> Blueprint;
	TWeakObjectPtr<UWidgetAnimPresetAsset> Asset;
	EMode Mode = EMode::NewAnimation;

	/** The designer preview found last, and when it was last looked for - looking is a walk over every User Widget, so it isn't done every frame. */
	mutable TWeakObjectPtr<UUserWidget> CachedRoot;
	mutable double LastRootSearchTime = -1.0;

	/** The root the baselines were captured on, and what they were captured for; either changing means capture again. */
	TWeakObjectPtr<UUserWidget> PosedRoot;
	uint32 PosedSignature = 0;
	FWidgetAnimBaselineSet Baselines;

	FTSTicker::FDelegateHandle TickerHandle;
	double LastPanelActiveTime = -1.0;

	float Alpha = 0.f;
	bool bPlaying = false;
	bool bPlayingForward = true;
	bool bLoop = false;
	bool bPingPong = true;
	bool bInvert = false;
	float Speed = 1.f;
};