// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTextReveal.h"

#include "NameTBD/WidgetAnimation/WidgetAnimParts.h"

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

	/** Where each part of Text ends: its letters, its words, or its own lines (a plain text's lines are its line breaks). */
	void FindPartEnds(const FString& Text, EWidgetAnimTextUnit Unit, TArray<int32>& OutEnds)
	{
		OutEnds.Reset();

		TArray<WidgetAnimTextParts::FRange> Letters;
		WidgetAnimTextParts::GetLetters(Text, Letters);

		if (Unit == EWidgetAnimTextUnit::Word)
		{
			TArray<WidgetAnimTextParts::FRange> Words;
			WidgetAnimTextParts::GetWords(Letters, Words);
			for (const WidgetAnimTextParts::FRange& Word : Words)
			{
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
			OutEnds.Add(Letter.End);
		}
	}

	/** The pause after a part ending at End (exclusive) of Text: a sentence's, a comma's, or none. */
	float GetPauseAfter(const FString& Text, int32 End, const FWidgetAnimTextRevealParams& Reveal)
	{
		// Closing quotes and brackets belong to what they close: 'he said "stop."' pauses after the quote.
		int32 Last = End - 1;
		while (Last >= 0 && FCString::Strchr(TEXT("\"')]}\x201D\x2019\x00BB"), Text[Last]) != nullptr)
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
	 * Works out Plan for String, counted in Unit as NumParts parts - PartEnds where each ends, when known, for the pauses.
	 * FitSeconds above 0 fits the reveal to that many seconds; otherwise it runs as long as its Typing Speed takes.
	 */
	void Build(FWidgetAnimTextRevealPlan& Plan, const FWidgetAnimTextRevealParams& Reveal, const FString& String, EWidgetAnimTextUnit Unit,
		int32 NumParts, const TArray<int32>& PartEnds, float FitSeconds)
	{
		Plan.ForString = String;
		Plan.Unit = ToPartUnit(Unit);
		Plan.NumParts = FMath::Max(NumParts, 0);
		Plan.Starts.Reset(Plan.NumParts);

		// Every part's start in seconds, at one interval apart and with its pauses, then scaled to the run.
		const bool bKnowEnds = PartEnds.Num() == Plan.NumParts;
		TArray<float, TInlineAllocator<64>> Pauses;
		float TotalPause = 0.f;
		for (int32 Part = 0; Part + 1 < Plan.NumParts; ++Part)
		{
			const float Pause = bKnowEnds ? FMath::Max(GetPauseAfter(String, PartEnds[Part], Reveal), 0.f) : 0.f;
			Pauses.Add(Pause);
			TotalPause += Pause;
		}

		float Appear = FMath::Max(Reveal.Appear.Time, 0.f);
		const int32 Gaps = FMath::Max(Plan.NumParts - 1, 0);
		float Interval = Reveal.TypingSpeed > KINDA_SMALL_NUMBER ? 1.f / Reveal.TypingSpeed : 0.f;
		float PauseScale = 1.f;
		float Run = Gaps * Interval + TotalPause + Appear;

		if (FitSeconds > 0.f)
		{
			// Across the run given: the pauses and the appearing kept in seconds, the parts spread over what's left - or,
			// with nothing left, all of it squeezed in proportion.
			Run = FitSeconds;
			const float Fixed = TotalPause + Appear;
			if (Fixed <= Run)
			{
				Interval = Gaps > 0 ? (Run - Fixed) / Gaps : 0.f;
			}
			else
			{
				Interval = 0.f;
				PauseScale = Fixed > KINDA_SMALL_NUMBER ? Run / Fixed : 0.f;
				Appear *= PauseScale;
			}
		}
		Run = FMath::Max(Run, 0.01f);

		float Time = 0.f;
		for (int32 Part = 0; Part < Plan.NumParts; ++Part)
		{
			Plan.Starts.Add(FMath::Clamp(Time / Run, 0.f, 1.f));
			if (Pauses.IsValidIndex(Part))
			{
				Time += Interval + Pauses[Part] * PauseScale;
			}
		}
		Plan.AppearFraction = Appear / Run;
		Plan.RunSeconds = Run;
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
	if (AppearFraction <= KINDA_SMALL_NUMBER)
	{
		return Past > 0.0 ? 1.f : 0.f;
	}
	return static_cast<float>(FMath::Clamp(Past / AppearFraction, 0.0, 1.0));
}

FText FWidgetAnimTextRevealEvaluator::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Text Reveal");
}

FText FWidgetAnimTextRevealEvaluator::GetDescription() const
{
	return LOCTEXT("Description", "Types a text out a letter, word or line at a time, each part appearing as its Appear says. At a Typing Speed the track is as long as its text takes, pausing after sentences and commas; with it off, Duration sets the pace. Value timing follows a value from 0 to 1 - for keeping a line in step with its voice-over. On an animated text widget nothing re-wraps as it types.");
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

	// The text it will type, counted as it will be - the widget's own count of its own text, so a stagger and the
	// reveal agree on what a line is.
	const FString String = Target ? WidgetAnimTextRevealPlan::GetRevealedString(Target, Reveal->Text) : FString();
	TArray<int32> PartEnds;
	WidgetAnimTextRevealPlan::FindPartEnds(String, Reveal->Unit, PartEnds);
	const int32 NumParts = Target ? WidgetAnimTextRevealPlan::CountParts(Target, String, Reveal->Unit, PartEnds, Reveal->Text.IsEmpty()) : PartEnds.Num();

	WidgetAnimTextRevealPlan::Build(Reveal->Plan, *Reveal, String, Reveal->Unit, NumParts, PartEnds, Reveal->bUseTypingSpeed ? 0.f : AuthoredRun);

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

	// A plan worked out for another text - one set while it plays, or none worked out yet - is worked out again, fitted
	// to the length the track already has.
	auto EnsurePlan = [&Plan, Reveal](const FString& String, int32 NumParts, const TArray<int32>& PartEnds)
	{
		if (!Plan.IsFor(String, WidgetAnimTextRevealPlan::ToPartUnit(Reveal->Unit), NumParts))
		{
			WidgetAnimTextRevealPlan::Build(Plan, *Reveal, String, Reveal->Unit, NumParts, PartEnds, Plan.RunSeconds);
		}
	};

	if (IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target))
	{
		if (!Reveal->Text.IsEmpty() && !Host->GetWidgetAnimText().ToString().Equals(Reveal->Text.ToString(), ESearchCase::CaseSensitive))
		{
			Host->SetWidgetAnimText(Reveal->Text);
		}

		const FString Display = Host->GetWidgetAnimDisplayString();
		// Counted by the widget, which lays its text out itself; where each part ends is only looked for when the plan
		// has to be worked out again.
		const FName Unit = WidgetAnimTextRevealPlan::ToPartUnit(Reveal->Unit);
		TArray<int32> PartEnds;
		const int32 NumParts = WidgetAnimTextRevealPlan::CountParts(Target, Display, Reveal->Unit, PartEnds, /*bOwnText=*/true);
		if (!Plan.IsFor(Display, Unit, NumParts))
		{
			WidgetAnimTextRevealPlan::FindPartEnds(Display, Reveal->Unit, PartEnds);
		}
		EnsurePlan(Display, NumParts, PartEnds);

		FWidgetAnimTextEffects& Effects = Host->GetWidgetAnimTextEffects();
		Effects.bReveal = true;
		Effects.RevealUnit = Unit;
		Effects.RevealAlphas.SetNumUninitialized(NumParts);
		for (int32 Part = 0; Part < NumParts; ++Part)
		{
			Effects.RevealAlphas[Part] = Plan.GetPartAlpha(Part, Value);
		}
		Effects.Appear = Reveal->Appear;
		Effects.Cursor = Reveal->Cursor;
		Effects.bCursorBlink = Reveal->bCursorBlink;
		Host->OnWidgetAnimTextEffectsChanged();
		return;
	}

	// A plain text: rewritten to the parts that have appeared.
	const FText Source = WidgetAnimPlainText::GetSource(Target, RevealChannel, Reveal->Text);
	const FString SourceString = WidgetAnimPlainText::GetDisplayString(Target, Source);
	TArray<int32> PartEnds;
	WidgetAnimTextRevealPlan::FindPartEnds(SourceString, Reveal->Unit, PartEnds);
	EnsurePlan(SourceString, PartEnds.Num(), PartEnds);

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

	const FWidgetAnimTextTrackBaseline* Captured = Baseline.GetPtr<FWidgetAnimTextTrackBaseline>();
	if (Captured && Captured->bHasText)
	{
		WidgetAnimPlainText::Forget(Target, RevealChannel);
		WidgetAnimPlainText::Write(Target, Captured->Text);
	}
}

bool FWidgetAnimTextRevealEvaluator::ClearEffects(FWidgetAnimTextEffects& Effects) const
{
	const bool bWasSet = Effects.bReveal;
	Effects.bReveal = false;
	Effects.RevealAlphas.Reset();
	Effects.Cursor.Reset();
	return bWasSet;
}

#undef LOCTEXT_NAMESPACE
