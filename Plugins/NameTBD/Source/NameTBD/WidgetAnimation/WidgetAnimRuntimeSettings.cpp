// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimRuntimeSettings.h"

UWidgetAnimRuntimeSettings::UWidgetAnimRuntimeSettings()
{
	// The library a project starts with. Only a default: once the list is edited, the project's own list is what the
	// config holds, so a tag a team removed stays removed.
	WidgetAnimTextMarkup::GetDefaultTags(TextTags);
}

#if WITH_EDITOR
void UWidgetAnimRuntimeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Every text reads its tags again the next time it is drawn, so an edited tag - or the glow's defaults or material -
	// shows in the designer at once.
	const FName Member = PropertyChangedEvent.GetMemberPropertyName();
	if (Member == GET_MEMBER_NAME_CHECKED(UWidgetAnimRuntimeSettings, TextTags)
		|| Member == GET_MEMBER_NAME_CHECKED(UWidgetAnimRuntimeSettings, TextGlowDefaults)
		|| Member == GET_MEMBER_NAME_CHECKED(UWidgetAnimRuntimeSettings, TextGlowMaterial))
	{
		WidgetAnimTextMarkup::NotifyTagsChanged();
	}
}
#endif

FString UWidgetAnimRuntimeSettings::GetDefaultSavePackagePath() const
{
#if WITH_EDITORONLY_DATA
	return NormalizeContentFolder(DefaultSaveFolder.Path);
#else
	return FString();
#endif
}

FString UWidgetAnimRuntimeSettings::NormalizeContentFolder(FString Folder)
{
	Folder.TrimStartAndEndInline();
	Folder.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (Folder.IsEmpty())
	{
		return Folder;
	}

	// The picker stores /Game/... paths; anything typed by hand relative to Content is taken as such.
	if (!Folder.StartsWith(TEXT("/")))
	{
		Folder = FString(TEXT("/Game/")) + Folder;
	}
	while (Folder.Len() > 1 && Folder.EndsWith(TEXT("/")))
	{
		Folder.LeftChopInline(1);
	}
	return Folder;
}
