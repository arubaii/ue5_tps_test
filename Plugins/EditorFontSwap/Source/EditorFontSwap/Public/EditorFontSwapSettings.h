// EditorFontSwapSettings.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EditorFontSwapSettings.generated.h"

/**
 * Project Settings page: Editor > Editor Font Swap.
 * Config lives in DefaultEditorFontSwap.ini so it doesn't pollute other configs.
 */
UCLASS(config = EditorFontSwap, defaultconfig, meta = (DisplayName = "Editor Font Swap"))
class EDITORFONTSWAP_API UEditorFontSwapSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEditorFontSwapSettings();

	/** Master switch. When off, the plugin leaves the default Slate fonts untouched. */
	UPROPERTY(config, EditAnywhere, Category = "Font")
	bool bEnabled = false;

	/**
	 * Absolute path to a .ttf/.otf file on disk to use for the editor UI.
	 * Environment-style tokens are not expanded; use a full path.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Font", meta = (FilePathFilter = "ttf", RelativeToGameContentDir = false))
	FFilePath FontFilePath;

	/** UDeveloperSettings overrides so it appears under the Editor section. */
	virtual FName GetContainerName() const override { return FName("Project"); }
	virtual FName GetCategoryName() const override { return FName("Editor"); }

	/** Re-apply live when the user edits the path/toggle in Project Settings. */
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
