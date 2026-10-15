// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuSettingsSchema.h"

#include "Misc/DataValidation.h"
#include "MenuProjectSettings.h"

#define LOCTEXT_NAMESPACE "MenuSettingsSchema"

FSimpleMulticastDelegate& UMenuSettingsSchema::OnAnySchemaChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

#if WITH_EDITOR
EDataValidationResult UMenuSettingsSchema::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<FName> Seen;
	TSet<FName> PageIds;
	for (const FMenuSettingsPage& Page : Pages)
	{
		if (Page.Id.IsNone())
		{
			Context.AddError(FText::Format(LOCTEXT("PageNoId", "Page '{0}' has no Id."), Page.DisplayName));
			Result = EDataValidationResult::Invalid;
		}
		else if (PageIds.Contains(Page.Id))
		{
			Context.AddWarning(FText::Format(LOCTEXT("PageTwice", "Page {0} is listed twice (its sections are merged)."), FText::FromName(Page.Id)));
		}
		PageIds.Add(Page.Id);
		for (const FMenuSettingsSection& Section : Page.Sections)
		{
			for (const UMenuSetting* Setting : Section.Settings)
			{
				if (!Setting)
				{
					continue;
				}
				if (Setting->IsDataValid(Context) == EDataValidationResult::Invalid)
				{
					Result = EDataValidationResult::Invalid;
				}
				if (!Setting->Id.IsNone())
				{
					if (Seen.Contains(Setting->Id))
					{
						Context.AddError(FText::Format(LOCTEXT("SettingTwice", "Setting Id {0} is used twice: one value would be shared by both."), FText::FromName(Setting->Id)));
						Result = EDataValidationResult::Invalid;
					}
					Seen.Add(Setting->Id);
				}
			}
		}
	}
	return Result;
}

void UMenuSettingsSchema::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnAnySchemaChanged().Broadcast();
}
#endif

namespace MenuSettingsSchemaUtils
{
	namespace
	{
		bool ReplaceById(TArray<FMenuSettingsPage>& OutPages, UMenuSetting* Setting)
		{
			for (FMenuSettingsPage& Page : OutPages)
			{
				for (FMenuSettingsSection& Section : Page.Sections)
				{
					for (TObjectPtr<UMenuSetting>& Existing : Section.Settings)
					{
						if (Existing && Existing->Id == Setting->Id)
						{
							Existing = Setting;
							return true;
						}
					}
				}
			}
			return false;
		}
	}

	void Merge(const TArray<const UMenuSettingsSchema*>& Schemas, const FMenuSettingsMergeRules& Rules, TArray<FMenuSettingsPage>& OutPages)
	{
		OutPages.Reset();
		for (const UMenuSettingsSchema* Schema : Schemas)
		{
			if (!Schema)
			{
				continue;
			}
			for (const FMenuSettingsPage& SourcePage : Schema->Pages)
			{
				if (SourcePage.Id.IsNone() || Rules.HiddenPages.Contains(SourcePage.Id))
				{
					continue;
				}
				FMenuSettingsPage* Page = OutPages.FindByPredicate([&SourcePage](const FMenuSettingsPage& Candidate) { return Candidate.Id == SourcePage.Id; });
				if (!Page)
				{
					Page = &OutPages.AddDefaulted_GetRef();
					Page->Id = SourcePage.Id;
					Page->Order = SourcePage.Order;
				}
				else if (SourcePage.Order != FMenuSettingsPage().Order)
				{
					Page->Order = SourcePage.Order;
				}
				if (!SourcePage.DisplayName.IsEmpty())
				{
					Page->DisplayName = SourcePage.DisplayName;
				}
				if (SourcePage.Icon.HasImage())
				{
					Page->Icon = SourcePage.Icon;
				}
				if (SourcePage.Platforms.Num() > 0)
				{
					Page->Platforms = SourcePage.Platforms;
				}

				for (const FMenuSettingsSection& SourceSection : SourcePage.Sections)
				{
					const FName MergeId = SourceSection.GetMergeId();
					FMenuSettingsSection* Section = Page->Sections.FindByPredicate([MergeId](const FMenuSettingsSection& Candidate) { return Candidate.GetMergeId() == MergeId; });
					if (!Section)
					{
						Section = &Page->Sections.AddDefaulted_GetRef();
						Section->Id = SourceSection.Id;
						Section->Title = SourceSection.Title;
					}
					if (!SourceSection.Title.IsEmpty())
					{
						Section->Title = SourceSection.Title;
					}
					if (SourceSection.bKeyBindings)
					{
						Section->bKeyBindings = true;
						Section->KeyBindingDevice = SourceSection.KeyBindingDevice;
						Section->KeyBindingCategories = SourceSection.KeyBindingCategories;
					}
					Section->ShownWhen.Append(SourceSection.ShownWhen);

					for (UMenuSetting* Setting : SourceSection.Settings)
					{
						if (!Setting || Rules.HiddenSettings.Contains(Setting->Id))
						{
							continue;
						}
						if (Setting->Id.IsNone() || !ReplaceById(OutPages, Setting))
						{
							Section->Settings.Add(Setting);
						}
					}
				}
			}
		}
		OutPages.StableSort([](const FMenuSettingsPage& A, const FMenuSettingsPage& B) { return A.Order < B.Order; });
	}

	void GatherSchemas(UMenuSettingsSchema* BuiltIn, const TArray<UMenuSettingsSchema*>& Extra, TArray<const UMenuSettingsSchema*>& OutSchemas)
	{
		const UMenuProjectSettings& Settings = UMenuProjectSettings::Get();
		OutSchemas.Reset();
		if (BuiltIn && Settings.bIncludeBuiltInPages)
		{
			OutSchemas.Add(BuiltIn);
		}
		TArray<const UMenuSettingsSchema*> Ordered;
		for (const TSoftObjectPtr<UMenuSettingsSchema>& Entry : Settings.Schemas)
		{
			if (const UMenuSettingsSchema* Loaded = Entry.LoadSynchronous())
			{
				Ordered.AddUnique(Loaded);
			}
		}
		for (const UMenuSettingsSchema* Schema : Extra)
		{
			if (Schema)
			{
				Ordered.AddUnique(Schema);
			}
		}
		Ordered.StableSort([](const UMenuSettingsSchema& A, const UMenuSettingsSchema& B) { return A.Priority < B.Priority; });
		OutSchemas.Append(Ordered);
	}

	FMenuSettingsMergeRules MakeProjectRules()
	{
		const UMenuProjectSettings& Settings = UMenuProjectSettings::Get();
		FMenuSettingsMergeRules Rules;
		Rules.HiddenPages.Append(Settings.HiddenPages);
		Rules.HiddenSettings.Append(Settings.HiddenSettings);
		return Rules;
	}
}

#undef LOCTEXT_NAMESPACE
