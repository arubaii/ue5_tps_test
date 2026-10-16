#pragma once

#include "CoreMinimal.h"
#include "ContextMenuInterface.h"
#include "ButtonAction.generated.h"

class UContextButtonPayload;
class UContextMenuButton;
class UContextMenuWidget;
class UInfoCardAnchor;
class UUserWidget;

/**
 * Where a context menu row's action is running: the menu, the row's button, the anchor that opened it, the widget that
 * anchor sits in (the inventory slot, the list row), and the payloads the card carries. Handed to Execute Menu Action and to
 * the Is Action Visible / Enabled checks.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuActionContext
{
	GENERATED_BODY()

	/** The row's Entry ID. */
	UPROPERTY(BlueprintReadOnly, Category = "Context Menu")
	FName EntryId = NAME_None;

	/** The menu the row is in (a submenu's own menu for a row in a submenu). */
	UPROPERTY(BlueprintReadOnly, Category = "Context Menu")
	TObjectPtr<UContextMenuWidget> Menu = nullptr;

	/** The row's button. Null in the visibility / enabled checks, which run before rows are built. */
	UPROPERTY(BlueprintReadOnly, Category = "Context Menu")
	TObjectPtr<UContextMenuButton> Button = nullptr;

	/** The anchor that opened the menu (for a submenu, the one that opened the first menu). Null for a menu opened without one. */
	UPROPERTY(BlueprintReadOnly, Category = "Context Menu")
	TObjectPtr<UInfoCardAnchor> Anchor = nullptr;

	/** The User Widget the anchor sits in: usually what the menu is about. */
	UPROPERTY(BlueprintReadOnly, Category = "Context Menu")
	TObjectPtr<UUserWidget> SourceWidget = nullptr;

	/** Every action payload the menu's card carries, in order (a Context Menu Anchor's Action Payloads). */
	UPROPERTY(BlueprintReadOnly, Category = "Context Menu")
	TArray<TObjectPtr<UContextButtonPayload>> Payloads;
};

/**
 * One thing a context menu button does when it is clicked.
 *
 * Two ways to use one:
 * - Inline, on a Context Menu asset's entry (Actions): pick the class and fill its fields in right there. Each open menu gets
 *   its own copy. The built-in ones - Run Console Command, Copy Text, Call Function, Open URL, Play Sound - cover the common
 *   cases without any Blueprint.
 * - By tag: list the class under a tag in Project Settings > Info Card > Button Actions, and every button (or menu entry)
 *   carrying that tag instantiates it and calls it, in the order listed.
 *
 * Subclass it in C++ or Blueprint and override Execute Menu Action (gets the whole context: menu, anchor, source widget,
 * payloads) or Execute Action (the button and the payloads only). Override Is Action Visible / Is Action Enabled to hide or
 * grey out a menu row from the state of the game - "Equip" greyed out while the item is already equipped.
 *
 * The instance is owned by the button or row that runs it and lives as long as it does, so an action may keep state between
 * clicks. Nothing here should assume the card's payloads are populated when it is created - read them from the arguments.
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class NAMETBD_API UButtonAction : public UObject, public IContextMenuInterface
{
	GENERATED_BODY()

public:

	/** Does nothing by default; a base class with no behaviour of its own is the point. */
	virtual void ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads) override;

	/**
	 * What a context menu row runs. By default it calls Execute Action with the row's button and the payloads, so an action
	 * written for buttons works in a menu unchanged.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Context Menus")
	void ExecuteMenuAction(const FContextMenuActionContext& Context);
	virtual void ExecuteMenuAction_Implementation(const FContextMenuActionContext& Context);

	/** False hides the menu row this action is on, each time the menu opens. True by default. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Context Menus")
	bool IsActionVisible(const FContextMenuActionContext& Context) const;
	virtual bool IsActionVisible_Implementation(const FContextMenuActionContext& Context) const { return true; }

	/** False greys out the menu row this action is on, each time the menu opens. True by default. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Context Menus")
	bool IsActionEnabled(const FContextMenuActionContext& Context) const;
	virtual bool IsActionEnabled_Implementation(const FContextMenuActionContext& Context) const { return true; }

	/** The first payload in Payloads of PayloadClass (or a subclass of it), or null. */
	UFUNCTION(BlueprintPure, Category = "Context Menus", meta = (DeterminesOutputType = "PayloadClass"))
	static UContextButtonPayload* FindActionPayload(const TArray<UContextButtonPayload*>& Payloads, TSubclassOf<UContextButtonPayload> PayloadClass);

	/** Borrows the world from the button or row that owns us, so Blueprint subclasses can use world-context nodes. */
	virtual UWorld* GetWorld() const override;
};
