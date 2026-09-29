// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTextMarkup.h"

#include "NameTBD/WidgetAnimation/WidgetAnimRuntimeSettings.h"

// =====================================================================================================
// Tags
// =====================================================================================================

void FWidgetAnimTextTag::Scale(float Amount, float Speed)
{
	if (Amount != 1.f)
	{
		WaveAmplitude *= Amount;
		ShakeDistance *= Amount;
		ShakeAngle *= Amount;
		PulseAmount *= Amount;
		Size = 1.f + (Size - 1.f) * Amount;
		ShimmerStrength = FMath::Clamp(ShimmerStrength * Amount, 0.f, 1.f);
		GlowStrength *= Amount;
		BlinkOpacity = FMath::Clamp(1.f - (1.f - BlinkOpacity) * Amount, 0.f, 1.f);
	}
	if (Speed != 1.f)
	{
		WaveSpeed *= Speed;
		ShakeRate *= Speed;
		PulseSpeed *= Speed;
		BlinkSpeed *= Speed;
		RainbowSpeed *= Speed;
		GlowPulseSpeed *= Speed;
		ShimmerEvery /= FMath::Max(Speed, 0.01f);
		RevealSpeed *= Speed;
	}
}

// =====================================================================================================
// Read markup
// =====================================================================================================

float FWidgetAnimTextMarkup::GetRevealSpeedAt(int32 Index) const
{
	float Speed = 1.f;
	for (const FWidgetAnimTextSpan& Span : Spans)
	{
		if (Span.Tag.bRevealSpeed && Span.Covers(Index))
		{
			Speed *= FMath::Max(Span.Tag.RevealSpeed, 0.01f);
		}
	}
	return Speed;
}

bool FWidgetAnimTextMarkup::IsInstantAt(int32 Index) const
{
	return Spans.ContainsByPredicate([Index](const FWidgetAnimTextSpan& Span) { return Span.Tag.bRevealInstant && Span.Covers(Index); });
}

const FWidgetAnimTextSpan* FWidgetAnimTextMarkup::FindRevealLookAt(int32 Index) const
{
	// The innermost: spans are kept in the order they were opened, so an inner one comes after the one around it.
	for (int32 SpanIndex = Spans.Num() - 1; SpanIndex >= 0; --SpanIndex)
	{
		if (Spans[SpanIndex].Tag.bRevealLook && Spans[SpanIndex].Covers(Index))
		{
			return &Spans[SpanIndex];
		}
	}
	return nullptr;
}

float FWidgetAnimTextMarkup::GetPauseBetween(int32 After, int32 Upto) const
{
	float Seconds = 0.f;
	for (const FWidgetAnimTextMarker& Marker : Markers)
	{
		if (Marker.Kind == EWidgetAnimTextMarkerKind::Pause && Marker.At > After && Marker.At <= Upto)
		{
			Seconds += FMath::Max(Marker.Seconds, 0.f);
		}
	}
	return Seconds;
}

namespace WidgetAnimTextMarkup
{
	namespace
	{
		uint32 GTagsVersion = 1;
		float GGlobalRevealSpeed = 1.f;

		bool IsNameChar(TCHAR Char)
		{
			return FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('.') || Char == TEXT('-');
		}

		/** One tag as written: its name, the value after "=", its attributes, and where it ends. */
		struct FTagText
		{
			FName Name;
			FString Value;
			TArray<TPair<FString, FString>, TInlineAllocator<2>> Attributes;
			bool bSelfClosing = false;

			/** Just past its '>'. */
			int32 End = INDEX_NONE;

			const FString* FindAttribute(const TCHAR* Key) const
			{
				for (const TPair<FString, FString>& Attribute : Attributes)
				{
					if (Attribute.Key.Equals(Key, ESearchCase::IgnoreCase))
					{
						return &Attribute.Value;
					}
				}
				return nullptr;
			}
		};

		/**
		 * Reads the tag opening at Start - the '<' - as <Name>, <Name=Value>, <Name Key=Value ...> or any of those closed
		 * with "/>". False when what follows the '<' isn't a tag at all, which leaves it text.
		 */
		bool ReadTag(const FString& Source, int32 Start, FTagText& Out)
		{
			const int32 Length = Source.Len();
			int32 Index = Start + 1;
			const int32 NameBegin = Index;
			while (Index < Length && IsNameChar(Source[Index]))
			{
				++Index;
			}
			if (Index == NameBegin)
			{
				return false;
			}
			Out.Name = FName(*Source.Mid(NameBegin, Index - NameBegin));

			auto SkipSpace = [&Source, &Index, Length]()
			{
				while (Index < Length && FChar::IsWhitespace(Source[Index]))
				{
					++Index;
				}
			};
			auto ReadValue = [&Source, &Index, Length](FString& OutValue)
			{
				if (Index < Length && (Source[Index] == TEXT('"') || Source[Index] == TEXT('\'')))
				{
					const TCHAR Quote = Source[Index++];
					const int32 ValueBegin = Index;
					while (Index < Length && Source[Index] != Quote)
					{
						++Index;
					}
					if (Index >= Length)
					{
						return false;
					}
					OutValue = Source.Mid(ValueBegin, Index - ValueBegin);
					++Index;
					return true;
				}

				const int32 ValueBegin = Index;
				while (Index < Length && !FChar::IsWhitespace(Source[Index]) && Source[Index] != TEXT('>')
					&& !(Source[Index] == TEXT('/') && Index + 1 < Length && Source[Index + 1] == TEXT('>')))
				{
					++Index;
				}
				OutValue = Source.Mid(ValueBegin, Index - ValueBegin);
				return true;
			};

			if (Index < Length && Source[Index] == TEXT('='))
			{
				++Index;
				if (!ReadValue(Out.Value))
				{
					return false;
				}
			}

			for (;;)
			{
				SkipSpace();
				if (Index >= Length)
				{
					return false;
				}
				if (Source[Index] == TEXT('>'))
				{
					Out.End = Index + 1;
					return true;
				}
				if (Source[Index] == TEXT('/') && Index + 1 < Length && Source[Index + 1] == TEXT('>'))
				{
					Out.bSelfClosing = true;
					Out.End = Index + 2;
					return true;
				}

				// Anything but an attribute - "<3 you>" - means this was never a tag.
				const int32 KeyBegin = Index;
				while (Index < Length && IsNameChar(Source[Index]))
				{
					++Index;
				}
				if (Index == KeyBegin)
				{
					return false;
				}
				TPair<FString, FString>& Attribute = Out.Attributes.AddDefaulted_GetRef();
				Attribute.Key = Source.Mid(KeyBegin, Index - KeyBegin);
				SkipSpace();
				if (Index < Length && Source[Index] == TEXT('='))
				{
					++Index;
					SkipSpace();
					if (!ReadValue(Attribute.Value))
					{
						return false;
					}
				}
			}
		}

		/** Value as a number, or Fallback when it isn't one. */
		float ReadNumber(const FString& Value, float Fallback)
		{
			const FString Trimmed = Value.TrimStartAndEnd();
			return !Trimmed.IsEmpty() && FCString::IsNumeric(*Trimmed) ? FCString::Atof(*Trimmed) : Fallback;
		}

		/** A color written as #RGB, #RRGGBB, #RRGGBBAA (the # optional) or by name. */
		bool ReadColor(const FString& Value, FLinearColor& OutColor)
		{
			const FString Trimmed = Value.TrimStartAndEnd();
			struct FNamed
			{
				const TCHAR* Name;
				FLinearColor Color;
			};
			static const FNamed Named[] =
			{
				{ TEXT("red"), FLinearColor(1.f, 0.12f, 0.1f) },
				{ TEXT("orange"), FLinearColor(1.f, 0.45f, 0.05f) },
				{ TEXT("yellow"), FLinearColor(1.f, 0.9f, 0.1f) },
				{ TEXT("gold"), FLinearColor(1.f, 0.7f, 0.15f) },
				{ TEXT("green"), FLinearColor(0.2f, 0.85f, 0.25f) },
				{ TEXT("cyan"), FLinearColor(0.1f, 0.85f, 1.f) },
				{ TEXT("blue"), FLinearColor(0.15f, 0.35f, 1.f) },
				{ TEXT("purple"), FLinearColor(0.55f, 0.2f, 1.f) },
				{ TEXT("magenta"), FLinearColor(1.f, 0.15f, 0.8f) },
				{ TEXT("pink"), FLinearColor(1.f, 0.45f, 0.65f) },
				{ TEXT("white"), FLinearColor::White },
				{ TEXT("black"), FLinearColor::Black },
				{ TEXT("gray"), FLinearColor(0.35f, 0.35f, 0.35f) },
				{ TEXT("grey"), FLinearColor(0.35f, 0.35f, 0.35f) }
			};
			for (const FNamed& Entry : Named)
			{
				if (Trimmed.Equals(Entry.Name, ESearchCase::IgnoreCase))
				{
					OutColor = Entry.Color;
					return true;
				}
			}

			FString Hex = Trimmed;
			Hex.RemoveFromStart(TEXT("#"));
			if (Hex.Len() != 3 && Hex.Len() != 6 && Hex.Len() != 8)
			{
				return false;
			}
			for (const TCHAR Char : Hex)
			{
				if (!FChar::IsHexDigit(Char))
				{
					return false;
				}
			}
			// Written as sRGB, as a color picker shows it; drawn in linear.
			OutColor = FLinearColor(FColor::FromHex(Hex));
			return true;
		}

		/** Names of the built-in tags that stand at a point rather than covering words. */
		/**
		 * A glowing tag's attributes as written: r= its radius and p= its pulse, and every other attribute besides a= and
		 * s= (which Scale reads) a material parameter of that name - a number a scalar, a color a vector.
		 */
		void ApplyEffectAttributes(const FTagText& Tag, FWidgetAnimTextTag& Effect)
		{
			for (const TPair<FString, FString>& Attribute : Tag.Attributes)
			{
				const FString& Key = Attribute.Key;
				if (Key.Equals(TEXT("a"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("s"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				if (Key.Equals(TEXT("r"), ESearchCase::IgnoreCase))
				{
					Effect.GlowRadius = FMath::Max(ReadNumber(Attribute.Value, Effect.GlowRadius), 0.5f);
					continue;
				}
				if (Key.Equals(TEXT("p"), ESearchCase::IgnoreCase))
				{
					Effect.GlowPulse = FMath::Clamp(ReadNumber(Attribute.Value, Effect.GlowPulse), 0.f, 1.f);
					continue;
				}

				const FString Trimmed = Attribute.Value.TrimStartAndEnd();
				FLinearColor Color;
				if (!Trimmed.IsEmpty() && FCString::IsNumeric(*Trimmed))
				{
					Effect.GlowScalarParameters.Add(FName(*Key), FCString::Atof(*Trimmed));
				}
				else if (ReadColor(Trimmed, Color))
				{
					Effect.GlowVectorParameters.Add(FName(*Key), Color);
				}
			}
		}

		/**
		 * Which layer a glowing span's letters go into: its material (the tag's, or the project's), which side of the text,
		 * and its material parameters - so spans agreeing on all three share one. 0 when it has no material to draw with.
		 */
		void ResolveEffect(FWidgetAnimTextSpan& Span)
		{
			const FWidgetAnimTextTag& Tag = Span.Tag;
			Span.EffectKey = 0;
			Span.EffectMaterial.Reset();
			if (!Tag.bGlow)
			{
				return;
			}

			Span.EffectMaterial = Tag.GlowMaterial.IsNull()
				? UWidgetAnimRuntimeSettings::Get().TextGlowMaterial.ToSoftObjectPath()
				: Tag.GlowMaterial.ToSoftObjectPath();
			if (Span.EffectMaterial.IsNull())
			{
				return;
			}

			uint32 Key = HashCombine(GetTypeHash(Span.EffectMaterial), GetTypeHash(Tag.bGlowOverText));

			// Parameters in name order, so the same set written in another order is the same layer.
			TArray<FName> ScalarNames;
			Tag.GlowScalarParameters.GetKeys(ScalarNames);
			ScalarNames.Sort(FNameLexicalLess());
			for (const FName Name : ScalarNames)
			{
				Key = HashCombine(Key, HashCombine(GetTypeHash(Name.ToString().ToLower()), GetTypeHash(Tag.GlowScalarParameters.FindChecked(Name))));
			}
			TArray<FName> VectorNames;
			Tag.GlowVectorParameters.GetKeys(VectorNames);
			VectorNames.Sort(FNameLexicalLess());
			for (const FName Name : VectorNames)
			{
				Key = HashCombine(Key, HashCombine(GetTypeHash(Name.ToString().ToLower()), GetTypeHash(Tag.GlowVectorParameters.FindChecked(Name))));
			}
			Span.EffectKey = Key != 0 ? Key : 1u;
		}

		bool IsMarkerName(FName Name)
		{
			static const FName Pause(TEXT("pause"));
			static const FName Wait(TEXT("wait"));
			static const FName Event(TEXT("event"));
			static const FName Play(TEXT("play"));
			return Name == Pause || Name == Wait || Name == Event || Name == Play;
		}
	}

	uint32 GetTagsVersion()
	{
		return GTagsVersion;
	}

	void NotifyTagsChanged()
	{
		++GTagsVersion;
	}

	float GetGlobalRevealSpeed()
	{
		return GGlobalRevealSpeed;
	}

	const FWidgetAnimTextTag* FindTag(FName Name)
	{
		if (Name.IsNone())
		{
			return nullptr;
		}
		// FName comparison ignores case, so <Shake> finds shake.
		return UWidgetAnimRuntimeSettings::Get().TextTags.FindByPredicate([Name](const FWidgetAnimTextTag& Tag) { return Tag.Tag == Name; });
	}

	void Parse(const FString& Source, bool bRich, bool bTags, TFunctionRef<bool(FName)> IsStyle, FWidgetAnimTextMarkup& Out)
	{
		Out = FWidgetAnimTextMarkup();
		FString& Display = Out.Display;
		Display.Reserve(Source.Len());

		// What is open, innermost last: a style (rich text only) or an effect span, by its index in Out.Spans.
		struct FOpen
		{
			FName Name;
			bool bStyle = false;
			int32 Span = INDEX_NONE;
		};
		TArray<FOpen, TInlineAllocator<8>> Open;

		// Styled runs, as WidgetAnimRichText::Parse makes them - a style's run from where it opened to where it closed.
		FName CurrentStyle;
		int32 RunBegin = 0;
		auto SwitchStyle = [&Out, &Display, &CurrentStyle, &RunBegin](FName NewStyle)
		{
			if (Display.Len() > RunBegin)
			{
				WidgetAnimRichText::FRun& Run = Out.StyleRuns.AddDefaulted_GetRef();
				Run.Begin = RunBegin;
				Run.End = Display.Len();
				Run.Style = CurrentStyle;
			}
			RunBegin = Display.Len();
			CurrentStyle = NewStyle;
		};

		auto CloseOpen = [&Open, &Out, &Display, &SwitchStyle](int32 OpenIndex)
		{
			const FOpen Closed = Open[OpenIndex];
			Open.RemoveAt(OpenIndex);
			if (Closed.bStyle)
			{
				// Back to the style around it, if any - styles nest here, though a Rich Text Block's own don't.
				FName Outer;
				for (int32 OuterIndex = Open.Num() - 1; OuterIndex >= 0; --OuterIndex)
				{
					if (Open[OuterIndex].bStyle)
					{
						Outer = Open[OuterIndex].Name;
						break;
					}
				}
				SwitchStyle(Outer);
			}
			else if (Out.Spans.IsValidIndex(Closed.Span))
			{
				Out.Spans[Closed.Span].End = Display.Len();
			}
		};

		auto OpenSpan = [&Open, &Out, &Display](const FWidgetAnimTextTag& Tag, FName Name, bool bKeepOpen)
		{
			FWidgetAnimTextSpan& Span = Out.Spans.AddDefaulted_GetRef();
			Span.Begin = Display.Len();
			// To the end of the text unless closed - a <speed=2> never closed speeds up the rest of the line.
			Span.End = MAX_int32;
			Span.Tag = Tag;
			if (Tag.bRevealLook)
			{
				Span.Appear = Tag.RevealLook.ToAppear();
			}
			if (bKeepOpen)
			{
				FOpen& Opened = Open.AddDefaulted_GetRef();
				Opened.Name = Name;
				Opened.Span = Out.Spans.Num() - 1;
			}
		};

		static const FName SpeedName(TEXT("speed"));
		static const FName InstantName(TEXT("instant"));
		static const FName ColorName(TEXT("color"));
		static const FName ColourName(TEXT("colour"));
		static const FName SizeName(TEXT("size"));
		static const FName GlowName(TEXT("glow"));
		static const FName EventName(TEXT("event"));
		static const FName PlayName(TEXT("play"));

		const int32 Length = Source.Len();
		int32 Index = 0;
		while (Index < Length)
		{
			const TCHAR Char = Source[Index];

			// Escapes, as a Rich Text Block's own parser turns them back.
			if (bRich && Char == TEXT('&'))
			{
				struct FEscape
				{
					const TCHAR* Key;
					TCHAR Value;
				};
				static const FEscape Escapes[] =
				{
					{ TEXT("&lt;"), TEXT('<') }, { TEXT("&gt;"), TEXT('>') }, { TEXT("&amp;"), TEXT('&') },
					{ TEXT("&quot;"), TEXT('"') }, { TEXT("&apos;"), TEXT('\'') }
				};
				bool bEscaped = false;
				for (const FEscape& Escape : Escapes)
				{
					const int32 EscapeLength = FCString::Strlen(Escape.Key);
					if (FCString::Strncmp(*Source + Index, Escape.Key, EscapeLength) == 0)
					{
						Display.AppendChar(Escape.Value);
						Index += EscapeLength;
						bEscaped = true;
						break;
					}
				}
				if (!bEscaped)
				{
					Display.AppendChar(Char);
					++Index;
				}
				continue;
			}

			if (Char == TEXT('<') && Index + 1 < Length)
			{
				// Closing: </> closes what was opened last; </Name> the last Name opened.
				if (Source[Index + 1] == TEXT('/'))
				{
					const int32 Close = Source.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
					if (Close != INDEX_NONE)
					{
						const FString Written = Source.Mid(Index + 2, Close - Index - 2).TrimStartAndEnd();
						bool bConsumed = false;
						if (Written.IsEmpty())
						{
							if (Open.Num() > 0)
							{
								CloseOpen(Open.Num() - 1);
								bConsumed = true;
							}
						}
						else
						{
							const FName Name(*Written);
							for (int32 OpenIndex = Open.Num() - 1; OpenIndex >= 0; --OpenIndex)
							{
								if (Open[OpenIndex].Name == Name)
								{
									CloseOpen(OpenIndex);
									bConsumed = true;
									break;
								}
							}
							// A marker stands at a point and has nothing to close: its closing tag is simply dropped.
							bConsumed = bConsumed || (bTags && IsMarkerName(Name));
						}

						// A Rich Text Block ends its style at any closing tag.
						if (!bConsumed && bRich && !CurrentStyle.IsNone())
						{
							for (int32 OpenIndex = Open.Num() - 1; OpenIndex >= 0; --OpenIndex)
							{
								if (Open[OpenIndex].bStyle)
								{
									CloseOpen(OpenIndex);
									break;
								}
							}
							bConsumed = true;
						}

						if (bConsumed)
						{
							Index = Close + 1;
							continue;
						}
					}
				}
				else if (IsNameChar(Source[Index + 1]))
				{
					FTagText Tag;
					if (ReadTag(Source, Index, Tag))
					{
						bool bConsumed = true;
						const FName Name = Tag.Name;

						if (bRich && IsStyle(Name))
						{
							// A style of the rich text's own, as a Rich Text Block reads it; one closing itself is a
							// decorator, left out.
							if (!Tag.bSelfClosing)
							{
								SwitchStyle(Name);
								FOpen& Opened = Open.AddDefaulted_GetRef();
								Opened.Name = Name;
								Opened.bStyle = true;
							}
						}
						else if (bTags && IsMarkerName(Name))
						{
							FWidgetAnimTextMarker& Marker = Out.Markers.AddDefaulted_GetRef();
							Marker.At = Display.Len();
							if (Name == EventName || Name == PlayName)
							{
								Marker.Kind = Name == EventName ? EWidgetAnimTextMarkerKind::Event : EWidgetAnimTextMarkerKind::Play;
								const FString* NameAttribute = Tag.FindAttribute(TEXT("name"));
								Marker.Name = FName(*(Tag.Value.IsEmpty() && NameAttribute ? *NameAttribute : Tag.Value).TrimStartAndEnd());
								if (Marker.Name.IsNone())
								{
									Out.Markers.Pop();
								}
							}
							else
							{
								Marker.Kind = EWidgetAnimTextMarkerKind::Pause;
								Marker.Seconds = FMath::Max(ReadNumber(Tag.Value, 0.5f), 0.f);
							}
						}
						else if (bTags && Name == SpeedName)
						{
							FWidgetAnimTextTag Speed;
							Speed.bRevealSpeed = true;
							Speed.RevealSpeed = FMath::Max(ReadNumber(Tag.Value, 2.f), 0.01f);
							OpenSpan(Speed, Name, !Tag.bSelfClosing);
						}
						else if (bTags && Name == InstantName)
						{
							FWidgetAnimTextTag Instant;
							Instant.bRevealInstant = true;
							OpenSpan(Instant, Name, !Tag.bSelfClosing);
						}
						else if (bTags && (Name == ColorName || Name == ColourName))
						{
							// Opened even when the color can't be read, so its closing tag still closes it.
							FWidgetAnimTextTag Colored;
							FLinearColor Color;
							if (ReadColor(Tag.Value, Color))
							{
								Colored.bColor = true;
								Colored.Colors.Reset();
								Colored.Colors.Add(Color);
							}
							OpenSpan(Colored, Name, !Tag.bSelfClosing);
						}
						else if (bTags && Name == SizeName)
						{
							FWidgetAnimTextTag Sized;
							Sized.bSize = true;
							Sized.Size = FMath::Max(ReadNumber(Tag.Value, 1.3f), 0.05f);
							OpenSpan(Sized, Name, !Tag.bSelfClosing);
						}
						else if (bTags && Name == GlowName)
						{
							// From the project's Glow Defaults. <glow=cyan> or <glow=#40C0FF> in that color - or, given a
							// number rather than a color, <glow=2> twice as strong. a= strength, s= pulse speed, r= radius,
							// p= pulse, and any other attribute a parameter of the glow material.
							const FWidgetAnimTextGlowDefaults& Defaults = UWidgetAnimRuntimeSettings::Get().TextGlowDefaults;
							FWidgetAnimTextTag Glowing;
							Glowing.bGlow = true;
							Glowing.bGlowInTextColor = Defaults.bInTextColor;
							Glowing.GlowColor = Defaults.Color;
							Glowing.GlowRadius = FMath::Max(Defaults.Radius, 0.5f);
							Glowing.GlowStrength = FMath::Max(Defaults.Strength, 0.f);
							Glowing.GlowPulse = FMath::Clamp(Defaults.Pulse, 0.f, 1.f);
							Glowing.GlowPulseSpeed = FMath::Max(Defaults.PulseSpeed, 0.f);

							float Amount = 1.f;
							FLinearColor Color;
							if (ReadColor(Tag.Value, Color))
							{
								Glowing.GlowColor = Color;
								Glowing.bGlowInTextColor = false;
							}
							else if (!Tag.Value.IsEmpty())
							{
								Amount = ReadNumber(Tag.Value, 1.f);
							}
							const FString* AmountAttribute = Tag.FindAttribute(TEXT("a"));
							const FString* SpeedAttribute = Tag.FindAttribute(TEXT("s"));
							Glowing.Scale(AmountAttribute ? ReadNumber(*AmountAttribute, Amount) : Amount, SpeedAttribute ? ReadNumber(*SpeedAttribute, 1.f) : 1.f);
							ApplyEffectAttributes(Tag, Glowing);
							OpenSpan(Glowing, Name, !Tag.bSelfClosing);
						}
						else if (const FWidgetAnimTextTag* Found = bTags ? FindTag(Name) : nullptr)
						{
							// <shake=2> and <shake a=2> both double its size; s= scales its speed.
							const FString* AmountAttribute = Tag.FindAttribute(TEXT("a"));
							const FString* SpeedAttribute = Tag.FindAttribute(TEXT("s"));
							const float Amount = !Tag.Value.IsEmpty() ? ReadNumber(Tag.Value, 1.f) : AmountAttribute ? ReadNumber(*AmountAttribute, 1.f) : 1.f;
							const float Speed = SpeedAttribute ? ReadNumber(*SpeedAttribute, 1.f) : 1.f;

							FWidgetAnimTextTag Scaled = *Found;
							Scaled.Scale(Amount, Speed);
							if (Scaled.bGlow)
							{
								ApplyEffectAttributes(Tag, Scaled);
							}
							if (Scaled.PlayAnimId != NAME_None)
							{
								FWidgetAnimTextMarker& Marker = Out.Markers.AddDefaulted_GetRef();
								Marker.At = Display.Len();
								Marker.Kind = EWidgetAnimTextMarkerKind::Play;
								Marker.Name = Scaled.PlayAnimId;
							}
							// A tag closing itself covers nothing - only its Play Anim ID stands where it is.
							if (!Tag.bSelfClosing)
							{
								OpenSpan(Scaled, Name, /*bKeepOpen=*/true);
							}
						}
						else if (bRich)
						{
							// A name that is neither a style of its own nor a tag: a Rich Text Block still takes it out
							// (in its default style), and leaves a decorator out altogether.
							if (!Tag.bSelfClosing)
							{
								SwitchStyle(Name);
								FOpen& Opened = Open.AddDefaulted_GetRef();
								Opened.Name = Name;
								Opened.bStyle = true;
							}
						}
						else
						{
							bConsumed = false;
						}

						if (bConsumed)
						{
							Index = Tag.End;
							continue;
						}
					}
				}
			}

			// Anything else - a '<' that opens no tag included - is text.
			Display.AppendChar(Char);
			++Index;
		}

		SwitchStyle(NAME_None);

		// Spans left open run to the end; spans that ended up covering nothing are dropped.
		for (int32 SpanIndex = Out.Spans.Num() - 1; SpanIndex >= 0; --SpanIndex)
		{
			FWidgetAnimTextSpan& Span = Out.Spans[SpanIndex];
			Span.End = FMath::Min(Span.End, Display.Len());
			if (Span.End <= Span.Begin)
			{
				Out.Spans.RemoveAt(SpanIndex);
			}
		}

		for (FWidgetAnimTextSpan& Span : Out.Spans)
		{
			ResolveEffect(Span);
			Out.bDrawsEffects = Out.bDrawsEffects || Span.Tag.DrawsEffect();
			Out.bLiveEffects = Out.bLiveEffects || (Span.Tag.DrawsEffect() && Span.Tag.IsLive());
			Out.bShapesReveal = Out.bShapesReveal || Span.Tag.bRevealSpeed || Span.Tag.bRevealInstant || Span.Tag.bRevealLook;
		}
		for (const FWidgetAnimTextMarker& Marker : Out.Markers)
		{
			Out.bShapesReveal = Out.bShapesReveal || Marker.Kind == EWidgetAnimTextMarkerKind::Pause;
		}

		// A plain text keeps no runs: it is drawn in its one style.
		if (!bRich)
		{
			Out.StyleRuns.Reset();
		}
	}

	void GetDefaultTags(TArray<FWidgetAnimTextTag>& OutTags)
	{
		OutTags.Reset();
		auto Add = [&OutTags](const TCHAR* Name) -> FWidgetAnimTextTag&
		{
			FWidgetAnimTextTag& Tag = OutTags.AddDefaulted_GetRef();
			Tag.Tag = FName(Name);
			return Tag;
		};

		{
			FWidgetAnimTextTag& Tag = Add(TEXT("shake"));
			Tag.bShake = true;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("wave"));
			Tag.bWave = true;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("bounce"));
			Tag.bWave = true;
			Tag.WaveMotion = EWidgetAnimTextWaveMotion::Hop;
			Tag.WaveAmplitude = 4.f;
			Tag.WaveLength = 6.f;
			Tag.WaveSpeed = 1.5f;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("swing"));
			Tag.bWave = true;
			Tag.WaveMotion = EWidgetAnimTextWaveMotion::Rotate;
			Tag.WaveAmplitude = 8.f;
			Tag.WaveLength = 8.f;
			Tag.WaveSpeed = 0.8f;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("pulse"));
			Tag.bPulse = true;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("blink"));
			Tag.bBlink = true;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("rainbow"));
			Tag.bRainbow = true;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("shimmer"));
			Tag.bShimmer = true;
		}
		{
			// A courtroom objection: bigger, trembling, stamped in, and fast.
			FWidgetAnimTextTag& Tag = Add(TEXT("shout"));
			Tag.bSize = true;
			Tag.Size = 1.25f;
			Tag.bShake = true;
			Tag.ShakeDistance = 1.f;
			Tag.ShakeAngle = 2.f;
			Tag.bRevealSpeed = true;
			Tag.RevealSpeed = 2.f;
			Tag.bRevealLook = true;
			Tag.RevealLook.Style = EWidgetAnimTextRevealStyle::Stomp;
			Tag.RevealLook.AppearTime = 0.18f;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("whisper"));
			Tag.bSize = true;
			Tag.Size = 0.9f;
			Tag.bColor = true;
			Tag.Colors.Reset();
			Tag.Colors.Add(FLinearColor(0.55f, 0.55f, 0.6f, 1.f));
			Tag.bRevealSpeed = true;
			Tag.RevealSpeed = 0.6f;
			Tag.bRevealLook = true;
			Tag.RevealLook.Style = EWidgetAnimTextRevealStyle::Fade;
			Tag.RevealLook.AppearTime = 0.35f;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("angry"));
			Tag.bColor = true;
			Tag.Colors.Reset();
			Tag.Colors.Add(FLinearColor(1.f, 0.18f, 0.12f, 1.f));
			Tag.bShake = true;
			Tag.ShakeDistance = 1.2f;
			Tag.bRevealSpeed = true;
			Tag.RevealSpeed = 1.8f;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("pop"));
			Tag.bRevealLook = true;
			Tag.RevealLook.Style = EWidgetAnimTextRevealStyle::Pop;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("fast"));
			Tag.bRevealSpeed = true;
			Tag.RevealSpeed = 2.5f;
		}
		{
			FWidgetAnimTextTag& Tag = Add(TEXT("slow"));
			Tag.bRevealSpeed = true;
			Tag.RevealSpeed = 0.4f;
		}
	}
}

// =====================================================================================================
// Blueprint
// =====================================================================================================

void UWidgetAnimTextLibrary::SetGlobalTextRevealSpeed(float Speed)
{
	WidgetAnimTextMarkup::GGlobalRevealSpeed = FMath::Max(Speed, 0.01f);
}

float UWidgetAnimTextLibrary::GetGlobalTextRevealSpeed()
{
	return WidgetAnimTextMarkup::GetGlobalRevealSpeed();
}

FText UWidgetAnimTextLibrary::RemoveTextTags(const FText& Text)
{
	FWidgetAnimTextMarkup Markup;
	WidgetAnimTextMarkup::Parse(Text.ToString(), /*bRich=*/false, /*bTags=*/true, [](FName) { return false; }, Markup);
	return Markup.Display.Equals(Text.ToString(), ESearchCase::CaseSensitive) ? Text : FText::FromString(Markup.Display);
}
