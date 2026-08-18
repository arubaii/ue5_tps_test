// EditorFontSwapModule.h
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FEditorFontSwapModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Reads UEditorFontSwapSettings and applies or reverts accordingly. */
	void ApplyFromSettings();

	/** Convenience accessor so the settings object can poke the module. */
	static FEditorFontSwapModule* GetPtr();

private:
	/** Overwrite the font in the live Starship style's registered entries. */
	void ApplyFontOverride(const FString& AbsoluteTtfPath);

	/** Best-effort restore of the entries we changed. */
	void RevertFontOverride();

	/** Whether we currently have an override installed. */
	bool bOverrideActive = false;
};
