// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAIDocs.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace NameTBDAIDocs
{
	namespace
	{
		struct FIndex
		{
			TArray<FTopic> Topics;
			/** Per topic: term -> count (title terms counted three times). */
			TArray<TMap<FString, int32>> Terms;
			TArray<int32> Lengths;
			TMap<FString, int32> DocumentFrequency;
			float AverageLength = 1.f;
			bool bBuilt = false;
		};

		FIndex& GetIndex()
		{
			static FIndex Index;
			return Index;
		}

		const TSet<FString>& StopWords()
		{
			static const TSet<FString> Words = {
				TEXT("the"), TEXT("a"), TEXT("an"), TEXT("and"), TEXT("or"), TEXT("of"), TEXT("to"), TEXT("in"), TEXT("on"), TEXT("for"),
				TEXT("is"), TEXT("it"), TEXT("its"), TEXT("by"), TEXT("as"), TEXT("at"), TEXT("be"), TEXT("this"), TEXT("that"), TEXT("with"),
				TEXT("from"), TEXT("are"), TEXT("what"), TEXT("how"), TEXT("do"), TEXT("i"), TEXT("can"), TEXT("when"), TEXT("which"), TEXT("not"),
			};
			return Words;
		}

		/** Lowercase words, split at CamelCase too ("FlowList" -> flowlist, flow, list), plurals folded. */
		void Tokenize(const FString& Text, TArray<FString>& Out)
		{
			auto AddToken = [&Out](FString Token)
			{
				Token.ToLowerInline();
				if (Token.Len() > 3 && Token.EndsWith(TEXT("s")) && !Token.EndsWith(TEXT("ss")))
				{
					Token.LeftChopInline(1);
				}
				if (Token.Len() > 1 && !StopWords().Contains(Token))
				{
					Out.Add(MoveTemp(Token));
				}
			};
			auto AddWord = [&AddToken](const FString& Word)
			{
				if (Word.IsEmpty())
				{
					return;
				}
				AddToken(Word);
				TArray<FString> Parts;
				FString Part;
				for (int32 Index = 0; Index < Word.Len(); ++Index)
				{
					const TCHAR Char = Word[Index];
					if (FChar::IsUpper(Char) && !Part.IsEmpty() && FChar::IsLower(Part[Part.Len() - 1]))
					{
						Parts.Add(Part);
						Part.Reset();
					}
					Part.AppendChar(Char);
				}
				Parts.Add(Part);
				if (Parts.Num() > 1)
				{
					for (const FString& Each : Parts)
					{
						AddToken(Each);
					}
				}
			};
			FString Word;
			for (const TCHAR Char : Text)
			{
				if (FChar::IsAlnum(Char))
				{
					Word.AppendChar(Char);
				}
				else
				{
					AddWord(Word);
					Word.Reset();
				}
			}
			AddWord(Word);
		}

		FString FirstSentence(const FString& Text, int32 MaxChars = 180)
		{
			FString Clean = Text.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
			int32 End = INDEX_NONE;
			for (int32 Index = 0; Index < Clean.Len() - 1; ++Index)
			{
				if (Clean[Index] == TEXT('.') && Clean[Index + 1] == TEXT(' '))
				{
					End = Index + 1;
					break;
				}
			}
			FString Out = End != INDEX_NONE ? Clean.Left(End) : Clean;
			return Out.Len() > MaxChars ? Out.Left(MaxChars - 3) + TEXT("...") : Out;
		}

		// ---- Sources ---------------------------------------------------------------------------------------------------

		/** Docs/*.md: "# Title" for the file, "## Heading" starts a topic of its own. */
		void AddGuides(TArray<FTopic>& Out)
		{
			const FString Folder = GetResourcesDir() / TEXT("Docs");
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.md")), true, false);
			Files.Sort();
			for (const FString& File : Files)
			{
				FString Text;
				if (!FFileHelper::LoadFileToString(Text, *(Folder / File)))
				{
					continue;
				}
				TArray<FString> Lines;
				Text.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
				const FString Stem = FPaths::GetBaseFilename(File);
				FString FileTitle = Stem;
				FTopic Current;
				Current.Kind = TEXT("guide");
				auto Finish = [&Out, &Current, &Stem]()
				{
					Current.Text.TrimStartAndEndInline();
					if (!Current.Text.IsEmpty())
					{
						if (Current.Id.IsEmpty())
						{
							Current.Id = Stem;
						}
						Out.Add(Current);
					}
					Current.Text.Reset();
				};
				for (const FString& Line : Lines)
				{
					if (Line.StartsWith(TEXT("# ")))
					{
						FileTitle = Line.Mid(2).TrimStartAndEnd();
						Current.Title = FileTitle;
						Current.Id = Stem;
					}
					else if (Line.StartsWith(TEXT("## ")))
					{
						Finish();
						const FString Heading = Line.Mid(3).TrimStartAndEnd();
						Current.Title = FString::Printf(TEXT("%s: %s"), *FileTitle, *Heading);
						FString Slug;
						for (const TCHAR Char : Heading.ToLower())
						{
							Slug.AppendChar(FChar::IsAlnum(Char) ? Char : TEXT('-'));
						}
						Current.Id = Stem + TEXT("/") + Slug;
					}
					else
					{
						Current.Text += Line + TEXT("\n");
					}
				}
				Finish();
			}
		}

		bool IsDocumentedPackage(const UObject* Field)
		{
			const FString Package = Field->GetOutermost()->GetName();
			return Package.StartsWith(TEXT("/Script/NameTBD")) && Package != TEXT("/Script/NameTBDAI");
		}

		FString DescribeFieldsOf(const UStruct* Struct)
		{
			FString Text;
			for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const FProperty* Property = *It;
				if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Deprecated))
				{
					continue;
				}
				const FString Tip = Property->GetMetaData(TEXT("ToolTip"));
				const FString Display = Property->GetMetaData(TEXT("DisplayName"));
				Text += FString::Printf(TEXT("- %s%s (%s)%s%s\n"), *Property->GetAuthoredName(),
					Display.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" \"%s\""), *Display),
					*Property->GetCPPType(), Tip.IsEmpty() ? TEXT("") : TEXT(": "), *FirstSentence(Tip, 220));
			}
			return Text;
		}

		/** Every class, struct and enum of the plugin's modules, from the code's own comments. */
		void AddReflection(TArray<FTopic>& Out)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				const UClass* Class = *It;
				if (!IsDocumentedPackage(Class) || Class->HasAnyClassFlags(CLASS_Deprecated) || Class->GetName().StartsWith(TEXT("SKEL_")))
				{
					continue;
				}
				FTopic Topic;
				Topic.Kind = TEXT("class");
				Topic.Id = TEXT("class:") + Class->GetName();
				const FString Display = Class->GetMetaData(TEXT("DisplayName"));
				Topic.Title = Display.IsEmpty() ? Class->GetName() : FString::Printf(TEXT("%s (%s)"), *Display, *Class->GetName());
				Topic.Text = Class->GetMetaData(TEXT("ToolTip"));
				if (const UClass* Super = Class->GetSuperClass())
				{
					Topic.Text += FString::Printf(TEXT("\nDerives from %s.\n"), *Super->GetName());
				}
				const FString Fields = DescribeFieldsOf(Class);
				if (!Fields.IsEmpty())
				{
					Topic.Text += TEXT("\nSettings:\n") + Fields;
				}
				if (!Topic.Text.TrimStartAndEnd().IsEmpty())
				{
					Out.Add(MoveTemp(Topic));
				}
			}
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				const UScriptStruct* Struct = *It;
				if (!IsDocumentedPackage(Struct))
				{
					continue;
				}
				FTopic Topic;
				Topic.Kind = TEXT("struct");
				Topic.Id = TEXT("struct:F") + Struct->GetName();
				Topic.Title = TEXT("F") + Struct->GetName();
				Topic.Text = Struct->GetMetaData(TEXT("ToolTip"));
				const FString Fields = DescribeFieldsOf(Struct);
				if (!Fields.IsEmpty())
				{
					Topic.Text += TEXT("\nFields:\n") + Fields;
				}
				if (!Topic.Text.TrimStartAndEnd().IsEmpty())
				{
					Out.Add(MoveTemp(Topic));
				}
			}
			for (TObjectIterator<UEnum> It; It; ++It)
			{
				const UEnum* Enum = *It;
				if (!IsDocumentedPackage(Enum))
				{
					continue;
				}
				FTopic Topic;
				Topic.Kind = TEXT("enum");
				Topic.Id = TEXT("enum:") + Enum->GetName();
				Topic.Title = Enum->GetName();
				Topic.Text = Enum->GetMetaData(TEXT("ToolTip")) + TEXT("\nValues:\n");
				for (int32 Index = 0; Index < Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0); ++Index)
				{
					if (Enum->HasMetaData(TEXT("Hidden"), Index))
					{
						continue;
					}
					const FString Tip = Enum->GetToolTipTextByIndex(Index).ToString();
					Topic.Text += FString::Printf(TEXT("- %s (\"%s\")%s%s\n"), *Enum->GetNameStringByIndex(Index), *Enum->GetDisplayNameTextByIndex(Index).ToString(),
						Tip.IsEmpty() ? TEXT("") : TEXT(": "), *FirstSentence(Tip, 220));
				}
				Out.Add(MoveTemp(Topic));
			}
		}

		/** NameTBDTooltips.h's help strings, read from the plugin's source when it's shipped with it. */
		void AddEditorHelp(TArray<FTopic>& Out)
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD"));
			if (!Plugin.IsValid())
			{
				return;
			}
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("Source/NameTBDEditor/Public/NameTBDTooltips.h"))))
			{
				return;
			}

			// NSLOCTEXT("Namespace", "Key", "text" "more text")
			TMap<FString, TArray<FString>> ByGroup;
			int32 Search = 0;
			while (true)
			{
				const int32 Start = Text.Find(TEXT("NSLOCTEXT("), ESearchCase::CaseSensitive, ESearchDir::FromStart, Search);
				if (Start == INDEX_NONE)
				{
					break;
				}
				int32 Cursor = Start + 10;
				TArray<FString> Literals;
				FString Current;
				bool bInString = false;
				for (; Cursor < Text.Len(); ++Cursor)
				{
					const TCHAR Char = Text[Cursor];
					if (bInString)
					{
						if (Char == TEXT('\\') && Cursor + 1 < Text.Len())
						{
							const TCHAR Next = Text[++Cursor];
							Current.AppendChar(Next == TEXT('n') ? TEXT('\n') : Next);
						}
						else if (Char == TEXT('"'))
						{
							bInString = false;
						}
						else
						{
							Current.AppendChar(Char);
						}
					}
					else if (Char == TEXT('"'))
					{
						bInString = true;
					}
					else if (Char == TEXT(','))
					{
						Literals.Add(Current);
						Current.Reset();
					}
					else if (Char == TEXT(')'))
					{
						Literals.Add(Current);
						break;
					}
				}
				Search = Cursor;
				if (Literals.Num() == 3)
				{
					FString Group = Literals[0];
					Group.RemoveFromStart(TEXT("NameTBDTooltips."));
					ByGroup.FindOrAdd(Group).Add(FString::Printf(TEXT("- %s: %s"), *Literals[1].Replace(TEXT("Tooltip"), TEXT("")), *Literals[2]));
				}
			}

			for (const TPair<FString, TArray<FString>>& Pair : ByGroup)
			{
				constexpr int32 PerTopic = 24;
				for (int32 First = 0; First < Pair.Value.Num(); First += PerTopic)
				{
					FTopic Topic;
					Topic.Kind = TEXT("editor help");
					const int32 Part = First / PerTopic + 1;
					Topic.Id = FString::Printf(TEXT("help:%s/%d"), *Pair.Key, Part);
					Topic.Title = FString::Printf(TEXT("Editor help: %s (%d)"), *Pair.Key, Part);
					for (int32 Index = First; Index < FMath::Min(First + PerTopic, Pair.Value.Num()); ++Index)
					{
						Topic.Text += Pair.Value[Index] + TEXT("\n");
					}
					Out.Add(MoveTemp(Topic));
				}
			}
		}

		void Build(FIndex& Index)
		{
			Index.Topics.Reset();
			AddGuides(Index.Topics);
			AddReflection(Index.Topics);
			AddEditorHelp(Index.Topics);

			Index.Terms.Reset();
			Index.Lengths.Reset();
			Index.DocumentFrequency.Reset();
			int64 Total = 0;
			for (const FTopic& Topic : Index.Topics)
			{
				TArray<FString> Tokens;
				Tokenize(Topic.Text, Tokens);
				TArray<FString> TitleTokens;
				Tokenize(Topic.Title, TitleTokens);
				for (int32 Repeat = 0; Repeat < 3; ++Repeat)
				{
					Tokens.Append(TitleTokens);
				}
				TMap<FString, int32>& Counts = Index.Terms.AddDefaulted_GetRef();
				for (const FString& Token : Tokens)
				{
					++Counts.FindOrAdd(Token);
				}
				for (const TPair<FString, int32>& Pair : Counts)
				{
					++Index.DocumentFrequency.FindOrAdd(Pair.Key);
				}
				Index.Lengths.Add(Tokens.Num());
				Total += Tokens.Num();
			}
			Index.AverageLength = Index.Topics.Num() > 0 ? FMath::Max(1.f, static_cast<float>(Total) / Index.Topics.Num()) : 1.f;
			Index.bBuilt = true;
			UE_LOG(LogTemp, Log, TEXT("NameTBD AI: docs index built - %d topics."), Index.Topics.Num());
		}
	}

	FString GetResourcesDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD"));
		const FString Base = Plugin.IsValid() ? Plugin->GetBaseDir() : FPaths::ProjectPluginsDir() / TEXT("NameTBD");
		return Base / TEXT("Resources") / TEXT("AI");
	}

	FString LoadResourceText(const FString& RelativePath)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *(GetResourcesDir() / RelativePath));
		return Text;
	}

	const FString& GetMap()
	{
		static FString Map;
		if (Map.IsEmpty())
		{
			Map = LoadResourceText(TEXT("Map.md"));
			// Line endings fixed so the text - and with it the provider's prompt cache - is the same on every machine.
			Map.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
			Map.TrimStartAndEndInline();
			if (Map.IsEmpty())
			{
				Map = TEXT("You build and edit Unreal Engine UI with the NameTBD plugin's tools. Use search_docs / get_docs to learn the plugin.");
			}
		}
		return Map;
	}

	void EnsureIndex()
	{
		FIndex& Index = GetIndex();
		if (!Index.bBuilt)
		{
			Build(Index);
		}
	}

	TArray<FHit> Search(const FString& Query, int32 MaxHits)
	{
		EnsureIndex();
		const FIndex& Index = GetIndex();
		TArray<FString> QueryTokens;
		Tokenize(Query, QueryTokens);
		TSet<FString> Unique(QueryTokens);

		constexpr float K1 = 1.2f;
		constexpr float B = 0.75f;
		const float Count = static_cast<float>(FMath::Max(Index.Topics.Num(), 1));

		TArray<FHit> Hits;
		for (int32 TopicIndex = 0; TopicIndex < Index.Topics.Num(); ++TopicIndex)
		{
			float Score = 0.f;
			const TMap<FString, int32>& Terms = Index.Terms[TopicIndex];
			for (const FString& Token : Unique)
			{
				const int32* Frequency = Terms.Find(Token);
				if (!Frequency)
				{
					continue;
				}
				const int32 Documents = Index.DocumentFrequency.FindRef(Token);
				const float Idf = FMath::Loge(1.f + (Count - Documents + 0.5f) / (Documents + 0.5f));
				const float Tf = static_cast<float>(*Frequency);
				Score += Idf * (Tf * (K1 + 1.f)) / (Tf + K1 * (1.f - B + B * Index.Lengths[TopicIndex] / Index.AverageLength));
			}
			// Hand-written guides answer "how do I" better than a field list of the same words.
			if (Score > 0.f && Index.Topics[TopicIndex].Kind == TEXT("guide"))
			{
				Score *= 1.35f;
			}
			if (Score > 0.f)
			{
				FHit& Hit = Hits.AddDefaulted_GetRef();
				Hit.Topic = &Index.Topics[TopicIndex];
				Hit.Score = Score;
			}
		}
		Hits.Sort([](const FHit& A, const FHit& Bh) { return A.Score > Bh.Score; });
		if (Hits.Num() > MaxHits)
		{
			Hits.SetNum(MaxHits);
		}

		// A snippet: the first line that holds a query word.
		for (FHit& Hit : Hits)
		{
			TArray<FString> Lines;
			Hit.Topic->Text.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				TArray<FString> LineTokens;
				Tokenize(Line, LineTokens);
				if (LineTokens.ContainsByPredicate([&Unique](const FString& Token) { return Unique.Contains(Token); }))
				{
					Hit.Snippet = FirstSentence(Line.TrimStartAndEnd(), 200);
					break;
				}
			}
			if (Hit.Snippet.IsEmpty())
			{
				Hit.Snippet = FirstSentence(Hit.Topic->Text, 200);
			}
		}
		return Hits;
	}

	const FTopic* Find(const FString& IdOrTitle)
	{
		EnsureIndex();
		const FIndex& Index = GetIndex();
		const FString Wanted = IdOrTitle.TrimStartAndEnd();
		for (const FTopic& Topic : Index.Topics)
		{
			if (Topic.Id.Equals(Wanted, ESearchCase::IgnoreCase) || Topic.Title.Equals(Wanted, ESearchCase::IgnoreCase))
			{
				return &Topic;
			}
		}
		// "FlowList" -> class:FlowList; "FWidgetShapePaint" -> struct:FWidgetShapePaint.
		for (const TCHAR* Prefix : { TEXT("class:"), TEXT("class:U"), TEXT("struct:"), TEXT("struct:F"), TEXT("enum:") })
		{
			for (const FTopic& Topic : Index.Topics)
			{
				if (Topic.Id.Equals(FString(Prefix) + Wanted, ESearchCase::IgnoreCase))
				{
					return &Topic;
				}
			}
		}
		const TArray<FHit> Hits = Search(Wanted, 1);
		return Hits.Num() > 0 && Hits[0].Score > 2.f ? Hits[0].Topic : nullptr;
	}

	void GetTopics(TArray<const FTopic*>& OutTopics)
	{
		EnsureIndex();
		for (const FTopic& Topic : GetIndex().Topics)
		{
			OutTopics.Add(&Topic);
		}
	}
}
