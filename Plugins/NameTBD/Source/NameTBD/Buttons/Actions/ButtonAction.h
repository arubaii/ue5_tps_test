#pragma once

#include "CoreMinimal.h"
#include "ContextMenuInterface.h"
#include "ButtonAction.generated.h"

class UContextButtonPayload;
class UContextMenuButton;

/**
 * One thing a context menu button does when it is clicked.
 *
 * Subclass this - in C++ or as a Blueprint - override Execute Action, and list the subclass under a tag
 * in Project Settings > Info Card > Button Actions. Every button carrying that tag instantiates it and
 * calls it. A tag can list several, and they run in the order they are listed.
 *
 * The instance is owned by the button and lives as long as it does, so an action may keep state between
 * clicks. It is created when the button resolves its tag, which is why nothing here should assume the
 * card's payloads are populated yet - read those from the ActionPayloads argument instead.
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class NAMETBD_API UButtonAction : public UObject, public IContextMenuInterface
{
	GENERATED_BODY()

public:

	/** Does nothing by default; a base class with no behaviour of its own is the point. */
	virtual void ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads) override;

	/** Borrows the world from the button that owns us, so Blueprint subclasses can use world-context nodes. */
	virtual UWorld* GetWorld() const override;
};