#include "ContextMenuPayload.h"

#include "NameTBD/Buttons/ContextMenuButton.h"
#include "Misc/StringBuilder.h"


void UContextMenuPayload::PostLoad()
{
	Super::PostLoad();

	// Transient, so it is empty on every load and there is nothing to invalidate - it just has to be
	// built once before anyone opens the Details panel on this payload.
	RefreshRequiredPayloadsSummary();
}

void UContextMenuPayload::RefreshRequiredPayloadsSummary()
{
	// The preset wins when one is assigned, matching the rule ContextButtons' own tooltip states and the
	// copy PostEditChangeProperty does - so the summary describes the menu that will actually be built
	// rather than whatever was last typed into the array by hand.
	const TArray<FGameplayTag>& Tags = ContextMenuPreset ? ContextMenuPreset->Buttons : ContextButtons;

	if (Tags.IsEmpty())
	{
		RequiredPayloadsSummary = TEXT("No buttons selected.");
		return;
	}

	TStringBuilder<2048> Builder;

	for (int32 Index = 0; Index < Tags.Num(); ++Index)
	{
		if (Index > 0)
		{
			Builder.Append(TEXT("\n\n"));
		}

		Builder.Appendf(TEXT("%d. "), Index + 1);

		// One shared description, so this and each button's own Action Status can never disagree about
		// what a tag supplies.
		Builder.Append(*UContextMenuButton::DescribeButtonTag(Tags[Index]));
	}

	RequiredPayloadsSummary = Builder.ToString();
}

#if WITH_EDITOR
void UContextMenuPayload::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) 
		? PropertyChangedEvent.Property->GetFName() 
		: NAME_None;

	// Check if ContextMenuPreset was the property that changed
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UContextMenuPayload, ContextMenuPreset))
	{
		if (ContextMenuPreset)
			ContextButtons = ContextMenuPreset->Buttons;
		else
			ContextButtons.Empty();
	}

	// Unconditional: the summary depends on the tag list AND on the settings entries those tags point
	// at, and an edit to the latter arrives here as a change to something else entirely (or as the
	// designer simply re-selecting this payload). Rebuilding it is a handful of string appends.
	RefreshRequiredPayloadsSummary();
}
#endif