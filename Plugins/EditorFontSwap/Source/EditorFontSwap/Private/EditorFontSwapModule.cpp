// EditorFontSwapModule.cpp
#include "EditorFontSwapModule.h"
#include "EditorFontSwapSettings.h"

#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyle.h"
#include "Styling/StyleColors.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogEditorFontSwap, Log, All);

IMPLEMENT_MODULE(FEditorFontSwapModule, EditorFontSwap)

static FEditorFontSwapModule* GEditorFontSwapModule = nullptr;

FEditorFontSwapModule* FEditorFontSwapModule::GetPtr()
{
	return GEditorFontSwapModule;
}

void FEditorFontSwapModule::StartupModule()
{
	GEditorFontSwapModule = this;

	// LoadingPhase is PostEngineInit, so the Starship style is already registered
	// by the time we get here. Apply on a next-tick to be safe against ordering.
	FCoreDelegates::OnPostEngineInit.AddLambda([this]()
	{
		ApplyFromSettings();
	});

	// If OnPostEngineInit already fired, apply directly too.
	ApplyFromSettings();
}

void FEditorFontSwapModule::ShutdownModule()
{
	RevertFontOverride();
	GEditorFontSwapModule = nullptr;
}

void FEditorFontSwapModule::ApplyFromSettings()
{
	const UEditorFontSwapSettings* Settings = GetDefault<UEditorFontSwapSettings>();
	if (!Settings)
	{
		return;
	}

	if (!Settings->bEnabled || Settings->FontFilePath.FilePath.IsEmpty())
	{
		RevertFontOverride();
		return;
	}

	const FString AbsPath = FPaths::ConvertRelativePathToFull(Settings->FontFilePath.FilePath);
	if (!FPaths::FileExists(AbsPath))
	{
		UE_LOG(LogEditorFontSwap, Warning, TEXT("Font file not found: %s"), *AbsPath);
		return;
	}

	ApplyFontOverride(AbsPath);
}

void FEditorFontSwapModule::ApplyFontOverride(const FString& AbsoluteTtfPath)
{
	// The editor's core style is registered under a well-known name. In UE5 the
	// Starship style provides the app style; FAppStyle::GetAppStyleSetName()
	// returns it (historically "CoreStyle"/"EditorStyle", now the Starship set).
	const FName StyleSetName = FAppStyle::GetAppStyleSetName();

	const ISlateStyle* ConstStyle = FSlateStyleRegistry::FindSlateStyle(StyleSetName);
	if (!ConstStyle)
	{
		UE_LOG(LogEditorFontSwap, Warning, TEXT("Could not find app slate style '%s'."), *StyleSetName.ToString());
		return;
	}

	// FSlateStyleSet is what FindSlateStyle returns concretely for the app style.
	// The registry hands back const; we mutate via a const_cast because there is
	// no public mutable accessor. This is the crux of the "hack": it works, but
	// relies on the concrete type and internal font-entry names.
	FSlateStyleSet* MutableStyle = const_cast<FSlateStyleSet*>(
		static_cast<const FSlateStyleSet*>(ConstStyle));

	// Build a composite font that points every typeface entry at our single TTF.
	// A real font family would map Regular/Bold/Italic to different files; here we
	// use one face for all so the swap is simple and predictable.
	TSharedRef<FCompositeFont> NewComposite = MakeShared<FCompositeFont>();
	FTypefaceEntry& Regular = NewComposite->DefaultTypeface.Fonts[
		NewComposite->DefaultTypeface.Fonts.Add(FTypefaceEntry())];
	Regular.Name = TEXT("Regular");
	Regular.Font = FFontData(AbsoluteTtfPath, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);

	// Map the common named weights the editor asks for onto the same face so nothing
	// falls back to Roboto.
	for (const TCHAR* Weight : { TEXT("Bold"), TEXT("Italic"), TEXT("BoldItalic"),
	                             TEXT("Light"), TEXT("BoldCondensed"), TEXT("Black") })
	{
		FTypefaceEntry& Entry = NewComposite->DefaultTypeface.Fonts[
			NewComposite->DefaultTypeface.Fonts.Add(FTypefaceEntry())];
		Entry.Name = Weight;
		Entry.Font = FFontData(AbsoluteTtfPath, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	}

	// The style stores FSlateFontInfo brushes under many keys (e.g. "NormalFont",
	// "SmallFont", "Font.Large", etc.). We can't enumerate them portably, so we set
	// a curated list of the entries that cover the bulk of editor chrome. Unknown
	// keys are ignored safely by Set().
	static const TCHAR* FontKeys[] = {
		TEXT("NormalFont"),
		TEXT("SmallFont"),
		TEXT("Font.Small"),
		TEXT("Font.Regular"),
		TEXT("Font.Large"),
		TEXT("BoldFont"),
		TEXT("EmbossedText"),
	};

	int32 Applied = 0;
	for (const TCHAR* Key : FontKeys)
	{
		// Preserve the existing size; only swap the composite font underneath.
		FSlateFontInfo Existing = MutableStyle->GetFontStyle(FName(Key));
		const int32 Size = Existing.Size > 0 ? Existing.Size : 10;

		FSlateFontInfo NewInfo(NewComposite, Size);
		MutableStyle->Set(FName(Key), NewInfo);
		++Applied;
	}

	bOverrideActive = true;

	// Force a redraw so already-open windows pick up what they can. Widgets that
	// captured an FSlateFontInfo by value at construction time will not update
	// until they are recreated (e.g. reopening a panel).
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->FlushFontCache(TEXT("EditorFontSwap"));
		FSlateApplication::Get().InvalidateAllWidgets(false);
	}

	UE_LOG(LogEditorFontSwap, Log,
		TEXT("Applied font override (%d keys) from %s. Reopen panels for full effect."),
		Applied, *AbsoluteTtfPath);
}

void FEditorFontSwapModule::RevertFontOverride()
{
	if (!bOverrideActive)
	{
		return;
	}

	// We cannot perfectly reconstruct the original FSlateFontInfo objects, so the
	// honest behavior is: mark inactive and tell the user a restart restores the
	// stock fonts cleanly. Attempting to re-derive Roboto here would be guesswork.
	bOverrideActive = false;

	UE_LOG(LogEditorFontSwap, Log,
		TEXT("Override marked inactive. Restart the editor to fully restore default fonts."));
}
