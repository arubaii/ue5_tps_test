// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "DiegeticTerminalText.generated.h"

class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDiegeticTerminalLineEvent, int32, LineIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDiegeticTerminalEvent);

/**
 * Terminal Text (palette NameTBD): a Text Block that works as a computer terminal's log - lines typed out a character at a time
 * with a blinking caret, older lines scrolling off the top, a key click as it types. Type Line queues a line; Print Line
 * shows one at once. Its own Text, written in the designer, is typed out line by line when it is shown: a boot sequence with
 * no Blueprint.
 *
 * For terminal screens (a Diegetic Screen with the Terminal look, a Fallout-style computer) and HUDs alike. On a Diegetic Screen
 * set to redraw When Changed, each typed character marks the screen changed; its sounds play from the screen. Everything else
 * is a Text Block's - give it a monospaced font for the classic look. For dialogue with tags and per-letter animation, use the
 * Animated Text Block instead.
 */
UCLASS(meta = (DisplayName = "Terminal Text"))
class NAMETBD_API UDiegeticTerminalText : public UTextBlock
{
	GENERATED_BODY()

public:

	UDiegeticTerminalText(const FObjectInitializer& ObjectInitializer);

	/** Characters typed a second. 0: lines appear whole. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal", meta = (ClampMin = "0", UIMin = "0", UIMax = "400"))
	float CharactersPerSecond = 60.f;

	/** Seconds it waits after typing a line before the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal", meta = (ClampMin = "0", UIMin = "0", UIMax = "2", ForceUnits = "s"))
	float LinePause = 0.08f;

	/** Lines kept; older ones scroll off the top. 0: every line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal", meta = (ClampMin = "0", UIMin = "0", UIMax = "60"))
	int32 MaxLines = 0;

	/** Types its Text (as written in the designer), line by line, when it is shown at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	bool bTypeTextWhenShown = true;

	/** A caret at the end of the last line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	bool bShowCaret = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal", meta = (EditCondition = "bShowCaret"))
	FString Caret = TEXT("_");

	/** Blinks a second; it stays lit while typing. 0: never blinks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal", meta = (ClampMin = "0", UIMin = "0", UIMax = "6", EditCondition = "bShowCaret"))
	float CaretBlinkRate = 2.f;

	/** Played as it types, every Sound Interval characters (not for spaces). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	TObjectPtr<USoundBase> TypeSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal", meta = (ClampMin = "1", UIMin = "1", UIMax = "8", EditCondition = "TypeSound != nullptr"))
	int32 SoundInterval = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal", meta = (ClampMin = "0", UIMin = "0", UIMax = "2", EditCondition = "TypeSound != nullptr"))
	float SoundVolume = 0.6f;

	/** A line has been typed out: its index among the lines typed since the last Clear Terminal. */
	UPROPERTY(BlueprintAssignable, Category = "Terminal")
	FDiegeticTerminalLineEvent OnLineTyped;

	/** Every queued line has been typed. */
	UPROPERTY(BlueprintAssignable, Category = "Terminal")
	FDiegeticTerminalEvent OnTypingFinished;

	/** Queues a line to type. A line holding line breaks types as several. */
	UFUNCTION(BlueprintCallable, Category = "Terminal")
	void TypeLine(const FText& Line);

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	void TypeLines(const TArray<FText>& Lines);

	/** Shows a line at once, after whatever is still being typed. */
	UFUNCTION(BlueprintCallable, Category = "Terminal")
	void PrintLine(const FText& Line);

	/** Types everything queued at once. */
	UFUNCTION(BlueprintCallable, Category = "Terminal")
	void FinishTyping();

	/** Empties it: every line, and everything queued. */
	UFUNCTION(BlueprintCallable, Category = "Terminal")
	void ClearTerminal();

	UFUNCTION(BlueprintPure, Category = "Terminal")
	bool IsTyping() const { return Queue.Num() > 0; }

	/** The lines shown, oldest first (the one being typed not among them). */
	UFUNCTION(BlueprintPure, Category = "Terminal")
	TArray<FString> GetTerminalLines() const { return ShownLines; }

	// UWidget
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:

	// UWidget
	virtual void OnWidgetRebuilt() override;

private:

	/** A queued line, and whether it is printed whole. */
	struct FQueuedLine
	{
		FString Line;
		bool bInstant = false;
	};

	/** Splits at line breaks into the queue. */
	void Enqueue(const FString& Line, bool bInstant);

	/** Starts its clock if anything needs it: typing, or a caret to blink. */
	void EnsureTicking();

	bool TickTerminal(float DeltaTime);

	/** A line finished: kept, the oldest dropped past Max Lines. */
	void CommitLine(const FString& Line);

	/** Its text, from the lines, the one being typed and the caret. Marks its Diegetic Screen changed. */
	void Refresh();

	TArray<FString> ShownLines;
	TArray<FQueuedLine> Queue;
	int32 TypedCharacters = 0;
	int32 LinesTyped = 0;
	double CharacterBudget = 0.0;
	float PauseLeft = 0.f;
	float CaretClock = 0.f;
	bool bCaretLit = true;
	int32 CharactersSinceSound = 0;
	bool bTypedAuthoredText = false;
	FTSTicker::FDelegateHandle TickerHandle;
};
