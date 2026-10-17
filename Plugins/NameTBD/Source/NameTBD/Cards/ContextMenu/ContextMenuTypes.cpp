// Copyright AR GameSystems 2026. All rights reserved.

#include "ContextMenuTypes.h"

#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

#include "NameTBD/DeveloperSettings/InfoCardDeveloperSettings.h"

namespace ContextMenuTypesPrivate
{
	FSlateFontInfo MakeFontAsset(const TCHAR* Typeface, int32 Size)
	{
		static ConstructorHelpers::FObjectFinder<UFont> DefaultFont(*UWidget::GetDefaultFontName());
		if (DefaultFont.Object)
		{
			return FSlateFontInfo(DefaultFont.Object, Size, FName(Typeface));
		}
		return FCoreStyle::GetDefaultFontStyle(FName(Typeface), Size);
	}

	FSlateBrush MakeRoundedBrush(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		Brush.OutlineSettings.Color = FSlateColor(Outline);
		Brush.OutlineSettings.Width = OutlineWidth;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		return Brush;
	}

	/** Entry IDs are names people read and type: letters, digits and underscores, from whatever the label says. */
	FName MakeIdFrom(const FString& Source)
	{
		FString Clean;
		Clean.Reserve(Source.Len());
		for (const TCHAR Character : Source)
		{
			if (FChar::IsAlnum(Character) || Character == TEXT('_'))
			{
				Clean.AppendChar(Character);
			}
		}
		return Clean.IsEmpty() ? FName(TEXT("Entry")) : FName(*Clean);
	}
}

// =================================================================================================
// Row tooltips, bindings
// =================================================================================================

FContextMenuTooltipStyle::FContextMenuTooltipStyle()
{
	// Below-right of the cursor, as desktop menus' item tips are: a submenu opening beside the row doesn't cover it.
	Positioning.PositionMode = EInfoCardPosition::ToCursor;
	Positioning.CursorPosition = EInfoCardCursorPosition::DownRight;
	Positioning.CardOffset = FVector2D(12.f, 20.f);
	Positioning.bAvoidOverlappingCards = true;
	// In front of every menu (a row's tooltip is kept in front of its own menu whatever this says).
	Positioning.ZOrder = 3000;
}

FContextMenuBindingStyle::FContextMenuBindingStyle()
{
	KeyDelimiter = INVTEXT("+");
	BindingSeparator = INVTEXT(", ");
	FrameBrush = ContextMenuTypesPrivate::MakeRoundedBrush(FLinearColor(1.f, 1.f, 1.f, 0.06f), 4.f, FLinearColor(1.f, 1.f, 1.f, 0.22f), 1.f);
}

// =================================================================================================
// FContextMenuEntry
// =================================================================================================

int32 FContextMenuEntry::CountLeftIcons() const
{
	int32 Count = 0;
	for (const FContextMenuIcon& Icon : LeftIcons)
	{
		Count += Icon.IsSet() ? 1 : 0;
	}
	return Count;
}

void FContextMenuEntry::PostSerialize(const FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	if (!Ar.IsLoading())
	{
		return;
	}

	// One icon per side became a list per side.
	auto MoveIcon = [](FContextMenuIcon& Old, TArray<FContextMenuIcon>& Into)
	{
		if (Old.IsSet() && Into.IsEmpty())
		{
			Into.Add(Old);
		}
		Old = FContextMenuIcon();
	};
	MoveIcon(LeftIcon_DEPRECATED, LeftIcons);
	MoveIcon(CenterIcon_DEPRECATED, CenterIcons);
	MoveIcon(RightIcon_DEPRECATED, RightIcons);

	// A Text Divider's own alignment (Left was the default, and reads the same as the style's default) became Style Default
	// or a choice of its own. Fill always drew as Left.
	if (HeaderAlignment_DEPRECATED != HAlign_Left && TextDividerAlignment == EContextMenuTextDividerAlignment::StyleDefault)
	{
		TextDividerAlignment = HeaderAlignment_DEPRECATED == HAlign_Center ? EContextMenuTextDividerAlignment::Center
			: (HeaderAlignment_DEPRECATED == HAlign_Right ? EContextMenuTextDividerAlignment::Right : EContextMenuTextDividerAlignment::Left);
	}
	HeaderAlignment_DEPRECATED = HAlign_Left;
#endif
}

FString FContextMenuEntry::GetIdSourceText() const
{
	switch (EntryType)
	{
	case EContextMenuEntryType::Button:
	case EContextMenuEntryType::Submenu:
		return Label.IsEmpty() ? (ButtonTag.IsValid() ? ButtonTag.GetTagName().ToString().Replace(TEXT("."), TEXT("_")) : FString(TEXT("Entry"))) : Label.ToString();
	case EContextMenuEntryType::Header:
		return HeaderText.IsEmpty() ? FString(TEXT("Divider")) : HeaderText.ToString();
	case EContextMenuEntryType::Divider:
		return TEXT("Divider");
	case EContextMenuEntryType::Spacer:
	default:
		return TEXT("Spacer");
	}
}

// =================================================================================================
// Style
// =================================================================================================

FContextMenuStyle::FContextMenuStyle()
{
	using namespace ContextMenuTypesPrivate;

	PanelBackground = MakeRoundedBrush(FLinearColor(0.022f, 0.024f, 0.03f, 0.98f), 8.f, FLinearColor(1.f, 1.f, 1.f, 0.09f), 1.f);
	RowHighlight = MakeRoundedBrush(FLinearColor(0.24f, 0.42f, 0.92f, 0.9f), 5.f, FLinearColor::Transparent, 0.f);
	RowPressed = MakeRoundedBrush(FLinearColor(0.18f, 0.33f, 0.78f, 1.f), 5.f, FLinearColor::Transparent, 0.f);

	LabelFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10);
	ShortcutFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9);
	HeaderFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8);

	// A quick pop: a fade with a slight grow, each played backwards as the menu closes.
	FInfoCardAdditiveAnimSettings Fade;
	Fade.AnimType = EInfoCardAdditiveAnimType::Fade;
	Fade.Duration = 0.1f;
	Fade.Easing = EWidgetAnimEasing::EaseOut;
	Fade.StartOpacity = 0.f;
	Fade.EndOpacity = 1.f;
	Fade.bMirrorInOut = true;

	FInfoCardAdditiveAnimSettings Grow;
	Grow.AnimType = EInfoCardAdditiveAnimType::Scale;
	Grow.Duration = 0.12f;
	Grow.Easing = EWidgetAnimEasing::EaseOut;
	Grow.StartScale = 0.96f;
	Grow.EndScale = 1.f;
	Grow.Overshoot = 0.f;
	Grow.bMirrorInOut = true;

	OpenAnimations.bUseBasicAnimations = true;
	OpenAnimations.PlayIn.Add(Fade);
	OpenAnimations.PlayIn.Add(Grow);

	// Menus drew at 1500 before Z Order was a placement field.
	Positioning.ZOrder = 1500;
}

const TArray<FName>& FContextMenuStyle::GetCategoryOrder()
{
	// What it does first (a new user's first questions: when, where), what it looks like after.
	static const TArray<FName> Order = {
		TEXT("Behavior"), TEXT("Placement"), TEXT("Submenu"), TEXT("Tooltips"),
		TEXT("Panel"), TEXT("Rows"), TEXT("Text"), TEXT("Bindings"), TEXT("Icons"), TEXT("Dividers"), TEXT("Animation & Sound") };
	return Order;
}

void FContextMenuStyle::UseDefaultFontAssets()
{
	if (IsRunningDedicatedServer())
	{
		return;
	}

	LabelFont = ContextMenuTypesPrivate::MakeFontAsset(TEXT("Regular"), 10);
	ShortcutFont = ContextMenuTypesPrivate::MakeFontAsset(TEXT("Regular"), 9);
	HeaderFont = ContextMenuTypesPrivate::MakeFontAsset(TEXT("Bold"), 8);
	RowTooltips.TextStyle.TitleFont = ContextMenuTypesPrivate::MakeFontAsset(TEXT("Bold"), 11);
	RowTooltips.TextStyle.BodyFont = ContextMenuTypesPrivate::MakeFontAsset(TEXT("Regular"), 10);
}

UContextMenuStyleAsset::UContextMenuStyleAsset()
{
	// The CDO is the built-in style (see Resolve), so it gets the real font asset too.
	Style.UseDefaultFontAssets();
}

const UContextMenuStyleAsset* UContextMenuStyleAsset::ResolveAsset(const UContextMenuStyleAsset* Override, const UContextMenuAsset* Menu)
{
	if (Override)
	{
		return Override;
	}

	if (Menu && Menu->Style)
	{
		return Menu->Style;
	}

	const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
	if (Settings && !Settings->DefaultContextMenuStyle.IsNull())
	{
		return Settings->DefaultContextMenuStyle.LoadSynchronous();
	}

	return nullptr;
}

const FContextMenuStyle& UContextMenuStyleAsset::Resolve(const UContextMenuStyleAsset* Override, const UContextMenuAsset* Menu)
{
	const UContextMenuStyleAsset* Found = ResolveAsset(Override, Menu);
	return Found ? Found->Style : GetDefault<UContextMenuStyleAsset>()->Style;
}

// =================================================================================================
// Menu asset
// =================================================================================================

TArray<FName> UContextMenuAsset::GetEntryIds() const
{
	TArray<FName> Ids;
	for (const FContextMenuEntry& Entry : Entries)
	{
		if (!Entry.EntryId.IsNone())
		{
			Ids.Add(Entry.EntryId);
		}
	}
	return Ids;
}

void UContextMenuAsset::AssignMissingEntryIds(bool bEvenWithoutText)
{
	TSet<FName> Used;
	for (FContextMenuEntry& Entry : Entries)
	{
		// A row just added has no label yet: its ID waits for one, or it would be stuck as "Entry".
		const bool bHasNoText = (Entry.EntryType == EContextMenuEntryType::Button || Entry.EntryType == EContextMenuEntryType::Submenu)
			&& Entry.Label.IsEmpty() && !Entry.ButtonTag.IsValid();
		if (Entry.EntryId.IsNone() && bHasNoText && !bEvenWithoutText)
		{
			continue;
		}

		if (Entry.EntryId.IsNone())
		{
			Entry.EntryId = ContextMenuTypesPrivate::MakeIdFrom(Entry.GetIdSourceText());
		}

		// Unique within the menu: overrides and events find entries by it. A copy-pasted row is the usual duplicate.
		FName Candidate = Entry.EntryId;
		int32 Suffix = 1;
		while (Used.Contains(Candidate))
		{
			Candidate = FName(Entry.EntryId, Suffix++);
		}
		Entry.EntryId = Candidate;
		Used.Add(Candidate);
	}
}

void UContextMenuAsset::PostLoad()
{
	Super::PostLoad();
	AssignMissingEntryIds(/*bEvenWithoutText=*/true);
}

#if WITH_EDITOR
void UContextMenuAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// An entry gets its ID from its label once it has one, and keeps it: anchors' overrides point at it. Clearing the ID makes
	// it take the current label again.
	AssignMissingEntryIds(/*bEvenWithoutText=*/false);
}
#endif

// =================================================================================================
// Payload
// =================================================================================================

TArray<FString> UInfoCardPayload_ContextMenu::GetMenuEntryIdOptions() const
{
	TArray<FString> Options;
	if (Menu)
	{
		for (const FName Id : Menu->GetEntryIds())
		{
			Options.Add(Id.ToString());
		}
	}
	return Options;
}

void UInfoCardPayload_ContextMenu::BuildEntries(TArray<FContextMenuEntry>& OutEntries) const
{
	OutEntries.Reset();
	if (Menu)
	{
		OutEntries = Menu->Entries;
	}

	TArray<bool> Removed;
	Removed.Init(false, OutEntries.Num());

	auto FindIndex = [&OutEntries](FName Id) -> int32
	{
		return Id.IsNone() ? INDEX_NONE : OutEntries.IndexOfByPredicate([Id](const FContextMenuEntry& Entry) { return Entry.EntryId == Id; });
	};

	for (const FContextMenuEntryOverride& Override : EntryOverrides)
	{
		const int32 Index = FindIndex(Override.EntryId);
		if (Index == INDEX_NONE)
		{
			continue;
		}

		FContextMenuEntry& Entry = OutEntries[Index];
		Removed[Index] = Override.bRemove;
		if (Override.bOverrideEnabled)
		{
			Entry.bEnabled = Override.bEnabled;
		}
		if (Override.bOverrideLabel)
		{
			Entry.Label = Override.Label;
		}
		if (Override.bOverrideChecked)
		{
			Entry.bChecked = Override.bChecked;
		}
	}

	// Extra entries next: their Before / After name the menu's own entries, and a runtime change can name an extra one.
	for (const FContextMenuExtraEntry& Extra : ExtraEntries)
	{
		int32 InsertAt = OutEntries.Num();
		switch (Extra.Position)
		{
		case EContextMenuInsertPosition::AtStart:
			InsertAt = 0;
			break;
		case EContextMenuInsertPosition::AfterEntry:
		case EContextMenuInsertPosition::BeforeEntry:
		{
			const int32 Relative = FindIndex(Extra.RelativeToEntryId);
			if (Relative != INDEX_NONE)
			{
				InsertAt = Extra.Position == EContextMenuInsertPosition::AfterEntry ? Relative + 1 : Relative;
			}
			break;
		}
		case EContextMenuInsertPosition::AtEnd:
		default:
			break;
		}

		OutEntries.Insert(Extra.Entry, InsertAt);
		Removed.Insert(false, InsertAt);
	}

	for (const TPair<FName, FContextMenuEntryRuntimeState>& Pair : RuntimeStates)
	{
		const int32 Index = FindIndex(Pair.Key);
		if (Index == INDEX_NONE)
		{
			continue;
		}

		FContextMenuEntry& Entry = OutEntries[Index];
		const FContextMenuEntryRuntimeState& State = Pair.Value;
		if (State.bEnabled.IsSet())
		{
			Entry.bEnabled = State.bEnabled.GetValue();
		}
		if (State.Label.IsSet())
		{
			Entry.Label = State.Label.GetValue();
		}
		if (State.bChecked.IsSet())
		{
			Entry.bChecked = State.bChecked.GetValue();
		}
		if (State.bHidden.IsSet())
		{
			Removed[Index] = State.bHidden.GetValue();
		}
	}

	TArray<FContextMenuEntry> Kept;
	Kept.Reserve(OutEntries.Num());
	for (int32 Index = 0; Index < OutEntries.Num(); ++Index)
	{
		if (!Removed[Index])
		{
			Kept.Add(MoveTemp(OutEntries[Index]));
		}
	}

	// Removing rows can leave a divider with nothing on one side of it: one at the very top or bottom, or two in a row.
	// Plain dividers only; a Text Divider names what follows and stays.
	TArray<FContextMenuEntry> Tidy;
	Tidy.Reserve(Kept.Num());
	for (FContextMenuEntry& Entry : Kept)
	{
		const bool bIsDivider = Entry.EntryType == EContextMenuEntryType::Divider;
		const bool bAfterDivider = Tidy.Num() > 0 && Tidy.Last().EntryType == EContextMenuEntryType::Divider;
		if (bIsDivider && (Tidy.Num() == 0 || bAfterDivider))
		{
			continue;
		}
		Tidy.Add(MoveTemp(Entry));
	}
	while (Tidy.Num() > 0 && Tidy.Last().EntryType == EContextMenuEntryType::Divider)
	{
		Tidy.Pop();
	}

	OutEntries = MoveTemp(Tidy);
}
