// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTextTracks.h"

#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"
#include "Components/Widget.h"
#include "Misc/App.h"
#include "NameTBD/WidgetAnimation/WidgetAnimParts.h"
#include "UObject/ObjectKey.h"

#define LOCTEXT_NAMESPACE "WidgetAnimTextTracks"

// =====================================================================================================
// Plain text
// =====================================================================================================

namespace WidgetAnimPlainText
{
	namespace
	{
		struct FEntry
		{
			FText Source;
			FString LastWritten;
			bool bHasWritten = false;
			double LastNumber = 0.0;
			bool bHasNumber = false;
		};

		using FKey = TPair<FObjectKey, FName>;

		TMap<FKey, FEntry>& GetEntries()
		{
			static TMap<FKey, FEntry> Entries;
			return Entries;
		}
	}

	FText GetSource(UObject* Target, FName Channel, const FText& Override)
	{
		if (!Override.IsEmpty())
		{
			return Override;
		}

		TMap<FKey, FEntry>& Entries = GetEntries();
		if (Entries.Num() > 256)
		{
			// Texts long gone are dropped wholesale rather than tracked; a live one only starts its text over.
			Entries.Reset();
		}

		FEntry& Entry = Entries.FindOrAdd(FKey(FObjectKey(Target), Channel));
		const FText Current = WidgetAnimText::GetText(Target);
		const bool bStillOurs = Entry.bHasWritten && Current.ToString().Equals(Entry.LastWritten, ESearchCase::CaseSensitive);
		if (!bStillOurs)
		{
			Entry.Source = Current;
		}
		return Entry.Source;
	}

	void NoteWritten(UObject* Target, FName Channel, const FString& Written)
	{
		FEntry& Entry = GetEntries().FindOrAdd(FKey(FObjectKey(Target), Channel));
		Entry.LastWritten = Written;
		Entry.bHasWritten = true;
	}

	bool IsStillWritten(const UObject* Target, FName Channel)
	{
		const FEntry* Entry = GetEntries().Find(FKey(FObjectKey(Target), Channel));
		return Entry && Entry->bHasWritten && WidgetAnimText::GetText(Target).ToString().Equals(Entry->LastWritten, ESearchCase::CaseSensitive);
	}

	void NoteNumberWritten(UObject* Target, FName Channel, double Written)
	{
		FEntry& Entry = GetEntries().FindOrAdd(FKey(FObjectKey(Target), Channel));
		Entry.LastNumber = Written;
		Entry.bHasNumber = true;
	}

	bool IsNumberStillWritten(const UObject* Target, FName Channel, double Current)
	{
		const FEntry* Entry = GetEntries().Find(FKey(FObjectKey(Target), Channel));
		// Numeric texts keep their number as a float, so near enough is the same number.
		return Entry && Entry->bHasNumber && FMath::IsNearlyEqual(Entry->LastNumber, Current, FMath::Max(FMath::Abs(Current) * 1.e-6, 1.e-4));
	}

	void Forget(UObject* Target, FName Channel)
	{
		GetEntries().Remove(FKey(FObjectKey(Target), Channel));
	}

	FString GetDisplayString(const UObject* Target, const FText& Text)
	{
		if (Cast<URichTextBlock>(Target))
		{
			FString Display;
			TArray<WidgetAnimRichText::FRun> Runs;
			WidgetAnimRichText::Parse(Text.ToString(), Display, Runs);
			return Display;
		}
		return Text.ToString();
	}

	void Write(UObject* Target, const FText& Text)
	{
		if (!WidgetAnimText::GetText(Target).ToString().Equals(Text.ToString(), ESearchCase::CaseSensitive))
		{
			WidgetAnimText::SetText(Target, Text);
		}
	}
}

int32 WidgetAnimTextPick(int32 Index, uint32 Seed, int32 Count)
{
	if (Count <= 1)
	{
		return 0;
	}
	uint32 Hash = static_cast<uint32>(Index) * 2654435761u ^ (Seed + 0x9E3779B9u) * 40503u;
	Hash ^= Hash >> 15;
	Hash *= 0x2C1B3C6Du;
	Hash ^= Hash >> 12;
	return static_cast<int32>(Hash % static_cast<uint32>(Count));
}

// =====================================================================================================
// Shared
// =====================================================================================================

FText FWidgetAnimTextTrackEvaluatorBase::GetCategory() const
{
	return LOCTEXT("Category", "Text");
}

UClass* FWidgetAnimTextTrackEvaluatorBase::GetTargetClass() const
{
	// Text Block and Rich Text Block - and every animated text widget, each one of those - share this.
	return UTextLayoutWidget::StaticClass();
}

UObject* FWidgetAnimTextTrackEvaluatorBase::ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const
{
	return WidgetAnimText::ResolveTarget(Widget);
}

bool FWidgetAnimTextTrackEvaluatorBase::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	// Nothing to record on a text host - its effects start empty - but a baseline is what gets them cleared again.
	if (!WidgetAnimText::AsHost(Target))
	{
		return false;
	}
	OutBaseline.InitializeAs<FWidgetAnimTextTrackBaseline>();
	return true;
}

void FWidgetAnimTextTrackEvaluatorBase::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	ClearEffectsOn(Target);
}

void FWidgetAnimTextTrackEvaluatorBase::ClearEffectsOn(UObject* Target) const
{
	// Put back every frame a track waits for its start, so only a change there was is drawn.
	if (IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target))
	{
		if (ClearEffects(Host->GetWidgetAnimTextEffects()))
		{
			Host->OnWidgetAnimTextEffectsChanged();
		}
	}
}

int32 FWidgetAnimTextTrackEvaluatorBase::CountLetters(const UObject* Target)
{
	if (const IWidgetAnimPartHost* PartHost = Cast<IWidgetAnimPartHost>(const_cast<UObject*>(Target)))
	{
		return PartHost->GetWidgetAnimPartCount(WidgetAnimPartUnits::Letter);
	}
	TArray<WidgetAnimTextParts::FRange> Letters;
	WidgetAnimTextParts::GetLetters(WidgetAnimText::GetDisplayString(Target), Letters);
	return Letters.Num();
}

namespace WidgetAnimTextTracks
{
	/** A track's Start or End field, for GetEndpointValue. */
	template <typename T>
	double GetStartOrEnd(const FInstancedStruct& Params, bool bStart, double Fallback)
	{
		const T* Typed = Params.GetPtr<T>();
		return Typed ? (bStart ? Typed->StartValue : Typed->EndValue) : Fallback;
	}
}

// =====================================================================================================
// Scramble
// =====================================================================================================

namespace
{
	const FName ScrambleChannel(TEXT("TextScramble"));
}

FString FWidgetAnimScrambleParams::GetCharacters() const
{
	switch (Charset)
	{
	case EWidgetAnimScrambleCharset::Letters:   return TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
	case EWidgetAnimScrambleCharset::UpperCase: return TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	case EWidgetAnimScrambleCharset::Digits:    return TEXT("0123456789");
	case EWidgetAnimScrambleCharset::Symbols:   return TEXT("!#$%&*+-=?@^~<>/\\|[]{}");
	case EWidgetAnimScrambleCharset::Binary:    return TEXT("01");
	case EWidgetAnimScrambleCharset::Hex:       return TEXT("0123456789ABCDEF");
	case EWidgetAnimScrambleCharset::Custom:    return CustomCharset.IsEmpty() ? FString(TEXT("#")) : CustomCharset;
	case EWidgetAnimScrambleCharset::LettersAndDigits:
	default:                                    return TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
	}
}

FText FWidgetAnimScrambleEvaluator::GetDisplayName() const
{
	return LOCTEXT("ScrambleName", "Scramble");
}

FText FWidgetAnimScrambleEvaluator::GetDescription() const
{
	return LOCTEXT("ScrambleDescription", "Decodes a text into place: each letter flickers through random characters until it settles on itself, one after another. Its value is how much of the text has settled. Letter by letter in place on an animated text widget; on a plain Text Block its text is rewritten.");
}

FText FWidgetAnimScrambleEvaluator::GetValueLabel() const
{
	return LOCTEXT("ScrambleValue", "Settled");
}

FName FWidgetAnimScrambleEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	return ScrambleChannel;
}

double FWidgetAnimScrambleEvaluator::GetEndpointValue(const FInstancedStruct& Params, bool bStart) const
{
	return WidgetAnimTextTracks::GetStartOrEnd<FWidgetAnimScrambleParams>(Params, bStart, bStart ? 0.0 : 1.0);
}

void FWidgetAnimScrambleEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	const FWidgetAnimScrambleParams* Scramble = GetParams(Params);
	if (!Target || !Scramble)
	{
		return;
	}

	const double Settledness = FMath::Clamp(Value, 0.0, 1.0);
	const int32 Ahead = Scramble->bLimitAhead ? FMath::Max(Scramble->AheadCount, 1) : -1;

	if (IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target))
	{
		const int32 NumLetters = CountLetters(Target);
		const int32 Settled = FMath::Clamp(FMath::FloorToInt32(Settledness * NumLetters + 1.e-4), 0, NumLetters);

		FWidgetAnimTextEffects& Effects = Host->GetWidgetAnimTextEffects();
		Effects.bScramble = Settled < NumLetters;
		Effects.ScrambleSettled = Settled;
		Effects.ScrambleAhead = Ahead;
		Effects.ScrambleCharset = Scramble->GetCharacters();
		Effects.ScrambleRate = FMath::Max(Scramble->ChangesPerSecond, 0.f);
		// A new pick each time a letter settles, too, so the flicker moves on with the decode even with no rate.
		Effects.ScrambleSeed = static_cast<uint32>(Settled) * 7919u;
		Host->OnWidgetAnimTextEffectsChanged();
		return;
	}

	// A plain text: its string rewritten - settled letters as they are, the next ones as stand-ins, white space kept,
	// and with Scramble Ahead nothing past those yet.
	const FText Source = WidgetAnimPlainText::GetSource(Target, ScrambleChannel, FText::GetEmpty());
	const FString SourceString = WidgetAnimPlainText::GetDisplayString(Target, Source);
	TArray<WidgetAnimTextParts::FRange> Letters;
	WidgetAnimTextParts::GetLetters(SourceString, Letters);
	const int32 NumLetters = Letters.Num();
	const int32 Settled = FMath::Clamp(FMath::FloorToInt32(Settledness * NumLetters + 1.e-4), 0, NumLetters);

	if (Settled >= NumLetters)
	{
		// All of it: the text itself, so a localized text stays one.
		WidgetAnimPlainText::Write(Target, Source);
		WidgetAnimPlainText::NoteWritten(Target, ScrambleChannel, Source.ToString());
		return;
	}

	const FString Characters = Scramble->GetCharacters();
	const uint32 Seed = static_cast<uint32>(Settled) * 7919u
		+ (Scramble->ChangesPerSecond > 0.f ? static_cast<uint32>(FMath::FloorToDouble(FApp::GetCurrentTime() * Scramble->ChangesPerSecond)) : 0u);

	FString Shown;
	Shown.Reserve(SourceString.Len());
	int32 Copied = 0;
	for (int32 LetterIndex = 0; LetterIndex < NumLetters; ++LetterIndex)
	{
		const WidgetAnimTextParts::FRange& Letter = Letters[LetterIndex];
		if (Ahead >= 0 && LetterIndex >= Settled + Ahead)
		{
			break;
		}

		// White space up to the letter, as it is.
		Shown.Append(SourceString.Mid(Copied, Letter.Begin - Copied));
		if (LetterIndex < Settled)
		{
			Shown.Append(SourceString.Mid(Letter.Begin, Letter.End - Letter.Begin));
		}
		else
		{
			Shown.AppendChar(Characters[WidgetAnimTextPick(LetterIndex, Seed, Characters.Len())]);
		}
		Copied = Letter.End;
	}

	WidgetAnimPlainText::Write(Target, FText::FromString(Shown));
	WidgetAnimPlainText::NoteWritten(Target, ScrambleChannel, Shown);
}

bool FWidgetAnimScrambleEvaluator::AnimatesAtRest(const FInstancedStruct& Params) const
{
	const FWidgetAnimScrambleParams* Scramble = GetParams(Params);
	return Scramble && Scramble->ChangesPerSecond > 0.f;
}

bool FWidgetAnimScrambleEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	if (!WidgetAnimText::IsText(Target))
	{
		return false;
	}
	FWidgetAnimTextTrackBaseline Baseline;
	Baseline.Text = WidgetAnimText::GetText(Target);
	Baseline.bHasText = !WidgetAnimText::AsHost(Target);
	OutBaseline.InitializeAs<FWidgetAnimTextTrackBaseline>(Baseline);
	return true;
}

void FWidgetAnimScrambleEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	ClearEffectsOn(Target);

	// Put back only while the scramble's own writing still shows - see WidgetAnimPlainText::IsStillWritten.
	const FWidgetAnimTextTrackBaseline* Captured = Baseline.GetPtr<FWidgetAnimTextTrackBaseline>();
	if (Captured && Captured->bHasText)
	{
		if (WidgetAnimPlainText::IsStillWritten(Target, ScrambleChannel))
		{
			WidgetAnimPlainText::Write(Target, Captured->Text);
		}
		WidgetAnimPlainText::Forget(Target, ScrambleChannel);
	}
}

bool FWidgetAnimScrambleEvaluator::ClearEffects(FWidgetAnimTextEffects& Effects) const
{
	const bool bWasSet = Effects.bScramble;
	Effects.bScramble = false;
	Effects.ScrambleSettled = 0;
	Effects.ScrambleAhead = -1;
	Effects.ScrambleRate = 0.f;
	return bWasSet;
}

// =====================================================================================================
// Text Color
// =====================================================================================================

namespace
{
	/**
	 * A plain Text Block's own color while a Text Color multiplies it - read on the track's first write, forgotten when
	 * its baseline is put back. The baseline itself can't be read from Apply.
	 */
	TMap<FObjectKey, FLinearColor>& GetOwnColors()
	{
		static TMap<FObjectKey, FLinearColor> OwnColors;
		return OwnColors;
	}

	/** The color a Text Color last set on a plain Text Block - so a color set since isn't undone when it lets go. */
	TMap<FObjectKey, FLinearColor>& GetWrittenColors()
	{
		static TMap<FObjectKey, FLinearColor> WrittenColors;
		return WrittenColors;
	}
}

FText FWidgetAnimTextColorEvaluator::GetDisplayName() const
{
	return LOCTEXT("ColorName", "Text Color");
}

FText FWidgetAnimTextColorEvaluator::GetDescription() const
{
	return LOCTEXT("ColorDescription", "Changes a text's color, blending from Start Color through a color at each key to End Color - a flash on damage, a highlight settling back, red to gold to white. Multiply keeps a rich text's styles apart; Replace makes every style the one color. Sets a plain Text Block's Color and Opacity, and puts it back after.");
}

FText FWidgetAnimTextColorEvaluator::GetValueLabel() const
{
	return LOCTEXT("ColorValue", "Blend");
}

FName FWidgetAnimTextColorEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	static const FName Channel(TEXT("TextColor"));
	return Channel;
}

FLinearColor FWidgetAnimTextColorEvaluator::GetEndpointColor(const FInstancedStruct& Params, bool bStart) const
{
	const FWidgetAnimTextColorParams* TextColor = GetParams(Params);
	return TextColor ? (bStart ? TextColor->StartColor : TextColor->EndColor) : FLinearColor::White;
}

void FWidgetAnimTextColorEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	// Unclamped, so a spring's overshoot carries on past the end color.
	if (const FWidgetAnimTextColorParams* TextColor = GetParams(Params))
	{
		ApplyColor(Target, *TextColor, FMath::Lerp(TextColor->StartColor, TextColor->EndColor, static_cast<float>(Value)));
	}
}

void FWidgetAnimTextColorEvaluator::ApplyWithColor(UObject* Target, const FInstancedStruct& Params, double Value, const FLinearColor& Color) const
{
	if (const FWidgetAnimTextColorParams* TextColor = GetParams(Params))
	{
		ApplyColor(Target, *TextColor, Color);
	}
}

void FWidgetAnimTextColorEvaluator::ApplyColor(UObject* Target, const FWidgetAnimTextColorParams& Params, const FLinearColor& Color)
{
	if (!Target)
	{
		return;
	}
	const bool bReplace = Params.Mode == EWidgetAnimTextColorMode::Replace;

	if (IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target))
	{
		FWidgetAnimTextEffects& Effects = Host->GetWidgetAnimTextEffects();
		Effects.bTint = true;
		Effects.Tint = Color;
		Effects.bTintReplaces = bReplace;
		Host->OnWidgetAnimTextEffectsChanged();
		return;
	}

	// A plain Text Block: its own color times this, or replaced by it at its own opacity.
	UTextBlock* TextBlock = Cast<UTextBlock>(Target);
	if (!TextBlock)
	{
		return;
	}
	TMap<FObjectKey, FLinearColor>& OwnColors = GetOwnColors();
	TMap<FObjectKey, FLinearColor>& WrittenColors = GetWrittenColors();
	if (OwnColors.Num() > 256)
	{
		OwnColors.Reset();
		WrittenColors.Reset();
	}
	const FObjectKey Key(TextBlock);
	FLinearColor Current = FLinearColor::White;
	WidgetAnimText::GetColor(TextBlock, Current);

	// Its own color is read on the first write - and again if something else has set a color since the last one.
	const FLinearColor* Written = WrittenColors.Find(Key);
	if (!OwnColors.Contains(Key) || !Written || !Written->Equals(Current, 0.f))
	{
		OwnColors.Add(Key, Current);
	}
	const FLinearColor Base = OwnColors.FindChecked(Key);
	FLinearColor Result = bReplace ? Color : Base * Color;
	if (bReplace)
	{
		Result.A *= Base.A;
	}
	WidgetAnimText::SetColor(TextBlock, Result);

	// Read back rather than kept as asked: a Slate color can round what it is given.
	FLinearColor Shown = Result;
	WidgetAnimText::GetColor(TextBlock, Shown);
	WrittenColors.Add(Key, Shown);
}

bool FWidgetAnimTextColorEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	if (WidgetAnimText::AsHost(Target))
	{
		return FWidgetAnimTextTrackEvaluatorBase::CaptureBaseline(Target, Params, OutBaseline);
	}

	FWidgetAnimTextTrackBaseline Baseline;
	if (!WidgetAnimText::GetColor(Target, Baseline.Color))
	{
		return false;
	}
	Baseline.bHasColor = true;
	OutBaseline.InitializeAs<FWidgetAnimTextTrackBaseline>(Baseline);
	return true;
}

void FWidgetAnimTextColorEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	ClearEffectsOn(Target);

	const FWidgetAnimTextTrackBaseline* Captured = Baseline.GetPtr<FWidgetAnimTextTrackBaseline>();
	if (Captured && Captured->bHasColor)
	{
		// Put back only while the color the track set still shows: one set since - in the designer's Details, or by the
		// game - is the text's color now.
		const FObjectKey Key(Target);
		const FLinearColor* Written = GetWrittenColors().Find(Key);
		FLinearColor Current;
		if (Written && WidgetAnimText::GetColor(Target, Current) && Written->Equals(Current, 0.f) && !Current.Equals(Captured->Color, 0.f))
		{
			WidgetAnimText::SetColor(Target, Captured->Color);
		}
		GetOwnColors().Remove(Key);
		GetWrittenColors().Remove(Key);
	}
}

bool FWidgetAnimTextColorEvaluator::ClearEffects(FWidgetAnimTextEffects& Effects) const
{
	const bool bWasSet = Effects.bTint;
	Effects.bTint = false;
	Effects.Tint = FLinearColor::White;
	Effects.bTintReplaces = false;
	return bWasSet;
}

// =====================================================================================================
// Shimmer
// =====================================================================================================

FText FWidgetAnimShimmerEvaluator::GetDisplayName() const
{
	return LOCTEXT("ShimmerName", "Shimmer");
}

FText FWidgetAnimShimmerEvaluator::GetDescription() const
{
	return LOCTEXT("ShimmerDescription", "Sweeps a band of light across a text, letter by letter. Its value is where the band is: 0 before the first letter, 1 past the last - loop it for a gleam every so often. Each key has a band color of its own, so the band can change color as it travels. Animated text widgets only.");
}

FText FWidgetAnimShimmerEvaluator::GetValueLabel() const
{
	return LOCTEXT("ShimmerValue", "Position");
}

FName FWidgetAnimShimmerEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	static const FName Channel(TEXT("TextShimmer"));
	return Channel;
}

FText FWidgetAnimShimmerEvaluator::GetKeyColorLabel(const FInstancedStruct& Params) const
{
	return LOCTEXT("ShimmerKeyColor", "Band Color");
}

FLinearColor FWidgetAnimShimmerEvaluator::GetEndpointColor(const FInstancedStruct& Params, bool bStart) const
{
	const FWidgetAnimShimmerParams* Shimmer = GetParams(Params);
	if (!Shimmer)
	{
		return FLinearColor::White;
	}
	return bStart || !Shimmer->bEndColor ? Shimmer->Color : Shimmer->EndColor;
}

void FWidgetAnimShimmerEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	ApplyWithColor(Target, Params, Value, GetEndpointColor(Params, /*bStart=*/true));
}

void FWidgetAnimShimmerEvaluator::ApplyWithColor(UObject* Target, const FInstancedStruct& Params, double Value, const FLinearColor& Color) const
{
	IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target);
	const FWidgetAnimShimmerParams* Shimmer = GetParams(Params);
	if (!Host || !Shimmer)
	{
		return;
	}

	// From wholly before the first letter to wholly past the last, so the band enters and leaves off the text.
	const float Reach = FMath::Max(Shimmer->Width, 0.f) + FMath::Max(Shimmer->Softness, 0.f) + 0.5f;
	const float Last = static_cast<float>(FMath::Max(CountLetters(Target) - 1, 0));

	FWidgetAnimTextEffects& Effects = Host->GetWidgetAnimTextEffects();
	Effects.bShimmer = true;
	Effects.ShimmerCenter = FMath::Lerp(-Reach, Last + Reach, static_cast<float>(Value));
	Effects.ShimmerWidth = FMath::Max(Shimmer->Width, 0.f);
	Effects.ShimmerSoftness = FMath::Max(Shimmer->Softness, 0.f);
	Effects.ShimmerColor = Color;
	Effects.ShimmerStrength = FMath::Clamp(Shimmer->Strength, 0.f, 1.f);
	Host->OnWidgetAnimTextEffectsChanged();
}

bool FWidgetAnimShimmerEvaluator::ClearEffects(FWidgetAnimTextEffects& Effects) const
{
	const bool bWasSet = Effects.bShimmer;
	Effects.bShimmer = false;
	return bWasSet;
}

// =====================================================================================================
// Wave
// =====================================================================================================

FText FWidgetAnimWaveEvaluator::GetDisplayName() const
{
	return LOCTEXT("WaveName", "Wave");
}

FText FWidgetAnimWaveEvaluator::GetDescription() const
{
	return LOCTEXT("WaveDescription", "Sends a wave along a text: letters bobbing, swaying, tilting or swelling one after another. Its value is the wave's strength, 0 still to 1 at full Amplitude; it keeps moving on its own while that holds. Animated text widgets only.");
}

FText FWidgetAnimWaveEvaluator::GetValueLabel() const
{
	return LOCTEXT("WaveValue", "Strength");
}

FName FWidgetAnimWaveEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	static const FName Channel(TEXT("TextWave"));
	return Channel;
}

double FWidgetAnimWaveEvaluator::GetEndpointValue(const FInstancedStruct& Params, bool bStart) const
{
	return WidgetAnimTextTracks::GetStartOrEnd<FWidgetAnimWaveParams>(Params, bStart, 1.0);
}

void FWidgetAnimWaveEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target);
	const FWidgetAnimWaveParams* Wave = GetParams(Params);
	if (!Host || !Wave)
	{
		return;
	}

	FWidgetAnimTextEffects& Effects = Host->GetWidgetAnimTextEffects();
	Effects.bWave = Value != 0.0;
	Effects.WaveMotion = Wave->Motion;
	Effects.WaveAmplitude = Wave->Amplitude * static_cast<float>(Value);
	Effects.WaveLength = FMath::Max(Wave->Wavelength, 0.5f);
	Effects.WaveSpeed = Wave->Speed;
	Effects.WavePhase = 0.f;
	Host->OnWidgetAnimTextEffectsChanged();
}

bool FWidgetAnimWaveEvaluator::ClearEffects(FWidgetAnimTextEffects& Effects) const
{
	const bool bWasSet = Effects.bWave;
	Effects.bWave = false;
	Effects.WaveSpeed = 0.f;
	return bWasSet;
}

// =====================================================================================================
// Shake
// =====================================================================================================

FText FWidgetAnimShakeEvaluator::GetDisplayName() const
{
	return LOCTEXT("ShakeName", "Shake");
}

FText FWidgetAnimShakeEvaluator::GetDescription() const
{
	return LOCTEXT("ShakeDescription", "Jitters every letter on its own - anger, fear, an impact. Its value is the shake's strength: by default a jolt that dies away; hold it at 1 for a steady tremble. Animated text widgets only.");
}

FText FWidgetAnimShakeEvaluator::GetValueLabel() const
{
	return LOCTEXT("ShakeValue", "Strength");
}

FName FWidgetAnimShakeEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	static const FName Channel(TEXT("TextShake"));
	return Channel;
}

double FWidgetAnimShakeEvaluator::GetEndpointValue(const FInstancedStruct& Params, bool bStart) const
{
	return WidgetAnimTextTracks::GetStartOrEnd<FWidgetAnimShakeParams>(Params, bStart, bStart ? 1.0 : 0.0);
}

void FWidgetAnimShakeEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target);
	const FWidgetAnimShakeParams* Shake = GetParams(Params);
	if (!Host || !Shake)
	{
		return;
	}

	const float Strength = FMath::Max(static_cast<float>(Value), 0.f);
	FWidgetAnimTextEffects& Effects = Host->GetWidgetAnimTextEffects();
	Effects.bShake = Strength > 0.f;
	Effects.ShakeAmount = Shake->Distance * Strength;
	Effects.ShakeAngle = Shake->Angle * Strength;
	Effects.ShakeRate = FMath::Max(Shake->Rate, 1.f);
	Host->OnWidgetAnimTextEffectsChanged();
}

bool FWidgetAnimShakeEvaluator::ClearEffects(FWidgetAnimTextEffects& Effects) const
{
	const bool bWasSet = Effects.bShake;
	Effects.bShake = false;
	Effects.ShakeRate = 0.f;
	return bWasSet;
}

// =====================================================================================================
// Letter Spacing
// =====================================================================================================

FText FWidgetAnimLetterSpacingEvaluator::GetDisplayName() const
{
	return LOCTEXT("SpacingName", "Letter Spacing");
}

FText FWidgetAnimLetterSpacingEvaluator::GetDescription() const
{
	return LOCTEXT("SpacingDescription", "Draws letters together or spreads them apart from the middle of each line, in Slate units between each two - a title tracking in to settle. The text is never laid out again, so wrapping doesn't change as it moves. Animated text widgets only.");
}

FText FWidgetAnimLetterSpacingEvaluator::GetValueLabel() const
{
	return LOCTEXT("SpacingValue", "Spacing");
}

FName FWidgetAnimLetterSpacingEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	static const FName Channel(TEXT("TextSpacing"));
	return Channel;
}

double FWidgetAnimLetterSpacingEvaluator::GetEndpointValue(const FInstancedStruct& Params, bool bStart) const
{
	return WidgetAnimTextTracks::GetStartOrEnd<FWidgetAnimLetterSpacingParams>(Params, bStart, 0.0);
}

void FWidgetAnimLetterSpacingEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target);
	if (!Host)
	{
		return;
	}

	FWidgetAnimTextEffects& Effects = Host->GetWidgetAnimTextEffects();
	const float Spacing = static_cast<float>(Value);
	if (Effects.ExtraSpacing != Spacing)
	{
		Effects.ExtraSpacing = Spacing;
		Host->OnWidgetAnimTextEffectsChanged();
	}
}

bool FWidgetAnimLetterSpacingEvaluator::ClearEffects(FWidgetAnimTextEffects& Effects) const
{
	const bool bWasSet = Effects.ExtraSpacing != 0.f;
	Effects.ExtraSpacing = 0.f;
	return bWasSet;
}

#undef LOCTEXT_NAMESPACE
