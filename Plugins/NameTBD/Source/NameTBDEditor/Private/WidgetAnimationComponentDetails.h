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
 * The lists' plain fields are still there, under the category's Advanced section.
 */
class FWidgetAnimationComponentDetails : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

	/** One list's editor, under its own row, with the list's plain fields moved to Advanced. */
	void AddListEditor(IDetailLayoutBuilder& DetailBuilder, class IDetailCategoryBuilder& Category, FName PropertyName, bool bOut,
		const TArray<TWeakObjectPtr<UObject>>& Objects);

	/** The builder these Details were laid out by - refreshed after a list is handed over to an entry. */
	TWeakPtr<IDetailLayoutBuilder> WeakDetailBuilder;
};