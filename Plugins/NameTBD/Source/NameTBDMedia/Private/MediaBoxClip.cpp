// Copyright AR GameSystems 2026. All rights reserved.

#include "MediaBoxClip.h"

#include "MediaBoxLog.h"
#include "MediaBoxSettings.h"

#include "Engine/Texture.h"
#include "FileMediaSource.h"
#include "HAL/FileManager.h"
#include "MediaSource.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Containers/Ticker.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/DataValidation.h"
#include "Misc/PackageName.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#define LOCTEXT_NAMESPACE "MediaBoxClip"

EMediaBoxAudio UMediaBoxClip::GetResolvedAudio() const
{
	if (Audio == EMediaBoxAudio::Auto)
	{
		return Sound ? EMediaBoxAudio::SeparateSound : EMediaBoxAudio::Embedded;
	}
	return Audio;
}

FName UMediaBoxClip::GetPlayerName() const
{
	return PlayerName.IsNone() ? UMediaBoxSettings::Get().PlayerName : PlayerName;
}

void UMediaBoxClip::PostLoad()
{
	Super::PostLoad();

	// Pasted text never read (an asset saved by hand, or made in code): read it now.
	if (Subtitles.Num() == 0 && !SubtitleText.IsEmpty())
	{
		MediaBoxSubtitles::Parse(SubtitleText, Subtitles);
	}
}

#if WITH_EDITOR
void UMediaBoxClip::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UMediaBoxClip, SubtitleText))
	{
		MediaBoxSubtitles::Parse(SubtitleText, Subtitles);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

namespace MediaBoxClipPrivate
{
	/**
	 * Whether an MP4 / MOV file has its index (moov) before its media data (mdat) - "fast start": a player can start it without
	 * reading to the end of the file. Unset when the file can't be read or isn't laid out as boxes.
	 */
	TOptional<bool> IsFastStart(const FString& Path)
	{
		const TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path));
		if (!Reader)
		{
			return {};
		}

		const int64 FileSize = Reader->TotalSize();
		int64 Offset = 0;
		bool bSeenMediaData = false;
		for (int32 BoxIndex = 0; BoxIndex < 64 && Offset + 8 <= FileSize; ++BoxIndex)
		{
			uint8 Header[16];
			Reader->Seek(Offset);
			Reader->Serialize(Header, 8);
			uint64 BoxSize = (uint64(Header[0]) << 24) | (uint64(Header[1]) << 16) | (uint64(Header[2]) << 8) | uint64(Header[3]);
			const bool bIsIndex = FMemory::Memcmp(Header + 4, "moov", 4) == 0;
			const bool bIsMediaData = FMemory::Memcmp(Header + 4, "mdat", 4) == 0;
			if (BoxSize == 1 && Offset + 16 <= FileSize)
			{
				Reader->Serialize(Header + 8, 8);
				BoxSize = 0;
				for (int32 ByteIndex = 8; ByteIndex < 16; ++ByteIndex)
				{
					BoxSize = (BoxSize << 8) | uint64(Header[ByteIndex]);
				}
			}
			else if (BoxSize == 0)
			{
				BoxSize = uint64(FileSize - Offset);
			}

			if (bIsIndex)
			{
				return !bSeenMediaData;
			}
			if (bIsMediaData)
			{
				bSeenMediaData = true;
			}
			if (BoxSize < 8)
			{
				return {};
			}
			Offset += int64(BoxSize);
		}
		return {};
	}
}

EDataValidationResult UMediaBoxClip::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!MediaSource)
	{
		Context.AddError(LOCTEXT("NoSource", "No Media Source: the clip plays nothing."));
		return EDataValidationResult::Invalid;
	}
	if (!MediaSource->Validate())
	{
		Context.AddError(FText::Format(LOCTEXT("SourceInvalid", "Its Media Source {0} isn't valid - is the file there?"), FText::FromString(MediaSource->GetName())));
		Result = EDataValidationResult::Invalid;
	}

	if (const UFileMediaSource* FileSource = Cast<UFileMediaSource>(MediaSource))
	{
		const FString Path = FileSource->GetFullPath();
		const FString Extension = FPaths::GetExtension(Path).ToLower();
		if (Extension == TEXT("mp4") || Extension == TEXT("m4v") || Extension == TEXT("mov"))
		{
			const TOptional<bool> bFastStart = MediaBoxClipPrivate::IsFastStart(Path);
			if (bFastStart.IsSet() && !bFastStart.GetValue())
			{
				Context.AddWarning(FText::Format(LOCTEXT("NotFastStart", "{0} has its index at the end of the file: it opens slower. Re-encode it with the index first (ffmpeg: -movflags +faststart)."), FText::FromString(FPaths::GetCleanFilename(Path))));
			}
		}
	}

	if (!Poster)
	{
		Context.AddWarning(LOCTEXT("NoPoster", "No Poster: the Media Box shows nothing until the first frame arrives. Generate Poster makes one from the video."));
	}
	if (Audio == EMediaBoxAudio::SeparateSound && !Sound)
	{
		Context.AddError(LOCTEXT("NoSound", "Audio is Separate Sound but there is no Sound."));
		Result = EDataValidationResult::Invalid;
	}
	if (bLoop && LoopEnd > 0.f && LoopEnd <= LoopStart)
	{
		Context.AddError(LOCTEXT("BadLoop", "Loop End is at or before Loop Start."));
		Result = EDataValidationResult::Invalid;
	}

	return Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result;
}

// ---------------------------------------------------------------------------------------------
// Generate Poster
// ---------------------------------------------------------------------------------------------

namespace MediaBoxPosterMaker
{
	/** One poster being made: a player of its own, opened, played to Poster Time, paused, its frame copied out. */
	struct FJob
	{
		TWeakObjectPtr<UMediaBoxClip> Clip;
		TStrongObjectPtr<UMediaPlayer> Player;
		TStrongObjectPtr<UMediaTexture> Texture;
		float At = 0.f;
		int32 Step = 0;
		int32 Frames = 0;
		double StartedAt = 0.0;
	};

	void Notify(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 4.f : 8.f;
		FSlateNotificationManager::Get().AddNotification(Info);
		if (!bSuccess)
		{
			UE_LOG(LogMediaBox, Warning, TEXT("%s"), *Message.ToString());
		}
	}

	/** Copies the media texture's frame into a texture asset next to the clip (or into its existing poster) and sets it. */
	bool Capture(FJob& Job)
	{
		UMediaBoxClip* PosterClip = Job.Clip.Get();
		UMediaTexture* Source = Job.Texture.Get();
		const int32 Width = Source ? Source->GetWidth() : 0;
		const int32 Height = Source ? Source->GetHeight() : 0;
		if (!PosterClip || Width <= 0 || Height <= 0 || !Source->GetResource())
		{
			return false;
		}

		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA8;
		Target->ClearColor = FLinearColor::Black;
		Target->InitAutoFormat(Width, Height);
		Target->UpdateResourceImmediate(true);

		FTextureRenderTargetResource* TargetResource = Target->GameThread_GetRenderTargetResource();
		if (!TargetResource)
		{
			return false;
		}
		{
			FCanvas Canvas(TargetResource, nullptr, FGameTime::GetTimeSinceAppStart(), GMaxRHIFeatureLevel);
			FCanvasTileItem Tile(FVector2D::ZeroVector, Source->GetResource(), FVector2D(Width, Height), FLinearColor::White);
			Tile.BlendMode = SE_BLEND_Opaque;
			Canvas.DrawItem(Tile);
			Canvas.Flush_GameThread(true);
		}
		FlushRenderingCommands();

		TArray<FColor> Pixels;
		if (!TargetResource->ReadPixels(Pixels) || Pixels.Num() != Width * Height)
		{
			return false;
		}
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
		}

		// Into the clip's poster when it is one made here before (same name), else a new texture <clip>_Poster.
		const FString AssetName = PosterClip->GetName() + TEXT("_Poster");
		const FString PackageName = FPackageName::GetLongPackagePath(PosterClip->GetOutermost()->GetName()) / AssetName;
		UTexture2D* PosterTexture = Cast<UTexture2D>(PosterClip->Poster.Get());
		if (!PosterTexture || PosterTexture->GetName() != AssetName)
		{
			UPackage* Package = CreatePackage(*PackageName);
			PosterTexture = FindObject<UTexture2D>(Package, *AssetName);
			if (!PosterTexture)
			{
				PosterTexture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
				FAssetRegistryModule::AssetCreated(PosterTexture);
			}
		}

		PosterTexture->Modify();
		PosterTexture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		PosterTexture->SRGB = true;
		PosterTexture->LODGroup = TEXTUREGROUP_UI;
		PosterTexture->MipGenSettings = TMGS_NoMipmaps;
		PosterTexture->CompressionSettings = TC_Default;
		PosterTexture->PostEditChange();
		PosterTexture->MarkPackageDirty();

		PosterClip->Modify();
		PosterClip->Poster = PosterTexture;
		PosterClip->PostEditChange();
		PosterClip->MarkPackageDirty();
		return true;
	}

	/** Advances a job; false once it is done (made or failed). */
	bool TickJob(const TSharedRef<FJob>& Job)
	{
		UMediaPlayer* Player = Job->Player.Get();
		UMediaBoxClip* PosterClip = Job->Clip.Get();
		if (!Player || !PosterClip)
		{
			return false;
		}

		const double Elapsed = FPlatformTime::Seconds() - Job->StartedAt;
		if (Player->HasError() || Elapsed > 15.0)
		{
			Notify(FText::Format(LOCTEXT("PosterFailed", "Generate Poster: couldn't play {0} (is its player plugin enabled in the editor?)."), FText::FromString(PosterClip->GetName())), false);
			Player->Close();
			return false;
		}

		++Job->Frames;
		switch (Job->Step)
		{
		case 0:
			// Opening.
			if (Player->IsReady())
			{
				if (Job->At > 0.f && Player->SupportsSeeking())
				{
					Player->Seek(FTimespan::FromSeconds(Job->At));
				}
				Player->Play();
				Job->Step = 1;
				Job->Frames = 0;
			}
			return true;
		case 1:
			// Playing, until a frame past Poster Time is in.
			if (Player->GetTime().GetTotalSeconds() > Job->At + 0.02 || Job->Frames > 120)
			{
				Player->Pause();
				Job->Step = 2;
				Job->Frames = 0;
			}
			return true;
		default:
			// A few frames for the texture to catch up, then the copy.
			if (Job->Frames < 4)
			{
				return true;
			}
			if (Capture(*Job))
			{
				Notify(FText::Format(LOCTEXT("PosterMade", "Generate Poster: {0} has its poster."), FText::FromString(PosterClip->GetName())), true);
			}
			else
			{
				Notify(FText::Format(LOCTEXT("PosterNoFrame", "Generate Poster: no frame came from {0}."), FText::FromString(PosterClip->GetName())), false);
			}
			Player->Close();
			return false;
		}
	}
}

void UMediaBoxClip::GeneratePoster()
{
	using namespace MediaBoxPosterMaker;

	if (!MediaSource)
	{
		Notify(LOCTEXT("PosterNoSource", "Generate Poster: the clip has no Media Source."), false);
		return;
	}

	const TSharedRef<FJob> Job = MakeShared<FJob>();
	Job->Clip = this;
	Job->At = PosterTime >= 0.f ? PosterTime : StartTime;
	Job->StartedAt = FPlatformTime::Seconds();
	Job->Player.Reset(NewObject<UMediaPlayer>(GetTransientPackage(), NAME_None, RF_Transient));
	Job->Player->PlayOnOpen = false;
	Job->Player->SetDesiredPlayerName(GetPlayerName());
	Job->Texture.Reset(NewObject<UMediaTexture>(GetTransientPackage(), NAME_None, RF_Transient));
	Job->Texture->AutoClear = false;
	Job->Texture->SetMediaPlayer(Job->Player.Get());
	Job->Texture->UpdateResource();

	if (!Job->Player->OpenSource(MediaSource))
	{
		Notify(FText::Format(LOCTEXT("PosterOpenFailed", "Generate Poster: couldn't open {0}."), FText::FromString(MediaSource->GetUrl())), false);
		return;
	}

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Job](float)
	{
		return TickJob(Job);
	}));
}
#endif // WITH_EDITOR

// =====================================================================================================
// Subtitles
// =====================================================================================================

namespace MediaBoxSubtitlesPrivate
{
	/** "01:02:03,500", "02:03.500", "3.5" - seconds, or below 0 when it isn't a time. */
	double ParseTime(FString Text)
	{
		Text.TrimStartAndEndInline();
		Text.ReplaceInline(TEXT(","), TEXT("."));
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(":"), /*InCullEmpty=*/false);
		if (Parts.Num() == 0 || Parts.Num() > 3)
		{
			return -1.0;
		}
		double Seconds = 0.0;
		for (const FString& Part : Parts)
		{
			if (Part.IsEmpty() || !Part.IsNumeric())
			{
				return -1.0;
			}
			Seconds = Seconds * 60.0 + FCString::Atod(*Part);
		}
		return Seconds;
	}

	/** Takes out <i>, </b>, <c.yellow>, {\an8}-style markup. */
	FString StripTags(const FString& Line)
	{
		FString Out;
		Out.Reserve(Line.Len());
		int32 Depth = 0;
		for (const TCHAR Char : Line)
		{
			if (Char == TEXT('<') || Char == TEXT('{'))
			{
				++Depth;
			}
			else if ((Char == TEXT('>') || Char == TEXT('}')) && Depth > 0)
			{
				--Depth;
			}
			else if (Depth == 0)
			{
				Out.AppendChar(Char);
			}
		}
		return Out.TrimStartAndEnd();
	}
}

void MediaBoxSubtitles::Parse(const FString& Source, TArray<FMediaBoxSubtitleCue>& OutCues)
{
	using namespace MediaBoxSubtitlesPrivate;

	OutCues.Reset();

	FString Normalized = Source;
	Normalized.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	Normalized.ReplaceInline(TEXT("\r"), TEXT("\n"));
	TArray<FString> Lines;
	Normalized.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);

	int32 LineIndex = 0;
	while (LineIndex < Lines.Num())
	{
		FString Line = Lines[LineIndex].TrimStartAndEnd();
		if (Line.Len() > 0 && Line[0] == TCHAR(0xFEFF))
		{
			Line.RightChopInline(1);
		}

		// WebVTT blocks that aren't cues: skipped to the next blank line.
		if (Line.StartsWith(TEXT("NOTE")) || Line.StartsWith(TEXT("STYLE")) || Line.StartsWith(TEXT("REGION")))
		{
			while (LineIndex < Lines.Num() && !Lines[LineIndex].TrimStartAndEnd().IsEmpty())
			{
				++LineIndex;
			}
			continue;
		}

		const int32 Arrow = Line.Find(TEXT("-->"));
		if (Arrow == INDEX_NONE)
		{
			++LineIndex;
			continue;
		}

		// The end time is the first word after the arrow; WebVTT cue settings may follow it.
		FString EndText = Line.Mid(Arrow + 3).TrimStart();
		int32 Space = INDEX_NONE;
		if (EndText.FindChar(TEXT(' '), Space) || EndText.FindChar(TEXT('\t'), Space))
		{
			EndText.LeftInline(Space);
		}
		const double Start = ParseTime(Line.Left(Arrow));
		const double End = ParseTime(EndText);

		// Its text: the lines under it, up to a blank line.
		++LineIndex;
		TArray<FString> TextLines;
		while (LineIndex < Lines.Num() && !Lines[LineIndex].TrimStartAndEnd().IsEmpty())
		{
			const FString Stripped = StripTags(Lines[LineIndex]);
			if (!Stripped.IsEmpty())
			{
				TextLines.Add(Stripped);
			}
			++LineIndex;
		}

		if (Start >= 0.0 && End > Start && TextLines.Num() > 0)
		{
			FMediaBoxSubtitleCue& Cue = OutCues.AddDefaulted_GetRef();
			Cue.Start = static_cast<float>(Start);
			Cue.End = static_cast<float>(End);
			Cue.Text = FText::FromString(FString::Join(TextLines, TEXT("\n")));
		}
	}

	OutCues.StableSort([](const FMediaBoxSubtitleCue& A, const FMediaBoxSubtitleCue& B) { return A.Start < B.Start; });
}

int32 MediaBoxSubtitles::FindCue(TConstArrayView<FMediaBoxSubtitleCue> Cues, float Seconds)
{
	int32 Found = INDEX_NONE;
	for (int32 CueIndex = 0; CueIndex < Cues.Num(); ++CueIndex)
	{
		const FMediaBoxSubtitleCue& Cue = Cues[CueIndex];
		if (Cue.Start > Seconds)
		{
			break;
		}
		if (Seconds < Cue.End)
		{
			Found = CueIndex;
		}
	}
	return Found;
}

#undef LOCTEXT_NAMESPACE
