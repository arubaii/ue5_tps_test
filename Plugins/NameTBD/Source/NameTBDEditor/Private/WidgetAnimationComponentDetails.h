// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

/**
 * The Details of a Widget Animation Component: its Animation settings in their usual order, with a compact track
 * editor under Additive Anim Layers In and under Additive Anim Layers Out.
 *
 * Each editor is the Widget Animations panel's own Track Editor, in its compact form (see SWidgetAnimTimeline's
 * Compact), editing that list in place through an FWidgetAnimLayerArrayPanelSource: tracks dragged and resized,
 * keys added and moved, curves between keys, the transport, and a small Selection panel for the selected track or
 * key. It is as tall as it is dragged to be, from the grip along its bottom, and Expand opens the same list in a
 * window of its own. Save as Animation keeps the list as a WA_ asset - and can hand it over to a new Anim In or
 * Anim Out entry playing that asset.
 *
 * Playing or scrubbing poses the component's widget in the designer; see FindPreviewWidget in the .cpp for how
 * the component shown here is matched to the one on the designer's preview.
 *
 * The lists' plain array fields are hidden: the editors are the lists. Events lists every Event track the component's
 * animations have, each with a + for its event, as a button lists On Clicked.
 */
class FWidgetAnimationComponentDetails : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/**
	 * The Events group, open from the start, at the end of Category: one row per Event track in the component's animations
	 * and additive tracks, and in the animations inserted into them as tracks - "On Card Event Track Landed Fired" for a Landed
	 * track in WA_Card - then one per state event and Fire Event action of the state machines it plays, each with a + that adds
	 * its event to the Widget Blueprint's graph and shows it, as a button's On Clicked does, or shows the one already there.
	 * For a Widget Animation Component, or a Widget Animation State Machine Component (its machines'). See
	 * WidgetAnimCues::GetEventFunctionName.
	 */
	static void AddEventRows(class IDetailCategoryBuilder& Category, const TArray<TWeakObjectPtr<UObject>>& Objects);

private:

	/** One list's editor, under its own row, with the list's plain array field hidden. */
	void AddListEditor(IDetailLayoutBuilder& DetailBuilder, class IDetailCategoryBuilder& Category, FName PropertyName, bool bOut,
		const TArray<TWeakObjectPtr<UObject>>& Objects);

	/** The builder these Details were laid out by - refreshed after a list is handed over to an entry. */
	TWeakPtr<IDetailLayoutBuilder> WeakDetailBuilder;
};

/**
 * The Details of a Widget Animation State Machine Component: its State Machines, how a Widget Animation Component's own
 * animations compose with them and Preserve Authored Transforms, then the Events group of its machines' state events and
 * Fire Event actions (and their animations' Event tracks), as a Widget Animation Component lists them.
 */
class FWidgetAnimationStateMachineComponentDetails : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};