// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTextReveal.h"

#include "NameTBD/WidgetAnimation/WidgetAnimParts.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextMarkup.h"

#define LOCTEXT_NAMESPACE "WidgetAnimTextReveal"

const FName FWidgetAnimTextRevealEvaluator::RevealChannel(TEXT("TextReveal"));

namespace WidgetAnimTextRevealPlan
{
	FName ToPartUnit(EWidgetAnimTextUnit Unit)
	{
		switch (Unit)
		{
		case EWidgetAnimTextUnit::Word: return WidgetAnimPartUnits::Word;
		case EWidgetAnimTextUnit::Line: return WidgetAnimPartUnits::Line;
		default:                        return WidgetAnimPartUnits::Letter;
		}
	}

	/**
	 * Where each part of Text begins and ends: its letters, its words, or its own lines (a plain text's lines are its
	 * line breaks).
	 */
	void FindParts(const FString& Text, EWidgetAnimTextUnit Unit, TArray<int32>& OutBegins, TArray<int32>& OutEnds)
	{
		OutBegins.Reset();
		OutEnds.Reset();

		TArray<WidgetAnimTextParts::FRange> Letters;
		WidgetAnimTextParts::GetLetters(Text, Letters);

		if (Unit == EWidgetAnimTextUnit::Word)
		{
			TArray<WidgetAnimTextParts::FRange> Words;
			WidgetAnimTextParts::GetWords(Letters, Words);
			for (const WidgetAnimTextParts::FRange& Word : Words)
			{
				OutBegins.Add(Word.Begin);
				OutEnds.Add(Word.End);
			}
			return;
		}

		if (Unit == EWidgetAnimTextUnit::Line)
		{
			// Lines with no letters are no part of their own - they come with the line after them.
			int32 LineEnd = 0;
			for (const WidgetAnimTextParts::FRange& Letter : Letters)
			{
				if (Letter.Begin < LineEnd)
				{
					continue;
				}
				OutBegins.Add(Letter.Begin);
				LineEnd = Letter.End;
				while (LineEnd < Text.Len() && Text[LineEnd] != TEXT('\n'))
				{
					++LineEnd;
				}
				OutEnds.Add(LineEnd);
			}
			return;
		}

		for (const WidgetAnimTextParts::FRange& Letter : Letters)
		{
			OutBegins.Add(Letter.Begin);
			OutEnds.Add(Letter.End);
		}
	}

	/** The pause after a part ending at End (exclusive) of Text: a sentence's, a comma's, or none. */
	float GetPauseAfter(const FString& Text, int32 End, const FWidgetAnimTextRevealParams& Reveal)
	{
		// Closing quotes and brackets belong to what they close: 'he said "stop."' pauses after the quote.
		int32 Last = End - 1;
		while (Last >= 0 && Last < Text.Len() && FCString::Strchr(TEXT("\"')]}\x201D\x2019\x00BB"), Text[Last]) != nullptr)
		{
			--Last;
		}
		if (Last < 0 || Last >= Text.Len())
		{
			return 0.f;
		}

		// Only at the end of a word, so 3.14, 1,000 and 12:30 don't stop mid-number, and an ellipsis pauses once.
		const bool bAtWordEnd = End >= Text.Len() || FChar::IsWhitespace(Text[End]);
		const TCHAR Char = Text[Last];

		// Full-width punctuation ends a word of its own: Chinese and Japanese sentences have no spaces.
		if (FCString::Strchr(TEXT("\x3002\xFF01\xFF1F"), Char) != nullptr)
		{
			return Reveal.SentencePause;
		}
		if (FCString::Strchr(TEXT("\x3001\xFF0C\xFF1B\xFF1A"), Char) != nullptr)
		{
			return Reveal.CommaPause;
		}
		if (!bAtWordEnd)
		{
			return 0.f;
		}
		if (FCString::Strchr(TEXT(".!?\x2026"), Char) != nullptr)
		{
			return Reveal.SentencePause;
		}
		if (FCString::Strchr(TEXT(",;:\x2014\x2013"), Char) != nullptr)
		{
			return Reveal.CommaPause;
		}
		return 0.f;
	}

	/**
	 * Works out Plan for String, counted in Unit as NumParts parts - PartBegins and PartEnds where each is, when known,
	 * for the pauses and for Markup's speeds, pauses and looks. FitSeconds above 0 fits the reveal to that many seconds;
	 * otherwise it runs as long as its Typing Speed takes, sped up by SpeedScale.
	 */
	void Build(FWidgetAnimTextRevealPlan& Plan, const FWidgetAnimTextRevealParams& Reveal, const FString& String, EWidgetAnimTextUnit Unit,
		int32 NumParts, const TArray<int32>& PartBegins, const TArray<int32>& PartEnds, const FWidgetAnimTextMarkup* Markup, float SpeedScale, float FitSeconds)
	{
		Plan.ForString = String;
		Plan.Unit = ToPartUnit(Unit);
		Plan.NumParts = FMath::Max(NumParts, 0);
		Plan.ForMarkup = Markup;
		Plan.Starts.Reset(Plan.NumParts);
		Plan.Appears.Reset();

		// Only a text counted the way it will be drawn can be paced by what is written in it.
		const bool bKnowParts = PartBegins.Num() == Plan.NumParts && PartEnds.Num() == Plan.NumParts;
		const FWidgetAnimTextMarkup* PacingMarkup = bKnowParts && Markup && Markup->bShapesReveal ? Markup : nullptr;

		// Per part, in authored seconds: how much of a typing interval it waits for (0 when shown with the one before, more
		// when typed slower), the pause before it, and how long it takes to appear.
		const float BaseAppear = FMath::Max(Reveal.GetAppear().Time, 0.f);
		TArray<float, TInlineAllocator<64>> Weights;
		TArray<float, TInlineAllocator<64>> Pauses;
		TArray<float, TInlineAllocator<64>> Appears;
		Weights.SetNumZeroed(Plan.NumParts);
		Pauses.SetNumZeroed(Plan.NumParts);
		Appears.Init(BaseAppear, Plan.NumParts);
		bool bAppearsDiffer = false;

		float TotalWeight = 0.f;
		float TotalPause = 0.f;
		for (int32 Part = 0; Part < Plan.NumParts; ++Part)
		{
			float Speed = 1.f;
			bool bInstant = false;
			if (PacingMarkup)
			{
				const int32 Begin = PartBegins[Part];
				Speed = PacingMarkup->GetRevealSpeedAt(Begin);
				bInstant = PacingMarkup->IsInstantAt(Begin);
				Pauses[Part] += PacingMarkup->GetPauseBetween(Part > 0 ? PartBegins[Part - 1] : -1, Begin);
				if (const FWidgetAnimTextSpan* Look = PacingMarkup->FindRevealLookAt(Begin))
				{
					Appears[Part] = FMath::Max(Look->Appear.Time, 0.f);
					bAppearsDiffer = true;
				}
			}

			if (Part > 0)
			{
				// Shown at once with the part before it, or after a typing interval at its speed.
				Weights[Part] = bInstant ? 0.f : 1.f / FMath::Max(Speed, 0.01f);
				if (bKnowParts)
				{
					Pauses[Part] += FMath::Max(GetPauseAfter(String, PartEnds[Part - 1], Reveal), 0.f);
				}
			}
			TotalWeight += Weights[Part];
			TotalPause += Pauses[Part];
		}

		const float LastAppear = Plan.NumParts > 0 ? Appears.Last() : BaseAppear;
		float Interval = Reveal.TypingSpeed > KINDA_SMALL_NUMBER ? 1.f / Reveal.TypingSpeed : 0.f;
		float FixedScale = 1.f;

		if (FitSeconds > 0.f)
		{
			// Across the run given: the pauses and the appearing kept in seconds, the parts spread over what's left - or,
			// with nothing left, all of it squeezed in proportion.
			const float Fixed = TotalPause + LastAppear;
			if (Fixed <= FitSeconds && TotalWeight > KINDA_SMALL_NUMBER)
			{
				Interval = (FitSeconds - Fixed) / TotalWeight;
			}
			else
			{
				Interval = 0.f;
				FixedScale = Fixed > KINDA_SMALL_NUMBER ? FMath::Min(FitSeconds / Fixed, 1.f) : 0.f;
			}
		}

		TArray<float, TInlineAllocator<64>> Seconds;
		Seconds.SetNumUninitialized(Plan.NumParts);
		float Time = 0.f;
		float Run = 0.f;
		for (int32 Part = 0; Part < Plan.NumParts; ++Part)
		{
			Time += Interval * Weights[Part] + Pauses[Part] * FixedScale;
			Seconds[Part] = Time;
			Run = FMath::Max(Run, Time + Appears[Part] * FixedScale);
		}
		Run = FitSeconds > 0.f ? FitSeconds : FMath::Max(Run, 0.01f);

		for (int32 Part = 0; Part < Plan.NumParts; ++Part)
		{
			Plan.Starts.Add(FMath::Clamp(Seconds[Part] / Run, 0.f, 1.f));
		}
		if (bAppearsDiffer)
		{
			Plan.Appears.Reserve(Plan.NumParts);
			for (const float Appear : Appears)
			{
				Plan.Appears.Add(Appear * FixedScale / Run);
			}
		}
		Plan.AppearFraction = BaseAppear * FixedScale / Run;

		// Faster or slower as the widget and the game ask: every part keeps its place along the run, only the run changes.
		Plan.RunSeconds = FitSeconds > 0.f ? Run : Run / FMath::Max(SpeedScale, 0.01f);
	}

	/** Target's text as it will be revealed: Override when set, else what it shows. */
	FString GetRevealedString(const UObject* Target, const FText& Override)
	{
		return Override.IsEmpty() ? WidgetAnimText::GetDisplayString(Target) : WidgetAnimPlainText::GetDisplayString(Target, Override);
	}

	/** How many parts of Unit Target's String has - a text widget's own count where it lays the text out itself. */
	int32 CountParts(const UObject* Target, const FString& String, EWidgetAnimTextUnit Unit, const TArray<int32>& PartEnds, bool bOwnText)
	{
		const IWidgetAnimPartHost* PartHost = Cast<IWidgetAnimPartHost>(const_cast<UObject*>(Target));
		if (PartHost && bOwnText)
		{
			return PartHost->GetWidgetAnimPartCount(ToPartUnit(Unit));
		}
		return PartEnds.Num();
	}

	/** Gives a text widget the reveal's own Text, if it has one and the widget doesn't show it yet. */
	void GiveOwnText(IWidgetAnimTextHost* Host, UObject* Target, const FText& Override)
	{
		if (Host && !Override.IsEmpty() && !Host->GetWidgetAnimText().ToString().Equals(Override.ToString(), ESearchCase::CaseSensitive))
		{
			Host->SetWidgetAnimText(Override);
			WidgetAnimPlainText::NoteWritten(Target, FWidgetAnimTextRevealEvaluator::RevealChannel, Override.ToString());
		}
	}
}

float FWidgetAnimTextRevealPlan::GetPartAlpha(int32 PartIndex, double Revealed) const
{
	// All of it at the end, however the arithmetic falls.
	if (Revealed >= 1.0 - 1.e-6)
	{
		return 1.f;
	}
	if (!Starts.IsValidIndex(PartIndex))
	{
		return 0.f;
	}

	// Strictly after its start, so a reveal at 0 shows nothing.
	const double Past = Revealed - Starts[PartIndex];
	const float PartAppear = Appears.IsValidIndex(PartIndex) ? Appears[PartIndex] : AppearFraction;
	if (PartAppear <= KINDA_SMALL_NUMBER)
	{
		return Past > 0.0 ? 1.f : 0.f;
	}
	return static_cast<float>(FMath::Clamp(Past / PartAppear, 0.0, 1.0));
}

FText FWidgetAnimTextRevealEvaluator::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Text Reveal");
}

FText FWidgetAnimTextRevealEvaluator::GetDescription() const
{
	return LOCTEXT("Description", "Types a text out a letter, word or line at a time, each part appearing by its Style - Typewriter, Fade, Slide, Pop, Stomp, Drop, Spin, Flip, Stretch, Blur, Wipe or Scatter. At a Typing Speed the track is as long as its text takes, pausing after sentences and commas and paced by the text's tags; with it off, Duration sets the pace. Preview Text To Come shows the whole line faintly first. Value timing follows a value from 0 to 1 - for keeping a line in step with its voice-over. On an animated text widget nothing re-wraps as it types.");
}

FText FWidgetAnimTextRevealEvaluator::GetValueLabel() const
{
	return LOCTEXT("ValueLabel", "Revealed");
}

FName FWidgetAnimTextRevealEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	return RevealChannel;
}

bool FWidgetAnimTextRevealEvaluator::HasDerivedLength(const FInstancedStruct& Params) const
{
	const FWidgetAnimTextRevealParams* Reveal = GetParams(Params);
	return Reveal && Reveal->bUseTypingSpeed;
}

FText FWidgetAnimTextRevealEvaluator::GetDerivedLengthLabel(const FInstancedStruct& Params) const
{
	const FWidgetAnimTextRevealParams* Reveal = GetParams(Params);
	if (!Reveal)
	{
		return FText::GetEmpty();
	}

	FNumberFormattingOptions Options;
	Options.SetMaximumFractionalDigits(1);
	const FText Speed = FText::AsNumber(Reveal->TypingSpeed, &Options);
	switch (Reveal->Unit)
	{
	case EWidgetAnimTextUnit::Word: return FText::Format(LOCTEXT("WordsPerSecond", "{0} words/s"), Speed);
	case EWidgetAnimTextUnit::Line: return FText::Format(LOCTEXT("LinesPerSecond", "{0} lines/s"), Speed);
	default:                        return FText::Format(LOCTEXT("LettersPerSecond", "{0} letters/s"), Speed);
	}
}

bool FWidgetAnimTextRevealEvaluator::IsReadyToPrepare(UObject* Target, const FInstancedStruct& Params) const
{
	// A widget's own lines are only known once it has a width to wrap at.
	const FWidgetAnimTextRevealParams* Reveal = GetParams(Params);
	const IWidgetAnimPartHost* PartHost = Cast<IWidgetAnimPartHost>(Target);
	if (!Reveal || !PartHost || Reveal->Unit != EWidgetAnimTextUnit::Line)
	{
		return true;
	}
	return PartHost->AreWidgetAnimPartsReady(WidgetAnimPartUnits::Line);
}

float FWidgetAnimTextRevealEvaluator::PrepareForPlay(UObject* Target, FInstancedStruct& Params, float AuthoredRun) const
{
	FWidgetAnimTextRevealParams* Reveal = Params.GetMutablePtr<FWidgetAnimTextRevealParams>();
	if (!Reveal)
	{
		return -1.f;
	}

	// A text widget given the reveal's own text now, so it is counted - tags and all - as it will be drawn.
	IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target);
	WidgetAnimTextRevealPlan::GiveOwnText(Host, Target, Reveal->Text);

	// The text it will type, counted as it will be - the widget's own count of its own text, so a stagger and the
	// reveal agree on what a line is.
	const FString String = Host ? Host->GetWidgetAnimDisplayString() : Target ? WidgetAnimTextRevealPlan::GetRevealedString(Target, Reveal->Text) : FString();
	TArray<int32> PartBegins;
	TArray<int32> PartEnds;
	WidgetAnimTextRevealPlan::FindParts(String, Reveal->Unit, PartBegins, PartEnds);
	const int32 NumParts = Target ? WidgetAnimTextRevealPlan::CountParts(Target, String, Reveal->Unit, PartEnds, Host || Reveal->Text.IsEmpty()) : PartEnds.Num();

	const TSharedPtr<const FWidgetAnimTextMarkup> Markup = Host ? Host->GetWidgetAnimTextMarkup() : nullptr;
	const float SpeedScale = Host ? Host->GetWidgetAnimRevealSpeed() : 1.f;
	WidgetAnimTextRevealPlan::Build(Reveal->Plan, *Reveal, String, Reveal->Unit, NumParts, PartBegins, PartEnds, Markup.Get(), SpeedScale,
		Reveal->bUseTypingSpeed ? 0.f : AuthoredRun);

	// With no text to count - nothing to aim at, say, in a preview - a typed reveal keeps its authored length.
	if (!Reveal->bUseTypingSpeed || !Target)
	{
		return -1.f;
	}
	return Reveal->Plan.RunSeconds;
}

double FWidgetAnimTextRevealEvaluator::GetEndpointValue(const FInstancedStruct& Params, bool bStart) const
{
	const FWidgetAnimTextRevealParams* Reveal = GetParams(Params);
	if (!Reveal)
	{
		return bStart ? 0.0 : 1.0;
	}
	return bStart ? Reveal->StartValue : Reveal->EndValue;
}

void FWidgetAnimTextRevealEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	const FWidgetAnimTextRevealParams* Reveal = GetParams(Params);
	if (!Target || !Reveal)
	{
		return;
	}
	FWidgetAnimTextRevealPlan& Plan = Reveal->Plan;
	const FName Unit = WidgetAnimTextRevealPlan::ToPartUnit(Reveal->Unit);

	if (IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target))
	{
		WidgetAnimTextRevealPlan::GiveOwnText(Host, Target, Reveal->Text);

		const FString Display = Host->GetWidgetAnimDisplayString();
		const TSharedPtr<const FWidgetAnimTextMarkup> Markup = Host->GetWidgetAnimTextMarkup();

		// Counted by the widget, which lays its text out itself. A plan worked out for another text - one set while it
		// plays, or none worked out yet - is worked out again, fitted to the length the track already has.
		TArray<int32> PartBegins;
		TArray<int32> PartEnds;
		const int32 NumParts = WidgetAnimTextRevealPlan::CountParts(Target, Display, Reveal->Unit, PartEnds, /*bOwnText=*/true);
		if (!Plan.IsFor(Display, Unit, NumParts, Markup.Get()))
		{
			WidgetAnimTextRevealPlan::FindParts(Display, Reveal->Unit, PartBegins, PartEnds);
			WidgetAnimTextRevealPlan::Build(Plan, *Reveal, Display, Reveal->Unit, NumParts, PartBegins, PartEnds, Markup.Get(), Host->GetWidgetAnimRevealSpeed(), Plan.RunSeconds);
		}

		FWidgetAnimTextEffects& Effects = Host->GetWidgetAnimTextEffects();
		Effects.bReveal = true;
		Effects.RevealUnit = Unit;
		Effects.RevealAlphas.SetNumUninitialized(NumParts);
		for (int32 Part = 0; Part < NumParts; ++Part)
		{
			Effects.RevealAlphas[Part] = Plan.GetPartAlpha(Part, Value);
		}
		Effects.Appear = Reveal->GetAppear();
		Effects.Cursor = Reveal->Cursor;
		Effects.bCursorBlink = Reveal->bCursorBlink;
		Effects.bRevealGhost = Reveal->bPreviewTextToCome;
		Effects.GhostOpacity = FMath::Clamp(Reveal->PreviewOpacity, 0.f, 1.f);
		Effects.bGhostColor = Reveal->bPreviewColor;
		Effects.GhostColor = Reveal->PreviewColor;
		Host->OnWidgetAnimTextEffectsChanged();
		return;
	}

	// A plain text: rewritten to the parts that have appeared.
	const FText Source = WidgetAnimPlainText::GetSource(Target, RevealChannel, Reveal->Text);
	const FString SourceString = WidgetAnimPlainText::GetDisplayString(Target, Source);
	TArray<int32> PartBegins;
	TArray<int32> PartEnds;
	WidgetAnimTextRevealPlan::FindParts(SourceString, Reveal->Unit, PartBegins, PartEnds);
	if (!Plan.IsFor(SourceString, Unit, PartEnds.Num(), nullptr))
	{
		WidgetAnimTextRevealPlan::Build(Plan, *Reveal, SourceString, Reveal->Unit, PartEnds.Num(), PartBegins, PartEnds, nullptr, 1.f, Plan.RunSeconds);
	}

	int32 Shown = 0;
	while (Shown < PartEnds.Num() && Plan.GetPartAlpha(Shown, Value) > 0.f)
	{
		++Shown;
	}

	// All of it: the text itself, so a localized text stays one - and a rich text keeps its styles.
	if (Shown >= PartEnds.Num())
	{
		WidgetAnimPlainText::Write(Target, Source);
		WidgetAnimPlainText::NoteWritten(Target, RevealChannel, Source.ToString());
		return;
	}

	const FString Written = SourceString.Left(Shown > 0 ? PartEnds[Shown - 1] : 0);
	WidgetAnimPlainText::Write(Target, FText::FromString(Written));
	WidgetAnimPlainText::NoteWritten(Target, RevealChannel, Written);
}

bool FWidgetAnimTextRevealEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	const FWidgetAnimTextRevealParams* Reveal = GetParams(Params);
	if (!Reveal || !WidgetAnimText::IsText(Target))
	{
		return false;
	}

	// A plain text is rewritten, and a text widget given the track's own Text: either way its text goes back after.
	FWidgetAnimTextTrackBaseline Baseline;
	Baseline.Text = WidgetAnimText::GetText(Target);
	Baseline.bHasText = !WidgetAnimText::AsHost(Target) || !Reveal->Text.IsEmpty();
	OutBaseline.InitializeAs<FWidgetAnimTextTrackBaseline>(Baseline);
	return true;
}

void FWidgetAnimTextRevealEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	ClearEffectsOn(Target);

	// Put back only while what shows is still what the reveal wrote. A text set since - in the designer, or by the game
	// for the next line - is the text now, and putting the old one back over it would undo it.
	const FWidgetAnimTextTrackBaseline* Captured = Baseline.GetPtr<FWidgetAnimTextTrackBaseline>();
	if (Captured && Captured->bHasText)
	{
		if (WidgetAnimPlainText::IsStillWritten(Target, RevealChannel))
		{
			WidgetAnimPlainText::Write(Target, Captured->Text);
		}
		WidgetAnimPlainText::Forget(Target, RevealChannel);
	}
}

bool FWidgetAnimTextRevealEvaluator::ClearEffects(FWidgetAnimTextEffects& Effects) const
{
	const bool bWasSet = Effects.bReveal;
	Effects.bReveal = false;
	Effects.RevealAlphas.Reset();
	Effects.Cursor.Reset();
	Effects.bRevealGhost = false;
	return bWasSet;
}

#undef LOCTEXT_NAMESPACE
