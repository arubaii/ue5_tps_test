// EditorFontSwapSettings.cpp
#include "EditorFontSwapSettings.h"
#include "EditorFontSwapModule.h"

UEditorFontSwapSettings::UEditorFontSwapSettings()
{
	CategoryName = TEXT("Editor");
	SectionName = TEXT("Editor Font Swap");
}

#if WITH_EDITOR
void UEditorFontSwapSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Ask the module to re-apply (or revert) whenever settings change.
	if (FEditorFontSwapModule* Module = FEditorFontSwapModule::GetPtr())
	{
		Module->ApplyFromSettings();
	}
}
#endif
