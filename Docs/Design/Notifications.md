# Notifications: toasts, banners, feeds, hints, a notification center

> **Standing expectation (the request's own words):** "There could be more use cases that users desire that I haven't thought of, but would be expected in a production-grade application." The section *Beyond the request* lists what was added for it; *Still open* lists what is still expected and not done.

## Asked
"Create a UI notification system. It should be AAA ready and production-grade. Research how the AAA standard is for notification systems in games, and then create it."

## What AAA notification systems do (the research this is built on)
- **One router, many presenters.** Gameplay posts *data*; the UI decides where and how it shows. Lyra routes HUD content through gameplay-tag-keyed hosts (UI Extension points, the accolade host per location tag, the elimination feed off the Gameplay Message Subsystem); CommonUI has a FIFO activatable-widget queue. Here: a per-player subsystem routes by **channel tag** to **Notification Host** widgets.
- **Channels by cost of missing it.** Critical interrupts (connection lost), contextual toasts (achievement, invite), ambient feeds (loot, XP, kills), hints, banners (objective updated) - each its own place, capacity and pacing. Transient alerts stay out of the persistent HUD.
- **Priority and queueing:** a cap on screen, the rest queued by priority then age; higher priority may push lower off (after a minimum on screen); stale items expire in the queue (a kill line 10 s late is noise); full queues drop the least important.
- **Coalescing:** repeats merge ("Wood x3", "+150 XP", 5 sales = 1 toast in WoW's loot toasts, the timer reset when it grows), identical errors are throttled ("Inventory full" pressed ten times), a newer event cancels an older one (Lyra's accolades: "Double Kill" cancels "Kill").
- **Timing:** long enough to read (reading-time scaling), paused while hovered, while paused or in a cutscene; nothing lost while the HUD is hidden.
- **Interaction:** actions (Accept / Decline) with **controller shortcuts that work without focus** (hold to confirm when the key is shared with gameplay), a default action on timeout, click to open, swipe / close to dismiss; toasts never steal focus.
- **Accessibility** (Xbox Accessibility Guidelines XAG 116 - adjustable time limits up to 10x, or "until dismissed"; Game Accessibility Guidelines): duration scaling, keep-until-dismissed, screen-reader announcements (`AnnounceAccessibleString`, not flooded), reduce motion, severity shown by shape not only color.
- **Player control:** Do Not Disturb, muting categories, a history / notification center with unread count (console system UIs, Destiny / Witcher logs).
- **Production plumbing:** split-screen (one per local player), server-to-client posting, survives map travel, handles to update a toast later (download progress), designer preview, debug commands, sounds that don't machine-gun.

Sources: Xbox Accessibility Guidelines XAG 116 (devdocs.xbox.com), Unreal API `FGenericAccessibleMessageHandler::MakeAccessibleAnnouncement` / Announce Accessible String, CommonUI `UCommonActivatableWidgetQueue`, Lyra's indicator / accolade / elimination-feed patterns, game HUD notification-layer guides (studyraid "structuring notification layers"), WoW Loot Toast UI (merging, timer reset on hover).

## Files (`Plugins/NameTBD/Source/NameTBD/Notifications/`)
- `NotificationTypes.h/.cpp`: native tags (`NotificationTags::`), enums, `FNotificationRequest` (what is posted), `FNotificationView` (what widgets see, what the history stores), `FNotificationTypeSettings`, `FNotificationChannelSettings`, `FNotificationUserPreferences`, `FNotificationAction`, `FNotificationIcon` (Image Source rule), `FNotificationKeyGlyph`, `NotificationUtils::FormatText / CountWords / GetDefaultSeverityColor`, `LogNameTBDNotifications`.
- `NotificationSettings.h/.cpp`: `UNotificationSettings` (Project Settings > Plugins > Notifications) with the shipped defaults.
- `NotificationSubsystem.h/.cpp`: `UNotificationSubsystem` (`ULocalPlayerSubsystem`), `FNotificationRecord`, `FNotificationInputProcessor`, console commands.
- `NotificationHost.h/.cpp`: `UNotificationHost` (UMG, palette NameTBD), `SNotificationStack` (panel), `SNotificationItemFrame` (per-item input).
- `SNotificationCard.h/.cpp`: the built-in look - `SNotificationCard`, `SNotificationGlyph`, `FNotificationCardStyle`.
- `NotificationEntryWidget.h/.cpp`: `UNotificationEntryWidget`, `UNotificationActionWidget` (custom looks in UMG).
- `NotificationLibrary.h/.cpp`: `UNotificationLibrary`, `UAsyncAction_PostNotification` (Post Notification And Wait), `UNotificationRelayComponent` (server -> owning client).

## Tags
- Channels: `Notification.Channel.Toast`, `.Banner`, `.Feed`, `.Hint`.
- Types: `Notification.Type.Info`, `.Success`, `.Warning`, `.Error`, `.Reward`, `.Loot`, `.Elimination`, `.Objective`, `.Achievement`, `.Social`, `.System`, `.Hint`.
- Plain gameplay tags (`UE_DEFINE_GAMEPLAY_TAG_COMMENT`): projects add their own anywhere. Type and channel settings are looked up by the tag, then each parent tag, then the defaults - `Notification.Type.Social.Invite` takes Social's settings until it has its own.

## Posting (`FNotificationRequest`)
- **Type** (picks the settings), **Title** / **Body** (FText, formatted: `{Count}`, `{Value}`, and **Text Args** by name), **Icon** (Image Source Texture 2D / SVG + Tint; none: the type's, then the card's severity glyph), **Count** (shown "x3" from 2), **Value** (XP, gold), **Progress** (0..1, negative none), **Stack Key** (what makes two "the same" for stacking and Cooldown: an item ID; None = for Merge / Replace types the type + Title as written, so "+{Value} XP" is one key and a new objective replaces the last; for Always New / Ignore types the type + the whole text shown, title and body formatted, so only an exact repeat is dropped), **Actions** + **Timeout Action Id**, **Payload** (`FInstancedStruct` for custom entries), **Context Object**.
- **Overrides** (inline toggles): Channel, Priority, Severity, Duration (0 = until dismissed).
- Blueprint: `Show Notification` (Type, Title, Body), `Post Notification` (request), `Post Notification And Wait` (async: On Posted / On Action / On Finished), `Update Notification`, `Set Notification Progress`, `Dismiss Notification`, `Respond To Notification`, `Make Notification Action`, `Is Valid (Notification Handle)`, `Push / Pop Notification Suppression`, `Get Notification Subsystem`. The subsystem's own functions and events are callable / bindable directly.
- C++: `UNotificationSubsystem::Get(WorldContext, Player)->PostNotification(Request)` or `PostNotificationWithCallbacks(Request, OnAction, OnFinished)`.
- **Player resolution** (`Get`): the given Player Controller; else the player owning the context (a widget's owning player, a controller, a pawn's controller, a component's owner); else the world's first local player. Null on a dedicated server.

## Type settings (`FNotificationTypeSettings`, per type tag)
- Display Name (settings screens), Channel, Priority (Low / Normal / High / Critical), Severity (Info / Success / Warning / Error / Reward / Social), Icon, **Entry Widget Class** (soft; preloaded by the subsystem at start).
- Timing: **Duration** (0 = until dismissed), **Add Reading Time** (words / Reading Words Per Minute, CJK two characters per word), **Max Queue Time** (expires waiting; never once shown), **Pause While Game Paused**.
- Stacking: **Stack Policy** Always New / **Merge** (Count and Value add, new text / image / progress / actions / payload taken, time refreshed with Refresh On Stack) / **Replace** (content in place, Count starts over) / **Ignore** (dropped while one is there); **Stack Window**; **Cooldown** (drops one with the same key posted within it, even after the first has gone; never for Merge types); **Cancels Types**.
- Behavior: **Ignore Suppression** (through cutscenes, Hold / Drop, Do Not Disturb, muting), **Allow Dismiss**, **Keep In History**, **Announce**, **Player Can Mute**.
- Sounds (`FUISoundSlot`, through `FUISoundPlayer`): Shown (first show only), Stacked, Action, Dismissed; **Min Time Between Sounds** per type. Played with the host widget as context, so a host on a Diegetic Screen plays from the screen.
- Shipped defaults:

| Type | Channel | Priority | Duration | Stacking | Other |
|---|---|---|---|---|---|
| Info | Toast | Normal | 4 s + reading | Ignore | history |
| Success | Toast | Normal | 3.5 s + reading | Ignore | |
| Warning | Toast | High | 5 s + reading | Ignore, Cooldown 1.5 s | not mutable |
| Error | Toast | High | 5 s + reading | Ignore, Cooldown 1.5 s | history, not mutable |
| Reward (XP) | Feed | Normal | 2.5 s | Merge | no announce |
| Loot | Feed | Normal | 3 s | Merge (Stack Key) | no announce |
| Elimination | Feed | Normal | 5 s | Always New | Max Queue Time 4 s |
| Objective | Banner | High | 3 s + reading | Replace | history |
| Achievement | Toast | High | 6 s + reading | Always New | history |
| Social | Toast | Normal | 20 s | Merge | history |
| System | Toast | Critical | until dismissed | Replace | Ignore Suppression, runs while paused, history |
| Hint | Hint | Normal | 4 s + reading | Replace | Max Queue Time 15 s |

## Channel settings (`FNotificationChannelSettings`, per channel tag)
- **Max Visible** (a host's own wins), **Max Queued** (past it the least important, then oldest, waiting one goes - Overflow), **Order By Priority** (off: oldest first; Critical always first), **Allow Preemption** + **Requeue Preempted**, **Make Room** (a full feed lets its oldest go), **Min Display Time** (before it can be pushed off), **Gap** (a beat between one-at-a-time items; skipped for Critical), **Wait For Host** (off: dropped as Expired when nothing shows the channel).
- Defaults: Toast 4 visible / 20 queued, by priority, preemption; Banner 1 / 6, preemption with requeue, Gap 0.25 s; Feed 6 / 30, oldest first, Make Room, Min Display 0.6 s; Hint 1 / 8, oldest first, Gap 0.3 s.

## Subsystem (`UNotificationSubsystem`)
- One per local player (split screen gets one each), kept across map travel. Ticked on the core `FTSTicker` in **real seconds** (delta capped at 0.1 s so a hitch doesn't eat a toast), so a paused or slowed game still drives it.
- **Post:** resolve (type settings by tag hierarchy, channel, priority, severity, stack key) -> Cooldown -> stack into a like one (newest with the same type and key, within Stack Window, not leaving) -> Cancels Types -> player's Do Not Disturb / muted types and Drop suppressions (straight to history, unread, reason Suppressed) -> queue -> Max Queued -> scheduled at once (shows the same frame).
- **States:** Pending (waiting), Visible, Leaving (its host plays it out; gone when the host says so, or after 5 s if the host stopped ticking).
- **Schedule** (per channel with something waiting): the host = the live hosts showing the channel, highest Host Priority (ties: the last registered). Held ones skipped (cinematic mode, Hold / Freeze suppressions). A game pause holds nothing back: what is posted while paused (a pause menu's "Settings saved") shows and its time runs; those posted before the pause stay on screen frozen. Gap. Fill up to capacity (Leaving ones still count, so a one-at-a-time banner waits for the exit). Still full: preempt the lowest-priority one on screen below the waiting one's priority (shown Min Display Time; requeued or dismissed), else Make Room lets the oldest go.
- **Time:** stops while the host is hovered / pressed (Pause On Hover), while the game is paused (types that pause, for notifications posted before the pause), while a Freeze suppression covers it, while one of its keys is being held. Duration resolved at first show: type or override, + reading time, at least Minimum Duration (2 s), at least Minimum Action Duration (10 s) with actions, x Duration Scale; Keep Until Dismissed makes it 0. Timeout: the Timeout Action is taken (reason Timed Out), else it goes.
- **Default hosts** (settings **Create Default Hosts**, on; **Default Host Delay** 0.5 s): when notifications of a channel wait with no live host and no host of the game's has ever registered for that channel, the subsystem makes one (`AddDefaultHost`: a `UNotificationHost` with the channel's preset - Toasts top right, Feed top left, Banner top center, Hints bottom center, any other channel as toasts - Host Priority -1000, put on the player's screen with `UGameViewportClient::AddViewportWidgetForPlayer`, Z order 9000). A Live host of the game's registering for the channel removes it (what it showed moves over with its time); one a map change took off the screen is replaced when needed; all go on Deinitialize. Off, or once the game has its own host for the channel: notifications wait, and a warning is logged once per channel naming why (no host placed, or the game's host isn't on screen).
- **Hosts going away:** a host that isn't live (removed, collapsed anywhere up its parents, not in a window, Slate released) sends its notifications back to waiting with the time they had; a Hold suppression or cinematic mode does the same (no second Shown sound or announcement when they come back).
- **Actions and keys** (`FNotificationInputProcessor`, a Slate input pre-processor, so it works whatever has focus): the **shortcut target** is the most important visible notification with a keyed action (or any dismissable one when a Dismiss key is set), newest first. A press of an action's Gamepad / Keyboard Key takes it (consumed); with Hold Time the key must be held (progress shown on the button, its time frozen meanwhile; releasing cancels; the release is consumed). Dismiss Gamepad / Keyboard Key (settings) closes the target. In split screen keys answer only the player whose Slate user sent them. The last device used (key / stick vs mouse / keyboard) picks which key a hint shows.
- **Suppressions:** `Push Notification Suppression(Mode, Types Or Channels)` returns an id; they add up. Hold (cutscene: nothing new, shown ones leave and return later), Freeze (menu over the HUD: shown ones stay frozen, nothing new), Drop (new ones go straight to history). Settings **Hold During Cinematics** treats the player controller's cinematic mode as a Hold.
- **Preferences** (`FNotificationUserPreferences`, Set / Get Notification Preferences; not saved by the plugin): Do Not Disturb, Muted Types (`Get Notification Types` lists mutable types with names, `Set Notification Type Muted`), Duration Scale (0.25-10, applied to those on screen at once, proportionally), Keep Until Dismissed, Screen Reader Announcements, Reduce Motion (hosts fade only, no slides / pops, sizes snap), Play Sounds. Defaults from the settings' Default Preferences.
- **History:** types with Keep In History, newest first, Max History (100); read when shown on screen, unread when suppressed. Get Notification History, Get Unread Count, Mark Notifications Read, Remove From History, Clear Notification History, **Restore Notification History** (from a save game; each entry gets a fresh handle; actions dropped). On History Changed, On Unread Count Changed.
- **Events:** On Notification Posted / Shown / Updated / Action / Clicked / Dismissed (with the reason, once per notification: Timed Out, User, Responded, Code, Preempted, Cancelled, Expired, Overflow, Suppressed, Throttled, Cleared).
- **Screen reader:** "Title. Body" through `UGameplayStatics::AnnounceAccessibleString`, queued (the platform cuts one off with the next), at most 4 waiting, spaced 1 s + 0.4 s per word.
- **GC:** live requests' objects (images, context objects, payload) are reported from `AddReferencedObjects`; the history is a `UPROPERTY`.
- Queries: Is Notification Active / Visible, Get Notification, Get Active Notifications, Get Queued Count, Get Time Remaining (Fraction).

## Notification Host (`UNotificationHost`, palette NameTBD)
- Place one per screen area; set **Channels** (or a Preset). Self Hit Test Invisible: only the notifications take the mouse.
- **Presets** (`MakePresetValues`; editing a value it set -> Custom; Host Priority, Max Visible, history options, Entry Widget Class, deck values and designer options are free): **Toasts** (default; top-right corner, newest nearest the corner, Slide and Fade from the side, pause on hover, swipe, "+N"), **Banner** (center, Pop in / Fade out, banner card, clicks pass through), **Feed** (left, newest at the end, compact pills, mouse passes through), **Hints** (bottom center, grows up, Rise in, compact without glyph, 14 pt), **History** (notification center).
- **Layout:** Growth Down / Up, Stack Layout **List** / **Deck** (newest in front; up to Deck Depth peek out Deck Peek apart, 5% smaller and 25% fainter each; hovering spreads them into the list), Newest First, Spacing, Item Alignment (Left / Center / Right / Fill), Item Width.
- **Motion:** Enter / Exit Transition (Slide and Fade, Slide, Fade, Pop, Rise), Slide From (Auto: the aligned side, else where it grows from), Slide Distance (0: its own size + a margin, from off the edge), Motion (`EWidgetMotion`) + Enter / Exit Duration. The others glide: each item's share of the stack grows / shrinks with its presence; a size change (progress bar appearing) eases.
- **Interaction:** Interactive (off: Hit Test Invisible - a kill feed never blocks the game), Pause On Hover, Click Behavior (Dismiss / First Action / Event Only - On Notification Clicked / None - body passes clicks through), Swipe To Dismiss (drag sideways past a third of the width, flung off; under it springs back; only for types that allow dismissing), Show Overflow Count ("+N" pill at the end of the list).
- **Appearance:** Entry Widget Class (a type's own wins) else the built-in card with **Card Style**.
- **History source:** a scroll box of the history (newest first, Max History Shown), synced on On History Changed (removed ones animate out, new ones in), marks the history read while on screen (Mark Read When Shown), close / swipe removes from the history, clicks fire On Notification Clicked with the history entry.
- **Liveness:** registered with its player's subsystem when its Slate widget is built (not in the designer); live while its Slate widget's parents up to a window are all visible. Works on a Diegetic Screen (its virtual window counts).
- Designer: **Preview In Designer** + Preview Count show sample notifications for the preset (or what Custom channels suggest), with key hints on the invite and "+2".
- Blueprint: Apply Preset, Set Card Style, Set Channels, Dismiss All, Get Shown Count; events On Notification Shown / Removed.

## Built-in card (`SNotificationCard`, `FNotificationCardStyle`)
- **Layout:** **Toast** (glyph / image, title + count + timestamp, body wrapping, progress, action row, close button, timer bar), **Banner** (a band across fading at both ends, large centered title between two accent lines, body, progress, actions), **Compact** (one-line pill: image, title, body, count, mini progress). `FNotificationCardStyle::Make(Layout)` gives each its starting look.
- Background: color, optional **Background Blur** (Slate Background Blur, glass card), corner radius, outline, soft **shadow** (the plugin's soft-box rings, cached by size / scale), Accent **Stripe** / **Top Line** / **Glow** / None in the severity's color (Use Severity Colors + six colors, or one Accent Color).
- Content: Icon Size, **severity glyphs** drawn as vectors on a tinted disc (i, check, warning triangle, cross, star, speech bubble - shape and color, never color alone), fonts and colors, Count Format ("x{Count}"), timer bar, close button, timestamps in history ("Just now", "5 min ago", "3 h ago", then the date).
- Merge feedback: the count pops (scale 1.35 -> 1 over 0.3 s) and the card flashes (white wash + accent glow over 0.45 s).
- Buttons: Primary (accent, dark text on light accents by luminance), Secondary (outlined), Destructive (error color); **never focusable**; key hint caps (Key Glyphs image or label) on the shortcut target only; hold fill.

## Custom looks (`UNotificationEntryWidget`, `UNotificationActionWidget`)
- Make a Widget Blueprint from Notification Entry Widget; set it as the host's (or a type's) Entry Widget Class. Pooled (`FUserWidgetPool`), reused.
- Optional named parts filled for you (Fill Parts): Title Text, Body Text, Icon Image (texture / material) / Icon SVG (SVG Image), Count Text (Count Format), Progress Bar, Timer Bar, Actions Panel (+ Action Widget Class). Events: On Notification Set / Updated (bStacked) / Cleared, On Shortcut Target Changed. Functions: Dismiss, Respond, Get Time Remaining (Fraction), Get Hold Progress, Is Shortcut Target, Get Action Key, Get Key Glyph, Get Notification, Is History Entry.
- Action widget parts: Action Button, Label Text, Key Text, Hold Bar; On Action Set (action, show key, glyph); Take Action.

## Network (`UNotificationRelayComponent`)
- Add **Notification Relay** to the Player Controller class. Server: `Post Notification To Player` (local controllers directly, remote ones through the relay's reliable Client RPC; a warning names a controller without a relay), `Post Notification To All Players`, relay `Post To Owning Player`, `Clear On Owning Player`. The request travels whole (texts, tags, actions, payload, asset references); handles stay client-side - update a networked toast by posting again with the same Stack Key and a Replace / Merge type.

## Console
Commands work from the game console or the editor's Output Log while playing (a command given the editor's world uses the PIE / game world), and say on screen and in the log what they did; with no local player they warn.
`NameTBD.Notify.Test` (one of each built-in type: toasts, banner, feed merging x3 and +150 XP, a hint, an invite with hold-to-accept and Decline on timeout), `NameTBD.Notify.Post <Type> <Title...>`, `NameTBD.Notify.Clear`, `NameTBD.Notify.Dump` (records with state, time, host and why a waiting one waits - no live host, cinematic mode, a suppression, channel full - or why a shown one's time stands still; hosts with live / channels / count), `NameTBD.Notify.DND [0|1]`. Verbose log: `log LogNameTBDNotifications Verbose`.

## Beyond the request (production expectations added)
- From the research: channels + priorities + preemption + requeue, queue expiry and overflow, merging / replacing / throttling / cancelling, reading-time durations, pause on hover / pause / cinematics, suppressions for cutscenes and menus, nothing lost while the HUD is hidden or rebuilt, actions with focus-free controller shortcuts and hold-to-confirm, timeout actions, async Blueprint node, handles for live updates (progress), history with unread count and save-game restore, Do Not Disturb, muting by category with a list for settings screens, accessibility (duration scale to 10x, keep until dismissed, screen reader, reduce motion, shapes not only colors), split screen, server-to-client relay, sounds per event with a rate limit (and from Diegetic Screens), designer preview, console commands, preloaded entry classes, GC-safe requests.
- Presentation: three built-in looks with presets for the usual HUD areas, list / deck layouts, five transitions on the plugin's motion vocabulary, reflow, swipe to dismiss, "+N" overflow, count pop and flash, severity glyphs, glass background option, custom UMG entries with auto-filled parts.

## Decisions made without asking
- **Local player subsystem**, not a world or game-instance one: split screen needs one per player, and it survives map travel as a HUD's notifications should.
- **Real time** for notification clocks, with types choosing whether a pause stops them (System doesn't).
- **Channels and types are gameplay tags** with hierarchy lookup (no new asset type); type settings live in Project Settings rather than data assets.
- **Keys through a Slate input pre-processor** with `FKey`s, not Enhanced Input or CommonUI actions: it works with any focus and input mode and needs neither plugin; CommonUI action data is left as an option (Still open).
- A Merge type is never throttled by Cooldown (merging is its throttle); Ignore reports the existing handle with reason Throttled to the new post's callbacks.
- A notification preempted in a channel with Requeue Preempted comes back with its remaining time; without it, it goes (Preempted).
- Default hosts are on by default, so a project sees notifications with nothing set up; they never appear for a channel the game has placed a host for (a hidden HUD then holds notifications, as intended).
- A pause freezes the notifications already on screen but never holds new ones back (posted from a pause menu, they belong on screen); the first version held them, which looked like only the "+1" pill showing.
- Muted / Do Not Disturb notifications go to the history unread (as platform notification centers do), never shown later.
- Ids are unique for the session across players; restored history gets fresh ids.

## Still open
- CommonUI integration: Common Action Widget key prompts and input actions for the shortcuts (`NameTBDCommonUI`), and MVVM view models for entries (`NameTBDMVVM`).
- Grouping several merged notifications into one summary ("3 new messages from 2 players") beyond Merge by Stack Key.
- Persisting history automatically (today: Restore for the game's save system). Preferences that have a menu setting (Do Not Disturb, sounds, duration scale, keep until dismissed, read aloud, reduce motion - see MenuSystem.md) are saved and restored by the menu settings; Muted Types are not.
- Server-side handles (update / dismiss a specific client toast from the server) beyond Stack Key re-posts.
- Touch: swipe works through Slate's mouse emulation of touch; no velocity fling or multi-touch.
- A host in an inactive Widget Switcher page counts as live (it isn't collapsed); a per-host "is this page shown" check.
- The AI Assistant can place hosts (`add_widgets` with class NotificationHost) but has no tool to post test notifications.

## Unverified (no engine here: written, not compiled or run)
- `ULocalPlayer::GetSlateUser() const`, `ULocalPlayer::GetPlayerController(const UWorld*)`, `APlayerController::bCinematicMode` being public.
- `FReferenceCollector::AddPropertyReferencesWithStructARO` (5.3+ name) for live requests.
- `UGameplayStatics::AnnounceAccessibleString` reaching the screen reader on each platform.
- `FUserWidgetPool` as a `UPROPERTY` constructed with the owning widget (as `UListViewBase` does), entries created in the designer.
- `SWidget::GetParentWidget` / `Advanced_IsWindow` walk for liveness inside Diegetic Screens, retainer boxes and invalidation panels; active timers on the stack and cards under global invalidation.
- `FSlateRoundedBoxBrush` fills taking the draw tint (white brush tinted per draw) and its outline color; `SBackgroundBlur` inside a card; `FSlateDrawElement::MakeLines` thickness on the glyphs at high DPI.
- `SNew` with extra Construct arguments for `SNotificationCard` / hold fill; `TSlotlessChildren<SNotificationItemFrame>`; `FArrangedWidget` with a render transform and pivot for Pop and Deck.
- Mouse capture on an item frame while its card's buttons are clicked; touch reaching `OnMouseButtonDown` through Slate's fallback.
- Reliable Client RPC with `FNotificationRequest` (an `FInstancedStruct` payload, `FKey`s, an object pointer).
- `FCoreStyle::GetDefaultFontStyle` defaults in a USTRUCT constructor (as Media Box's subtitle font).
- Default hosts: a `UNotificationHost` made with the subsystem as outer (not in a widget tree) taking its widget; `ULocalPlayer::ViewportClient` and `AddViewportWidgetForPlayer` / `RemoveViewportWidgetForPlayer` in 5.7; the viewport clearing them on map change (pruned and remade).

## Check by hand
0. With nothing placed: PIE, `NameTBD.Notify.Test` - default hosts appear (toasts top right, feed top left, banner top center, hint bottom center) and the log says each was added.
1. Add a Notification Host (preset Toasts) to the HUD's top-right in a Canvas slot (anchored top-right, size ~400 x 600), a second with preset Feed top-left, Banner top-center (stretched), Hints bottom-center. The designer shows samples for each.
2. PIE, console `NameTBD.Notify.Test`: toasts slide in, newest nearest the corner, "+N" when more than 4; the banner pops, then the next after it leaves; Iron Ore shows x3 (count pops), XP shows "+150 XP"; the hint rises.
3. Hover a toast: its timer bar stops; leave: it runs. Swipe one sideways: it flies off. Click the close button.
4. The invite: Y / N on keyboard, D-pad right (hold 0.5 s, the button fills) / left on a gamepad; leaving it times out to Decline (`Dump` shows nothing left; On Notification Dismissed reason Timed Out with action Decline).
5. `NameTBD.Notify.DND 1`, run Test: only System types show; a History host lists the rest unread.
6. Pause the game: toasts freeze (System types still count down). Set the controller to cinematic mode: toasts leave, and come back afterwards with their time.
7. Split screen: each player's toasts appear in their own HUD; one player's keys don't answer the other's invite.
8. Listen server + client: `Post Notification To Player` on the client's controller (with a Notification Relay) shows on the client only.
