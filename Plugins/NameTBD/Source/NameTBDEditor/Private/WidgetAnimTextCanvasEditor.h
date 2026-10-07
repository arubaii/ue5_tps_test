// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "DesignerExtension.h"
#include "IHasDesignerExtensibility.h"
#include "WidgetReference.h"

/**
 * Editing an animated text's tags on the UMG designer's canvas, the way the Custom Shape's outline is edited there.
 *
 * A designer extension puts a toolbar above a selected Animated Text Block, Animated Rich Text Block or their Common UI
 * kin (matched by class name, so this module needs no Common UI). It shows Edit Text; while editing, it shows a card
 * holding the text - tags and all - and, laid out as a word processor's, the buttons that mark it up, so nobody types
 * <Bold>...</> or <shake>...</> by hand:
 *
 *   Done
 *   Style, Effect      - the widget's Text Style Set rows (a rich text only), the project's Text Tags.
 *   Color, Size, Glow  - the built-in look tags.
 *   Clear              - takes the tags around or inside the selection off again.
 *
 * Each wraps the selection in its tag - the selection kept, so several can be stacked - or, with nothing selected, puts an
 * empty pair at the cursor. The buttons act as the mouse goes down and never take the keyboard, and the selection they act
 * on is kept by the widget's name, so it holds through a menu, the color picker or the designer rebuilding the toolbar.
 * Ctrl+Z and Ctrl+Y (or Ctrl+Shift+Z) step back and forward through the edit - typing and tags alike.
 *
 * The widget on the canvas follows as you type; every toolbar action, and the typing when the box is left, is one undo
 * step on the Widget Blueprint's widget, keeping the text's localization key.
 *
 * Whether a widget is being edited is kept here by its name, not in the canvas widgets, which the designer throws away and
 * builds again whenever it refreshes.
 */
namespace WidgetAnimTextEditing
{
	bool IsEditing(FName WidgetName);
	void SetEditing(FName WidgetName, bool bEditing);

	/** The animated text classes the editor is offered for, by name. */
	const TArray<FName>& GetEditableClassNames();
}

/** Puts the text editor on the designer's canvas for a selected animated text. */
class FWidgetAnimTextEditorExtension : public FDesignerExtension
{
public:
	FWidgetAnimTextEditorExtension();

	// FDesignerExtension
	virtual bool CanExtendSelection(const TArray<FWidgetReference>& Selection) const override;
	virtual void ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements) override;
};

/** Hands the designer a fresh text editor extension per designer opened. */
class FWidgetAnimTextEditorFactory : public IDesignerExtensionFactory, public TSharedFromThis<FWidgetAnimTextEditorFactory>
{
public:
	virtual ~FWidgetAnimTextEditorFactory() = default;

	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override;
};
