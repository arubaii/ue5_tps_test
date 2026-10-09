// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Every tooltip, and every line of help or explanation, the plugin's editor panels show - in one place, so the
 * wording can be read over and changed without hunting through the code that lays the panels out. The words
 * counterpart of NameTBDColors.h.
 *
 * Each is a function rather than a constant: an FText made while modules are still starting up is made before
 * the localization system is ready, and would never pick up a translation. Call it where the text is shown.
 *
 * A tip on an icon button is written "Name: what it does" - the name is shown bold, in the animation orange, above
 * the rest. A short one with no ": " is all name. See SWidgetAnimAnchoredTip.
 *
 * The text may hold {0}-style arguments where the panel fills something in - a count, a name. Keep those when
 * rewording; FText::Format is what fills them.
 */
namespace NameTBD::Tooltips
{
	/** The Widget Animations panel: the Animation Tracks list, the Track Editor, the Selection panel and their menus. */
	namespace WidgetAnimEditor
	{
		// -------------------------------------------------------------------------------------------------
		// The Selection panel - one thing selected
		// -------------------------------------------------------------------------------------------------

		inline FText PreviewTargetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PreviewTargetTooltip",
				"The widget this group animates while it is built here. Each use of the animation picks its own Target Widget per group, starting from this one.");
		}

		inline FText LayerGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "LayerGroupTooltip",
				"Which group - which widget - this track moves. Its place in time stays where it is.");
		}

		inline FText DurationFromKeysTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DurationFromKeysTooltip",
				"How long the track runs, set by its keys: it ends on its last key. Move that key on the track to change it.");
		}

		inline FText DurationFromSpringTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DurationFromSpringTooltip",
				"How long the track runs, set by its spring: it ends once the spring settles. Change the spring to change it.");
		}

		inline FText DurationDerivedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DurationDerivedTooltip",
				"How long the track runs, worked out from its {0}. Change that to change it.");
		}

		inline FText TypeNameTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TypeNameTooltip",
				"What kind of track this is. Picked when the track was added, from the Add Track menu.");
		}

		inline FText BlueprintSettingsStructTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BlueprintSettingsStructTooltip",
				"Filled in from the Settings Struct of this track's Blueprint type, and kept matching it. Change the struct on the type's class defaults.");
		}

		inline FText BrowseToSettingsStructTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BrowseToSettingsStructTooltip",
				"Browse to the Settings Struct in the Content Browser.");
		}

		inline FText BrowseToBlueprintTypeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BrowseToBlueprintTypeTooltip",
				"Browse to this track's Blueprint type in the Content Browser.");
		}

		inline FText KeyTimeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "KeyTimeTooltip",
				"Where the key is, in seconds from the track's start. Drag it on the track to move it; a track's first and last keys are its start and end, and stay there.");
		}

		inline FText SlideEndNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SlideEndNote",
				"A Slide ends where the widget sits - a Distance of 0. Its first key's Distance is how far away it starts.");
		}

		inline FText SlideKeyNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SlideKeyNote",
				"How far from where the widget sits, along the slide's direction: the track's Distance at its start, 0 at its end.");
		}

		inline FText WholeTrackKeyCurveNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "WholeTrackKeyCurveNote",
				"This track's Easing spans the whole track (Easing Across: Whole Track), so it passes through this key without easing into or out of it. Set Easing Across to Each Segment to curve each stretch on its own.");
		}

		inline FText SpringKeyCurveNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SpringKeyCurveNote",
				"A Spring track springs across its whole run, keys and all, so it has no curves between keys. Its keys are points along the way it travels.");
		}

		// -------------------------------------------------------------------------------------------------
		// Staggers - notes in a staggered track's tooltip
		// -------------------------------------------------------------------------------------------------

		inline FText StaggerPartsTypeNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "StaggerPartsTypeNote",
				"Letters, words and lines can only be moved by Fade, Scale, Slide, Rotate, Pulse, Shake and Transform tracks. This track plays once, on the whole widget.");
		}

		inline FText StaggerNoTextNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "StaggerNoTextNote",
				"Nothing to stagger on the preview: letters, words and lines need an Animated Text Block, and a plain Text Block has none to move. Replace it with an Animated Text Block.");
		}

		inline FText StaggerNoChildrenNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "StaggerNoChildrenNote",
				"Nothing to stagger on the preview: the widget has no children shown. A list filled at runtime staggers the entries it shows then.");
		}

		inline FText StaggerValueNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "StaggerValueNote",
				"A value-driven track shows its value from the moment it starts, so its items all follow it together - the stagger's spacing isn't used.");
		}

		inline FText NotForThisTrackType()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NotForThisTrackType",
				"Not used by this type of track.");
		}

		inline FText ValueDrivenTrackNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ValueDrivenTrackNote",
				"Value driven: gameplay's value decides where along its keys this track is, with Set Widget Animation Value, so its place and length here don't time it in game. Its keys still shape it. The preview sweeps it once across its length so it can be judged.");
		}

		inline FText ValueDrivenPulseNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ValueDrivenPulseNote",
				"Value driven: gameplay's value is how strongly this Pulse pulses, from none at the start of its range to full at the end. It keeps its own beat, its Duration, however long it runs.");
		}

		inline FText ValueDrivenShakeNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ValueDrivenShakeNote",
				"Value driven: gameplay's value is how strongly this Shake shakes - a trauma meter - from none at the start of its range to full at the end. It shakes on its own clock for as long as it has any strength; its Falloff is left to the value.");
		}

		// -------------------------------------------------------------------------------------------------
		// The Selection panel - several tracks
		// -------------------------------------------------------------------------------------------------

		inline FText MultipleSameType()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MultipleSameType",
				"{0} tracks selected. A setting they don't agree on reads Multiple Values; changing it sets it on all of them.");
		}

		inline FText MultipleMixedTypes()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MultipleMixedTypes",
				"{0} tracks of different types selected. Only the settings they all have are shown; one they don't agree on reads Multiple Values.");
		}

		/** A selection too large to show every track's values side by side. {0} is the count. */
		inline FText MultipleShowsFirstTrack()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MultipleShowsFirstTrack",
				"{0} tracks selected. The values shown are the first track's; a setting changed here is set on all {0}.");
		}

		// -------------------------------------------------------------------------------------------------
		// Panel
		// -------------------------------------------------------------------------------------------------

		inline FText NothingToEdit()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NothingToEdit",
				"Pick the Widget Animation asset to edit above, or switch to Animation Timeline.");
		}

		inline FText NoTimelineYet()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NoTimelineYet",
				"This Widget Blueprint has no animation timelines yet. Make one to start animating its widgets.");
		}

		// -------------------------------------------------------------------------------------------------
		// Panel header
		// -------------------------------------------------------------------------------------------------

		inline FText ModeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ModeTooltip",
				"Animation Timeline edits this Widget Blueprint's own animations, saved with it; Save as Animation Asset makes one usable by other widgets. Edit Existing Asset edits a Widget Animation asset in place, saved with Update Animation Asset.");
		}

		inline FText TimelineModeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TimelineModeTooltip",
				"Make or edit this Widget Blueprint's own animation timelines. They are saved with it and belong to it; to use one in another widget, save it as a Widget Animation asset.");
		}

		inline FText BuilderAnimationNameTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BuilderAnimationNameTooltip",
				"Animation Name: what Save Animation To calls the asset it makes (WA_ is added if missing). Double-click to rename it - the same as the builder's Animation Name in its Class Defaults.");
		}

		inline FText TimelinePickerTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TimelinePickerTooltip",
				"Timeline: which of this Widget Blueprint's animation timelines is open. Double-click its name to rename it; the arrow lists them all, and makes, duplicates or deletes them.");
		}

		inline FText OpenTimelineTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "OpenTimelineTooltip",
				"Open this timeline in the panel. Scripts run on it too.");
		}

		inline FText NewTimelineTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NewTimelineTooltip",
				"Press Enter for a new, empty timeline in this Widget Blueprint - named Timeline 1, Timeline 2 and so on unless a name is typed - opened straight away.");
		}

		inline FText NewTimelineButtonTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NewTimelineButtonTooltip",
				"New Timeline: make this Widget Blueprint's first animation timeline, named Timeline 1. Rename it from the timeline picker above.");
		}

		inline FText NewTimelineHint()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NewTimelineHint", "Name (Enter for Timeline N)");
		}

		inline FText RenameTimelineHint()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "RenameTimelineHint", "New name");
		}

		inline FText RenameTimelineTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "RenameTimelineTooltip",
				"Type a new name for the open timeline and press Enter. A name another timeline has is numbered.");
		}

		inline FText DuplicateTimelineTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DuplicateTimelineTooltip",
				"A copy of the open timeline, in this Widget Blueprint, opened straight away.");
		}

		inline FText DeleteTimelineTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DeleteTimelineTooltip",
				"Delete the open timeline and its tracks from this Widget Blueprint. Undo brings it back.");
		}

		inline FText DeleteTimelineQuestion()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DeleteTimelineQuestion",
				"Delete the animation timeline \"{0}\" and its {1} tracks from this Widget Blueprint?");
		}

		inline FText EditExistingAssetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "EditExistingAssetTooltip",
				"Pick a Widget Animation asset and edit it here, against this widget's own widgets.");
		}

		inline FText UpdateTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "UpdateTooltip",
				"Save the changes made to this Widget Animation asset. Every widget using it plays them.");
		}

		inline FText SaveAsBuilderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SaveAsBuilderTooltip",
				"Save these tracks as a new WA_ Widget Animation asset, named by Animation Name in Save Folder, and go on editing it.");
		}

		inline FText SaveAsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SaveAsTooltip",
				"Save this timeline as a new WA_ Widget Animation asset, so other widgets can use it. The timeline stays in this Widget Blueprint as it is.");
		}

		inline FText SaveToDefaultFolder()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SaveToDefaultFolder",
				"Default Folder");
		}

		inline FText SaveToDefaultFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SaveToDefaultFolderTooltip",
				"Save it in {0}, named after this widget and the timeline. Set the folder for the whole project in Project Settings > Widget Animations > Default Save Folder (a builder uses its own Save Folder).");
		}

		inline FText SaveToCustomLocation()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SaveToCustomLocation",
				"Custom Location...");
		}

		inline FText SaveToCustomLocationTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SaveToCustomLocationTooltip",
				"Pick the folder and name in the editor's Save Asset As window.");
		}

		inline FText DockTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DockTooltip",
				"Open this panel as a tab in the editor layout.");
		}

		inline FText UnlockTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "UnlockTooltip",
				"Unlock the animation, so it can be edited again.");
		}

		inline FText LockTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "LockTooltip",
				"Lock the animation so that it is read only: a preview, with everything that would edit it grayed out.");
		}

		// -------------------------------------------------------------------------------------------------
		// Animation Tracks header
		// -------------------------------------------------------------------------------------------------

		inline FText AddTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AddTooltip",
				"Add a Layer Group - a widget for the animation to move - a track, or an existing Widget Animation's tracks.");
		}

		// -------------------------------------------------------------------------------------------------
		// Animation Properties button
		// -------------------------------------------------------------------------------------------------

		inline FText AnimationPropertiesTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AnimationPropertiesTooltip",
				"Select the animation itself, to see its own settings in the Selection panel.");
		}

		// -------------------------------------------------------------------------------------------------
		// Track Editor header
		// -------------------------------------------------------------------------------------------------

		inline FText FitTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "FitTooltip",
				"Fit: The whole animation into view (F). Double-click anywhere to move the playhead there; right-drag pans along the timeline and down the lines; the wheel zooms, Shift + wheel scrolls the lines.");
		}

		inline FText TimeUnitTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TimeUnitTooltip",
				"Time In: Whether this animation plays over time, in seconds, or follows a progress value from 0% to 100% - a press-and-hold fill, a charge meter. Switching converts every track; one undo step takes it back.");
		}

		inline FText TimeInSecondsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TimeInSecondsTooltip",
				"Plays over time, like any animation. From Percent, 100% becomes one second - nothing moves.");
		}

		inline FText TimeInFramesTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TimeInFramesTooltip",
				"Shows every time in the panel in frames at the frame rate typed beside it - the ruler, the playhead, spans and keys - to author against a frame rate. Preview only: the animation is still timed in seconds, and plays at whatever rate the game runs.");
		}

		inline FText TimeInFramesRateTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TimeInFramesRateTooltip",
				"Target FPS: the frame rate frames are counted at. 60 to start; kept in Editor Preferences > Plugins > Widget Animations Editor. Typing it leaves the menu open.");
		}

		inline FText TimeInPercentTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TimeInPercentTooltip",
				"Follows one progress value, 0 to 1, instead of time: every track, key, stagger and cue is placed between 0% and 100%. The whole animation as it is now is fitted into 0% - 100%. Set its progress with Set Widget Animation Progress, from the Progress Input in Animation Properties, or play it over an entry's Progress Animation Duration.");
		}

		// -------------------------------------------------------------------------------------------------
		// Animation Controls
		// -------------------------------------------------------------------------------------------------

		inline FText SegmentTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SegmentTooltip",
				"{0}: {1}");
		}

		inline FText LoopTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "LoopTooltip",
				"Play the animation looped, as an entry with Loop set would, for as long as it is left playing.");
		}

		inline FText PingPongTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PingPongTooltip",
				"Play back to the start between loops instead of snapping there. Needs Loop.");
		}

		inline FText InvertTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "InvertTooltip",
				"Play the animation from its last frame to its first, as an entry with Invert Animation set would. Flipping it keeps the playhead where it is.");
		}

		inline FText SpeedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SpeedTooltip",
				"Preview Speed: How fast the preview plays, as an entry's own Playback Speed would play it - delays included. Only the preview: the animation's own Animation Speed Multiplier is in Animation Properties.");
		}

		inline FText ToStartTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ToStartTooltip",
				"Go to Start");
		}

		inline FText PrevKeyTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PrevKeyTooltip",
				"Previous Key: The previous key or track edge (,)");
		}

		inline FText PauseTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PauseTooltip",
				"Pause (Space)");
		}

		inline FText PlayTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PlayTooltip",
				"Play: From the top, or pause it (Space)");
		}

		inline FText NextKeyTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NextKeyTooltip",
				"Next Key: The next key or track edge (.)");
		}

		inline FText ToEndTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ToEndTooltip",
				"Go to End. Play then plays the animation backward, from its end to its start.");
		}

		inline FText TrackSnappingTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TrackSnappingTooltip",
				"Snapping: A dragged track or key snaps to the start and end of other tracks, the start and end of the animation, and the playhead. The distance is in Editor Preferences > Widget Animations Editor.");
		}

		inline FText LinkedEdgesTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "LinkedEdgesTooltip",
				"Linked Edges: Resizing a track also resizes the tracks below it whose start or end lines up exactly with the edge being dragged - so a sequence stays joined, and tracks that end together keep ending together.");
		}

		inline FText MotionPathsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MotionPathsTooltip",
				"Motion Path: Every widget this animation moves shows the path it travels on the designer's canvas - even once this panel is closed: a dot at every frame (close together where they move slowly, far apart where they move fast) and a diamond at every key. Click a diamond to pick its key; drag a Transform key's diamond to move it; Alt-drag it to pull out curve handles; right-click it for its Path (Corner, Smooth, Bezier) and Easing Across (each segment or the whole track). More in Editor Preferences > Widget Animations Editor > Motion Paths.");
		}

		inline FText MotionPathsOnlyTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MotionPathsOnlyTooltip",
				"Paths Only: While motion paths are drawn, clicks on the designer only reach them - widgets can't be selected there (no click, double click or marquee), so a widget under a key or handle never takes the click meant for it. Panning and zooming the designer, and selecting widgets in the Hierarchy, still work. Only while Motion Path is on.");
		}

		// -------------------------------------------------------------------------------------------------
		// The Animation Tracks list
		// -------------------------------------------------------------------------------------------------

		inline FText ListGroupRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ListGroupRowTooltip",
				"Click to select the group - its name and target widget - and bring its line into view. Double-click to fold its tracks away here.");
		}

		inline FText ListFoldTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ListFoldTooltip",
				"Show or hide this group's tracks in the list. They still play.");
		}

		inline FText GroupTargetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GroupTargetTooltip",
				"The widget this group animates here. Change it by selecting the group - or pick it by clicking it in the designer, from the group's menu or the eyedropper beside its Target Widget.");
		}

		inline FText ListLayerRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ListLayerRowTooltip",
				"Click to select this track and bring it into view - Shift or Ctrl / Cmd to select several. Right-click for its menu.");
		}

		inline FText ShowHiddenTrackTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShowHiddenTrackTooltip",
				"This track is hidden: the previews play without it. Click to show it again.");
		}

		inline FText UnlockTrackTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "UnlockTrackTooltip",
				"This track is locked: it can be selected, but not moved, resized or edited. Click to unlock it.");
		}

		// -------------------------------------------------------------------------------------------------
		// Add menu
		// -------------------------------------------------------------------------------------------------

		inline FText InsertAnimationTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "InsertAnimationTooltip",
				"Plays another Widget Animation as one track of this one, where a new track would go - at the time shown: where the menu was opened, or after the selected track. It plays exactly as it does on its own, and follows every change made to it. Open it with its arrow to see its groups and tracks; right-click it and Unravel to copy its tracks in, to edit them here.");
		}

		// -------------------------------------------------------------------------------------------------
		// Animation tracks - another Widget Animation inserted as one track
		// -------------------------------------------------------------------------------------------------

		inline FText AnimationTrackNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AnimationTrackNote",
				"Only when it starts is set here. To change what it does, edit the animation itself - every animation it is inserted into follows - or right-click the track and Unravel it to edit its tracks here.");
		}

		inline FText AnimationTrackFitTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AnimationTrackFitTooltip",
				"Stretches the animation across the track: it plays from its start to its end over the track's length, which can then be resized like any track's. Off, it plays at its own length. A percentage animation is always stretched, as is every animation track of a percentage timeline.");
		}

		inline FText AnimationTrackDriveTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AnimationTrackDriveTooltip",
				"What moves the animation along the track. Across the Track: it plays across the track as the timeline passes it. Float Value: it follows a value of its own, 0 at the track's start and 1 at its end, set with Set Widget Animation Value by its Value Name - so a percentage animation can be reused in a timeline that plays in seconds.");
		}

		inline FText AnimationTrackValueNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AnimationTrackValueNote",
				"Follows a float value: Set Widget Animation Value with its Value Name moves it from its start (Value At Start) to its end (Value At End). Its place on the timeline is only where the previews sweep it.");
		}

		inline FText AnimationTrackShortNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AnimationTrackShortNote",
				"Plays another Widget Animation whole. Give that animation a Description, and it shows here and on the track.");
		}

		inline FText AnimationTrackBrowseTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AnimationTrackBrowseTooltip",
				"Find this animation in the Content Browser.");
		}

		inline FText AnimationTrackFoldTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AnimationTrackFoldTooltip",
				"Show or hide the inserted animation's own groups and tracks, under this one. They are shown only: edit them in their own animation, or Unravel the track.");
		}

		inline FText NestedRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NestedRowTooltip",
				"A track of the animation inserted above, shown for reference: it plays as part of that track. Click to select the animation track.");
		}

		inline FText UnravelAnimationTrackTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "UnravelAnimationTrackTooltip",
				"Replaces this track with a copy of every track of the animation it plays, each where it plays now, to edit here. The copies no longer follow the animation.");
		}

		inline FText AddTrackToGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AddTrackToGroupTooltip",
				"A new track in this group, after the selected track - or at the end - on a line of its own.");
		}

		inline FText ReadOnlyAdd()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ReadOnlyAdd",
				"The animation is locked. Unlock it to add to it.");
		}

		inline FText ReadOnlyAsset()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ReadOnlyAsset",
				"Pick the Widget Animation asset to edit, or switch to Animation Timeline.");
		}

		inline FText AddGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AddGroupTooltip",
				"A new group: another widget for this animation to move.");
		}

		// -------------------------------------------------------------------------------------------------
		// Layer Group menu
		// -------------------------------------------------------------------------------------------------

		inline FText JumpToGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "JumpToGroupTooltip",
				"Take the playhead to where this group's first track starts, and scroll the Track Editor to it.");
		}

		inline FText HideGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "HideGroupTooltip",
				"Play the previews without this group's tracks - everything else stays where it is. A game still plays them.");
		}

		inline FText ShowGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShowGroupTooltip",
				"Play this group's tracks in the previews again.");
		}

		inline FText CollapseTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CollapseTooltip",
				"Fold this group's tracks away in the Track Editor, leaving its own line. They still play.");
		}

		inline FText DuplicateGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DuplicateGroupTooltip",
				"A copy of this group and its tracks, playing at the same moments - to point at another widget.");
		}

		inline FText DeleteGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DeleteGroupTooltip",
				"Delete this group and every track in it.");
		}

		// -------------------------------------------------------------------------------------------------
		// Track menu
		// -------------------------------------------------------------------------------------------------

		inline FText HideTrackTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "HideTrackTooltip",
				"Play the previews without this track - every other track stays where it is - to judge the rest on their own. A game still plays it.");
		}

		inline FText ShowTrackTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShowTrackTooltip",
				"Play this track in the previews again.");
		}

		inline FText JumpToTrackTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "JumpToTrackTooltip",
				"Take the playhead to where this track starts, and scroll the Track Editor to it.");
		}

		inline FText DeleteSelectedKeysTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DeleteSelectedKeysTooltip",
				"Delete every selected key that can be. A Transform track keeps at least two keys; any other track keeps its start and end.");
		}

		inline FText CurveBetweenSelectedKeysTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CurveBetweenSelectedKeysTooltip",
				"How the tracks travel across every stretch between two selected keys that sit next to each other.");
		}

		inline FText DeleteKeyTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DeleteKeyTooltip",
				"Delete this key. A Transform track keeps at least two; any other track keeps its start and end.");
		}

		inline FText CurveToNextKeyTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CurveToNextKeyTooltip",
				"How the track travels from this key to the next. Track Easing leaves it to the track's Easing.");
		}

		inline FText AddKeyAtPlayheadTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AddKeyAtPlayheadTooltip",
				"Add a key where the playhead is, starting at the value the track already has there.");
		}

		inline FText AddKeyHereTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AddKeyHereTooltip",
				"Add a key at this moment, starting at the value the track already has there.");
		}

		inline FText TrackCannotHaveKeysTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TrackCannotHaveKeysTooltip",
				"This kind of track has no keys: a Pulse or a Shake moves about where the widget is with nothing to key, and an animation track plays another animation's keys, edited there.");
		}

		inline FText LockSelectedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "LockSelectedTooltip",
				"Lock these tracks: they can still be selected and looked at, but not moved, resized or edited.");
		}

		inline FText DeleteSelectedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DeleteSelectedTooltip",
				"Delete every selected track that isn't locked.");
		}

		inline FText UnlockTrackMenuTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "UnlockTrackMenuTooltip",
				"Let this track be moved, resized and edited again.");
		}

		inline FText LockTrackMenuTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "LockTrackMenuTooltip",
				"Lock this track: it can still be selected and looked at, but not moved, resized or edited.");
		}

		inline FText MoveToGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MoveToGroupTooltip",
				"Make this track move another group's widget. Its place in time stays where it is.");
		}

		inline FText CutTracksTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CutTracksTooltip",
				"Copy these tracks, then delete them. Locked tracks are copied but not deleted.");
		}

		inline FText CopyTracksTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CopyTracksTooltip",
				"Copy these tracks, with their keys and settings, to paste here or into another animation.");
		}

		inline FText PasteTracksTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PasteTracksTooltip",
				"Paste the copied tracks, keeping their spacing in time. From a menu they start where it was opened; from the keyboard, at the playhead.");
		}

		inline FText FocusTracksTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "FocusTracksTooltip",
				"Hide every other track, so only these play in the previews. Un-focus shows them again.");
		}

		inline FText UnfocusTracksTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "UnfocusTracksTooltip",
				"Show the tracks the focus hid. Tracks hidden by hand before it stay hidden.");
		}

		// -------------------------------------------------------------------------------------------------
		// Curves between keys
		// -------------------------------------------------------------------------------------------------

		inline FText SegmentCurveCustomTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SegmentCurveCustomTooltip",
				"Follow a Curve asset from 0 to 1 across the stretch between the keys.");
		}

		// -------------------------------------------------------------------------------------------------
		// The Selection panel - several keys
		// -------------------------------------------------------------------------------------------------

		inline FText KeyCurveNone()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "KeyCurveNone",
				"(no stretch between selected keys)");
		}

		inline FText KeyCurveNoSegments()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "KeyCurveNoSegments",
				"No two selected keys sit next to each other on one track. Select neighboring keys - Shift- or Ctrl-click them, or drag a box around them - to set the curve between them.");
		}

		inline FText KeyCurveSegments()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "KeyCurveSegments",
				"Sets {0} {0}|plural(one=stretch,other=stretches) between neighboring selected keys. Any other stretch keeps its curve.");
		}

		inline FText KeyCurveSpringNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "KeyCurveSpringNote",
				"{0} Spring tracks among them spring across their whole run, and have no curves between keys.");
		}

		inline FText KeyCurvePanelHelp()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "KeyCurvePanelHelp",
				"The curve between two keys is how a track travels from one to the other. Set it here for every stretch between neighboring selected keys at once, or for one key at a time by selecting just that key. Track Easing leaves a stretch to its track's Easing.");
		}

		// -------------------------------------------------------------------------------------------------
		// Track tooltips
		// -------------------------------------------------------------------------------------------------

		inline FText IdentityKeysNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "IdentityKeysNote",
				"Every key is at the identity pose - this track doesn't move anything yet.");
		}

		inline FText HiddenTrackNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "HiddenTrackNote",
				"Hidden - the previews play without it. A game still plays it.");
		}

		inline FText LockedTrackNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "LockedTrackNote",
				"Locked - it can be selected and looked at, but not moved, resized or edited.");
		}

		inline FText PulseBlurbTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PulseBlurbTooltip",
				"Swells by its Scale Amount and settles back once per play - loop it for a steady beat.");
		}

		inline FText ShakeBlurbTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShakeBlurbTooltip",
				"Jitters the widget about where it is - a hit, an error, a rumble - moving it up to its Distance and turning it up to its Angle, strongest where its Falloff says. It lands exactly where it started, and adds to whatever else moves the widget. The same Seed always shakes the same way.");
		}

		// -------------------------------------------------------------------------------------------------
		// Collapsed tracks
		// -------------------------------------------------------------------------------------------------

		inline FText CollapsedTrackNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CollapsedTrackNote",
				"A collapsed track - these tracks, shown as one. Double-click it to open it on its own, with only its tracks in the previews. Each is still a track of its own group, playing exactly as it would anywhere.");
		}

		/** Under the first of a big collapsed track's tracks in its tooltip: how many more there are. {0} is the count. */
		inline FText CollapsedTrackMoreNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CollapsedTrackMoreNote",
				"...and {0} {0}|plural(one=more track,other=more tracks). Open it in the Animation Tracks list, or double-click it, to see every one.");
		}

		inline FText CollapseTracksTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CollapseTracksTooltip",
				"Fold these tracks into one collapsed track, as a Blueprint collapses part of a graph: shown as one track and one row, whichever groups they are in, and opened on their own by double-clicking it. Only how the panel shows them - they play exactly as they did. Copy it to copy all of them.");
		}

		inline FText OpenCollapsedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "OpenCollapsedTooltip",
				"Show only this collapsed track's tracks - their own groups, and the previews playing only them - as a collapsed graph opens in its own tab. All Tracks, at the top, or Escape goes back. Double-clicking it does the same.");
		}

		inline FText ExpandCollapsedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ExpandCollapsedTooltip",
				"Put this collapsed track's tracks back as tracks of their own, each where it is.");
		}

		inline FText RenameCollapsedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "RenameCollapsedTooltip",
				"Its name, shown on its track and its row. Press Enter to rename it; a name another collapsed track has is numbered.");
		}

		inline FText LeaveCollapsedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "LeaveCollapsedTooltip",
				"Back to all the tracks (Escape, with nothing selected).");
		}

		inline FText ListCollapsedRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ListCollapsedRowTooltip",
				"A collapsed track. Click to select every one of its tracks; double-click to open it on its own; right-click for its menu. The arrow lists its tracks under it.");
		}

		inline FText CollapsedListFoldTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CollapsedListFoldTooltip",
				"Show or hide a row for each of its tracks here, to pick one of them on its own.");
		}

		// -------------------------------------------------------------------------------------------------
		// Pinning, showing and hiding, and inverting tracks
		// -------------------------------------------------------------------------------------------------

		inline FText PinTrackTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PinTrackTooltip",
				"Pin to the top of the Track Editor, where it stays in view while the other tracks scroll.");
		}

		inline FText UnpinTrackTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "UnpinTrackTooltip",
				"Unpin - back to its own line in its group.");
		}

		inline FText HideTrackButtonTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "HideTrackButtonTooltip",
				"Hide: the previews play without it, everything else staying where it is. A game still plays it.");
		}

		inline FText InvertTrackTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "InvertTrackTooltip",
				"Play this track from its end back to its start, in its place - in the previews and in game. The preview's Invert inverts every track; one already inverted stays inverted.");
		}

		// -------------------------------------------------------------------------------------------------
		// Material tracks: the material they drive, checked against the preview
		// -------------------------------------------------------------------------------------------------

		inline FText MaterialParameterTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialParameterTooltip",
				"The parameter this track drives, by name. The arrow lists the parameters of the widget's material, each with its default value; a name can also be typed.");
		}

		inline FText MaterialParameterPickTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialParameterPickTooltip",
				"Pick from the parameters of {0}, the material of {1} - its default value beside each.");
		}

		inline FText MaterialParameterNoMaterialTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialParameterNoMaterialTooltip",
				"Nothing to list: the track's widget has no material in this slot on the preview.");
		}

		inline FText MaterialNoPreviewNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialNoPreviewNote",
				"Open this Widget Blueprint in the designer to check the track against the widget it plays on.");
		}

		inline FText MaterialNoSlotNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialNoSlotNote",
				"{0} ({1}) has no {2} material: Brush is an Image's or a Border's, Font and Font Outline a Text Block's, Retainer Effect a Retainer Box's. Aim the track at one of those - its group's target, or its Target Widget - or pick the material the widget has.");
		}

		inline FText MaterialNotSetNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialNotSetNote",
				"{0} has no material in its {1} yet, so this track has nothing to drive. Set one on the widget in the designer.");
		}

		inline FText MaterialNoParameterNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialNoParameterNote",
				"Pick the parameter to drive - the arrow beside Parameter lists those of {0}.");
		}

		inline FText MaterialMissingParameterNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialMissingParameterNote",
				"{0} has no {1} parameter named {2}, so this track does nothing. Pick one from the arrow beside Parameter.");
		}

		inline FText MaterialNoParametersNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialNoParametersNote",
				"{0} has no {1} parameters to drive.");
		}

		// -------------------------------------------------------------------------------------------------
		// Track names and track presets
		// -------------------------------------------------------------------------------------------------

		// -------------------------------------------------------------------------------------------------
		// Animation Parameters
		// -------------------------------------------------------------------------------------------------

		inline FText ParametersSectionTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ParametersSectionTooltip",
				"Parameters: named values this animation's tracks can take instead of their own - a slide's distance, a fade's end, a key's value, a color, a duration. Bind a field with the pill at the end of its row. Every use of the animation can then set them: a Widget Animation Component entry's Parameters, Play Widget Animation With Parameters, Set Widget Animation Parameter, a state machine state from its inputs. One animation, many results.\n\nEach row: its name, its type, and its default - what the preview plays. Right-click a row to rename, retype or delete it; expand it for its description, options and range.");
		}

		inline FText TrackNameTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TrackNameTooltip",
				"The track's name in the Track Editor - the same as Rename on its right-click menu. Empty, or its type's name, and it is called by its type again.");
		}

		inline FText ParameterPreviewTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ParameterPreviewTooltip",
				"Preview: ticked, the preview (and the designer's preview and motion paths) plays the value beside it instead of the parameter's default - try a few without changing the animation. Never saved. Unticked, the default plays again.");
		}

		inline FText MissingParameterTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MissingParameterTooltip",
				"Bound to a parameter this animation doesn't have (renamed or deleted): the field plays its own value. Bind it to another, or let go.");
		}

		inline FText AddParameterTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AddParameterTooltip",
				"Add Parameter: a new parameter of the type picked. Or make one straight from a field: its pill's menu, New Parameter From This Field.");
		}

		inline FText NoParametersNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NoParametersNote",
				"No parameters yet. Add one with +, or from any track field's pill (at the end of its row): New Parameter From This Field.");
		}

		inline FText BagBindTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BagBindTooltip",
				"Bind one of these to a parameter: click for each of them, and the animation's parameters of a type it takes - or make one from it. A bound one takes the parameter's value each time the track plays, instead of the value shown below.");
		}

		inline FText BagFieldsBoundTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BagFieldsBoundTooltip",
				"{0}|plural(one=One of these is,other={0} of these are) bound to the animation's parameters: each takes its parameter's value as the track plays, instead of the value shown below. Click to see which, bind others or let go.");
		}

		inline FText BagBindSection()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BagBindSection", "Bind To Parameter");
		}

		inline FText BindFieldTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BindFieldTooltip",
				"Bind to a parameter: this field takes the value of one of the animation's parameters each time it plays, instead of its own. Click for the parameters of a type it takes, or to make one from it.");
		}

		inline FText BoundFieldTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BoundFieldTooltip",
				"Bound to {0} ({1}): this field plays the parameter's value - {2} by default, whatever each use of the animation sets - not the value shown here, which plays only if the parameter is removed. Click to bind it to another, mirror it, or let go.");
		}

		inline FText PlaysWhenTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PlaysWhenTooltip",
				"Plays When: this track plays only for some values of a Choice or Bool parameter - Slide From Left for Side = Left, Slide From Right for Side = Right, in one animation. When it doesn't hold, the track keeps its place in time and poses nothing. Always: it always plays.");
		}

		inline FText RenameTrackTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "RenameTrackTooltip",
				"A name of its own for this track, shown in the list, on the track and in its tooltip - Enter to apply. No two renamed tracks share a name: a name another has gets a number. Cleared, the track is called by its type again.");
		}

		inline FText TrackPresetsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TrackPresetsTooltip",
				"Put a saved single-track preset's settings on this track - its type, values, timing, easing and keys - or save this track as a preset, under its name. Where the track is and what it is called stay its own.");
		}

		inline FText SaveTrackPresetsMenuTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SaveTrackPresetsMenuTooltip",
				"Save this track - or this collapsed track, with every track in it - as a preset, under the name it was given. Add it again from Add Animation Track > From Preset.");
		}

		inline FText PresetRenameTrackNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PresetRenameTrackNote",
				"Rename the track first: a preset is saved under the track's own name, so a track still called by its type can't be one.");
		}

		inline FText PresetRenameCollapsedNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PresetRenameCollapsedNote",
				"Rename the collapsed track first: a preset of several tracks is saved under the collapsed track's name, so one still called \"Collapsed Tracks\" can't be one.");
		}

		inline FText PresetSeveralTracksNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PresetSeveralTracksNote",
				"Several tracks are saved as one preset from a collapsed track: collapse them, rename the collapsed track, and save that.");
		}

		inline FText PresetAnimationTrackNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PresetAnimationTrackNote",
				"A track that plays another animation can't be in a preset: its settings are that animation's.");
		}

		inline FText AddCollapsedFromNamedPresetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AddCollapsedFromNamedPresetTooltip",
				"Add the tracks of the preset {0}, each at its time relative to the first, as a collapsed track of that name.");
		}

		inline FText ApplyTrackPresetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ApplyTrackPresetTooltip",
				"Put the preset {0} on the track: its type, values, timing, easing and keys. Its Starts, Delay, group, name and lock stay as they are. Ctrl+Z takes it back.");
		}

		inline FText AddTrackFromPresetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AddTrackFromPresetTooltip",
				"Add tracks made from one of your saved presets - a single track named after it, or a collapsed track of several - starting where a new track would.");
		}

		inline FText AddTrackFromNamedPresetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AddTrackFromNamedPresetTooltip",
				"Add a track made from the preset {0}, named after it.");
		}

		inline FText NoTrackPresetsToAddNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NoTrackPresetsToAddNote",
				"No track presets yet. Rename a track - or a collapsed track - and save it from its menu's Track Presets.");
		}

		inline FText NoTrackPresetsNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NoTrackPresetsNote",
				"No single-track presets yet. Rename a track and save it as one below.");
		}

		inline FText SaveTrackPresetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SaveTrackPresetTooltip",
				"Save as a preset under this name, to add again from Add Animation Track > From Preset. Presets are files in the project's Content/WidgetAnimTrackPresets folder, so they can be shared through source control.");
		}

		inline FText DeleteTrackPresetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DeleteTrackPresetTooltip",
				"Delete a saved preset. Tracks made from it keep their settings.");
		}

		inline FText OpenTrackPresetsFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "OpenTrackPresetsFolderTooltip",
				"Open the folder the project's track presets are saved in.");
		}

		// -------------------------------------------------------------------------------------------------
		// The Script area docked as a tab
		// -------------------------------------------------------------------------------------------------

		inline FText DockScriptTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DockScriptTooltip",
				"Dock in Layout: open the script editor as a tab of the editor's layout - beside the Details panel to start with - with the output under the code. Drag it anywhere, like any tab.");
		}

		inline FText ScriptTabTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptTabTooltip",
				"Widget Animation Scripting, docked in the layout. It runs on the animation open in this editor's Widget Animations panel.");
		}

		// -------------------------------------------------------------------------------------------------
		// The Widget Animation Scripting drawer
		// -------------------------------------------------------------------------------------------------

		inline FText ScriptingDrawerTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptingDrawerTooltip",
				"Widget Animation Scripting: Python that makes and changes tracks - patterns over many widgets in one go - run on the animation open in the Widget Animations panel.");
		}

		inline FText ScriptingDockInLayoutTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptingDockInLayoutTooltip",
				"Dock in Layout: keep the script editor open as a tab beside the Details panel, with the output under the code. Drag it anywhere, like any tab.");
		}

		inline FText ScriptingDrawerCloseTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptingDrawerCloseTooltip",
				"Close: close the drawer. The script is kept - Widget Animation Scripting on the status bar opens it again.");
		}

		inline FText ScriptTabCloseTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptTabCloseTooltip",
				"Close: close this tab. The script is kept - the Script button in the Widget Animations panel opens it again.");
		}

		// -------------------------------------------------------------------------------------------------
		// Folders of layer groups in the Animation Tracks list
		// -------------------------------------------------------------------------------------------------

		inline FText MoveToFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MoveToFolderTooltip",
				"Group these layer groups under a List Folder - a folder in this panel's Animation Tracks list only, to keep a long list tidy. It isn't a folder in the Content Browser or on disk, and nothing about the animation changes.");
		}

		inline FText NewFolderHint()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NewFolderHint",
				"New List Folder name - Enter to make it");
		}

		inline FText RemoveFromFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "RemoveFromFolderTooltip",
				"List the group on its own again, out of its List Folder. The group and its tracks stay as they are.");
		}

		inline FText ListFolderRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ListFolderRowTooltip",
				"A List Folder: layer groups gathered under one row of this list, to keep it tidy - it isn't a folder in the Content Browser or on disk, and changes nothing about the animation. Click to select its groups, double-click or the arrow to fold it, right-click for its menu.");
		}

		inline FText ListFolderFoldTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ListFolderFoldTooltip",
				"Fold the List Folder shut, or open it again, in this list.");
		}

		inline FText SelectFolderGroupsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SelectFolderGroupsTooltip",
				"Select every group in the List Folder.");
		}

		inline FText RenameFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "RenameFolderTooltip",
				"Rename the List Folder - Enter to apply. A name another List Folder already has joins the two.");
		}

		inline FText AddGroupToFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AddGroupToFolderTooltip",
				"Add a new layer group, listed in this List Folder.");
		}

		inline FText RemoveFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "RemoveFolderTooltip",
				"Take every group out of this List Folder and list them on their own. The groups and their tracks stay as they are.");
		}

		inline FText DeleteFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DeleteFolderTooltip",
				"Delete every group in this List Folder, with all of their tracks.");
		}

		inline FText FoldFolderGroupsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "FoldFolderGroupsTooltip",
				"Fold every group of the List Folder in the Track Editor, or open them all again.");
		}

		inline FText DeleteGroupsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DeleteGroupsTooltip",
				"Delete the selected groups, with all of their tracks.");
		}

		inline FText MultipleGroupsSelectedNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MultipleGroupsSelectedNote",
				"{0} layer groups selected - the settings below are {1}'s. Right-click any of them to put them all in a List Folder, hide, fold or delete them; Delete deletes them.");
		}

		// -------------------------------------------------------------------------------------------------
		// Picking a group's Target Widget in the designer
		// -------------------------------------------------------------------------------------------------

		inline FText PickTargetInDesignerTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PickTargetInDesignerTooltip",
				"Pick the widget this group animates by clicking it in the designer with the eyedropper, which names the widget under it. Escape calls it off.");
		}

		inline FText PickingTargetLabel()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PickingTargetLabel",
				"Click a widget in the designer...");
		}

		inline FText PickingTargetNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PickingTargetNote",
				"Point at the widget in the designer that this group should animate - its name shows by the eyedropper - and click it. Escape, or the button again, calls it off.");
		}

		inline FText PickTargetNotFoundNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "PickTargetNotFoundNote",
				"That widget can't be a Target Widget here: it isn't in this widget's tree.");
		}

		inline FText MaterialScalarBlurbTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialScalarBlurbTooltip",
				"Drives a scalar parameter of the material on its target Image's brush.");
		}

		inline FText MaterialVectorBlurbTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "MaterialVectorBlurbTooltip",
				"Drives a vector (color) parameter of the material on its target Image's brush.");
		}

		inline FText TransformBlurbTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TransformBlurbTooltip",
				"Poses the widget's Render Transform at each key, and moves it between them.");
		}

		inline FText ShapeBoxBlurbTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShapeBoxBlurbTooltip",
				"Shapes a Shape Box's outline at each key - every Shape Box setting - and morphs it between them. A key set to Use Authored Shape is the box as designed.");
		}

		inline FText ShapeBlurbTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShapeBlurbTooltip",
				"Sets a shape's settings at each key - its fill, stroke and trim, a Star's points, an Ellipse's sweep, a Custom Shape's path - and blends them between keys. A key set to Use Authored Shape is the shape as designed.");
		}

		inline FText DesignBlurbTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DesignBlurbTooltip",
				"Keys the widget's look - an Image's brush and tint, a Text Block's color, font and shadow, a Border's background, a Button's colors - and blends from one designed look to the next. Its first key uses the widget as designed; set the keys after it to where the look goes.");
		}

		inline FText DesignNoWidgetNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "DesignNoWidgetNote",
				"No widget to read a design off yet: give the track's Layer Group a Target Widget, or open the animation in a Widget Blueprint's editor, and its keys take that widget's settings.");
		}

		// A registered type describes itself (FWidgetAnimEvaluator::GetDescription); these two are only for a
		// Registered Type track that has no registered type to describe it.

		inline FText UnregisteredTypeBlurbTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "UnregisteredTypeBlurbTooltip",
				"This track's type isn't registered - the module that adds it isn't loaded. Its settings are kept, and it plays as nothing until that module is loaded again.");
		}

		inline FText BlueprintTypeOverBudgetWarning()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BlueprintTypeOverBudgetWarning",
				"This Blueprint type is over budget: {0} ms of Blueprint per frame, on {1}% of frames. Give it a Quantize Step, or move it to a C++ evaluator.");
		}

		inline FText BlueprintTypeNoQuantizeWarning()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "BlueprintTypeNoQuantizeWarning",
				"This Blueprint type has no Quantize Step, so it runs Blueprint on every frame it plays. Set one in its class defaults - 1 for whole numbers - to run it only when its value visibly changes.");
		}

		inline FText NoRegisteredTypeBlurbTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NoRegisteredTypeBlurbTooltip",
				"This track has no type, so it does nothing. Add a track of the type you want from the Add Track menu, and delete this one.");
		}

		inline FText TransformShapeBoxWarningTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TransformShapeBoxWarningTooltip",
				"A Shape Box track needs a Shape Box: the target widget itself, or the Shape Box it sits directly inside.");
		}

		inline FText ShapeTrackTargetWarningTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShapeTrackTargetWarningTooltip",
				"A Shape track needs a shape - an Ellipse, Triangle, Star, N-gon or Custom Shape: the target widget itself, or the first one inside it.");
		}

		inline FText ShapeTrackKindTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShapeTrackKindTooltip",
				"Which kind of shape this track's keys are for - the settings each key holds. The track plays on the widget the animation is put on, so its kind isn't known until then: pick the kind of shape it will be. Played on another kind, the settings the two share - fill, stroke, trim, layout - still animate.");
		}

		inline FText ShapeTrackKindNone()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShapeTrackKindNone", "Pick a kind of shape...");
		}

		inline FText ShapeTrackMatchedNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShapeTrackMatchedNote",
				"Keys {0} settings - the kind of shape this track's group targets.");
		}

		inline FText ShapeTrackNoFieldsNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShapeTrackNoFieldsNote",
				"No shape fields apply: this track's target, {0}, isn't a shape. Set the group's target to (this widget) or to a shape - an Ellipse, Triangle, Star, N-gon or Custom Shape.");
		}

		inline FText ShapeTrackPickKindNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ShapeTrackPickKindNote",
				"Pick the kind of shape above to set this key's shape fields.");
		}

		inline FText SetShapeTrackKindTransaction()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SetShapeTrackKindTransaction", "Set Shape Track Kind");
		}

		// -------------------------------------------------------------------------------------------------
		// Key tooltips
		// -------------------------------------------------------------------------------------------------

		inline FText SpringKeyPointNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "SpringKeyPointNote",
				"A Spring track springs across its whole run: this key is a point along the way it travels, reached when the spring gets there rather than at this moment.");
		}

		inline FText IdentityPoseNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "IdentityPoseNote",
				"Identity pose - nothing moved at this key.");
		}

		// -------------------------------------------------------------------------------------------------
		// Value Watcher
		// -------------------------------------------------------------------------------------------------

		inline FText WatchWidgetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "WatchWidgetTooltip",
				"The widget to watch - on the designer's preview, as the animation poses it.");
		}

		inline FText WatchValueTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "WatchValueTooltip",
				"What of the widget to read. Translation, Scale and Shear are read whole, X and Y together.");
		}

		inline FText WatchRemoveTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "WatchRemoveTooltip",
				"Remove: Stop watching this value.");
		}

		inline FText WatchAddTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "WatchAddTooltip",
				"Watch another value - of this widget or any other - alongside the ones already here.");
		}

		inline FText WatchFoldTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "WatchFoldTooltip",
				"Open or close the Value Watcher.");
		}
		// -------------------------------------------------------------------------------------------------
		// The compact panel - a plain list of tracks, such as a component's Additive Anim Layers
		// -------------------------------------------------------------------------------------------------

		inline FText CompactNoTracks()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CompactNoTracks",
				"No tracks yet. Add one with Add Track, or right-click here.");
		}

		inline FText CompactNothingSelected()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CompactNothingSelected",
				"Select a track, or a key on one, to edit it here. Play or scrub to see the tracks on the widget in the designer.");
		}

		inline FText CompactAddTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CompactAddTooltip",
				"Add Track: A track of any type, after the selected one - or build on an existing Widget Animation.");
		}

		inline FText CompactSaveTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CompactSaveTooltip",
				"Save as Animation: Keep these tracks as a Widget Animation asset of their own, to use on other widgets too.");
		}

		inline FText CompactSaveAsAssetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CompactSaveAsAssetTooltip",
				"Saves a copy of these tracks as a new WA_ asset beside this Widget Blueprint. The tracks here stay as they are.");
		}

		inline FText CompactReplaceWithEntryTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CompactReplaceWithEntryTooltip",
				"Saves these tracks as a new WA_ asset, then plays that asset from a new entry instead: the entry is added and these tracks are cleared. Undoable, apart from the asset itself.");
		}

		inline FText CompactExpandTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CompactExpandTooltip",
				"Expand: The same tracks in a window of their own, which can be made as big as they need.");
		}

		inline FText CompactSavedNotice()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CompactSavedNotice",
				"Saved the tracks as {0}.");
		}

		inline FText CompactReplacedNotice()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CompactReplacedNotice",
				"Saved the tracks as {0}, and replaced them with an entry playing it.");
		}

		inline FText AdditiveStartTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AdditiveStartTooltip",
				"Starts: When these tracks start - right away, with the rest of Anim In or Anim Out, or within one of its animations, as part of it.");
		}

		inline FText AdditiveStartRightAwayTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AdditiveStartRightAwayTooltip",
				"The tracks start as soon as Anim In (or Anim Out) plays, on a clock of their own.");
		}

		inline FText AdditiveStartWithinTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AdditiveStartWithinTooltip",
				"The tracks play as part of {0}: from its start, whenever it plays - Play In, a trigger, Play By Id - with its Loop, Ping Pong, Invert and Playback Speed. Its tracks show above these, grayed, to place them against.");
		}

		inline FText AdditiveStartNoAnimations()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AdditiveStartNoAnimations",
				"No animations to start within yet. Add one to Anim In or Anim Out - or mark one Mirror In Out.");
		}

		inline FText CompactFoldTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "CompactFoldTooltip",
				"Show or hide the track editor. Hidden, it leaves the widget in the designer as authored.");
		}

		inline FText LayerTargetWidgetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "LayerTargetWidgetTooltip",
				"The widget this track moves, as a path into this Widget Blueprint - the same kind an entry's Target Widgets use. None moves the widget the component is on.");
		}

		inline FText AdditiveLayersInEditorTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AdditiveLayersInEditorTooltip",
				"Tracks played on this widget on top of Anim In, every time it plays. For a touch that belongs to this one widget and isn't worth a Widget Animation asset of its own.");
		}

		inline FText AdditiveLayersOutEditorTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AdditiveLayersOutEditorTooltip",
				"Tracks played on this widget on top of Anim Out, every time it plays. Like every exit they play in reverse - authored as the widget arriving, played as it leaving - and the preview here plays them that way.");
		}

		inline FText AdditiveLayersRawFieldsNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AdditiveLayersRawFieldsNote",
				"The same tracks as plain fields, under Advanced below.");
		}

		inline FText AdditiveLayersResizeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AdditiveLayersResizeTooltip",
				"Drag to make the track editor taller or shorter.");
		}

		inline FText TrackEventRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TrackEventRowTooltip",
				"An Event track in this component's animations (Add Track > Cues > Event), or in an animation inserted into them as a track, named by the animation it is in. Its event fires in this Widget Blueprint's graph each time the track is passed, with an Info pin: the widget it is aimed at, which point fired and its value, the Anim ID that played it.");
		}

		inline FText TrackEventAddTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TrackEventAddTooltip",
				"Adds this event to the Event Graph and goes to it.");
		}

		inline FText TrackEventViewTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TrackEventViewTooltip",
				"This event is in the graph already: goes to it.");
		}

		inline FText StateEnteredEventTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "StateEnteredEventTooltip",
				"Fires in this Widget Blueprint's graph each time the {0} state machine's {1} layer enters its {2} state - with an Info pin: the machine, layer and state, the state it came from, and the widget playing it. Fired a frame later, so anything may be done in it.");
		}

		inline FText StateExitedEventTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "StateExitedEventTooltip",
				"Fires in this Widget Blueprint's graph each time the {0} state machine's {1} layer leaves its {2} state - with an Info pin: the machine, layer and state, the state it goes to, and the widget playing it. Fired a frame later, so anything may be done in it.");
		}

		inline FText ActionEventRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ActionEventRowTooltip",
				"Fires in this Widget Blueprint's graph each time the {0} state machine's Fire Event action {1} runs - on {2} - with an Info pin: the machine, the layer, Event Name, and the widget playing it. Run once the frame's transitions are settled.");
		}

		inline FText AdditiveLayersMultipleObjects()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AdditiveLayersMultipleObjects",
				"Several components are selected: edit their tracks one at a time, or in the fields under Advanced.");
		}

		inline FText AdditiveLayersWindowTitle()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "AdditiveLayersWindowTitle",
				"{0} - {1}");
		}

		// -------------------------------------------------------------------------------------------------
		// The Script area - Python that makes tracks
		// -------------------------------------------------------------------------------------------------

		inline FText ScriptTargetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptTargetTooltip",
				"Runs on: the animation this script edits - the timeline or asset open in this Widget Blueprint's Widget Animations panel.");
		}

		inline FText ScriptRunTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptRunTooltip",
				"Run (Ctrl+Enter): runs the script on this animation. One run is one undoable edit, and a run that stops with an error changes nothing.");
		}

		inline FText ScriptSnippetsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptSnippetsTooltip",
				"Snippets: ready-made scripts to start from, and your own - save the script as one, or delete one. Your functions are here too.");
		}

		inline FText ScriptSnippetEntryTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptSnippetEntryTooltip",
				"Puts this script in the code box in place of what is there. Restore Previous Script brings that back.");
		}

		inline FText ScriptNoSnippets()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptNoSnippets",
				"No .py files in the plugin's or this project's Content/Python/WidgetAnimSnippets.");
		}

		inline FText ScriptRestorePreviousTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptRestorePreviousTooltip",
				"Puts back the script the last snippet replaced.");
		}

		inline FText ScriptApiReferenceTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptApiReferenceTooltip",
				"API Reference: lists everything a script can use - anim, its tracks and groups, the widgets - in the output.");
		}

		inline FText ScriptClearOutputTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptClearOutputTooltip",
				"Clear Output: empties the output beside the code.");
		}

		inline FText ScriptCloseTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptCloseTooltip",
				"Close: hides the Script area. The script is kept.");
		}

		inline FText ScriptTabScriptTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptTabScriptTooltip",
				"Script: the script Run runs.");
		}

		inline FText ScriptTabFunctionsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptTabFunctionsTooltip",
				"Functions: your own functions, in the project's Content/Python/WidgetAnimFunctions/my_functions.py. Every run loads them first, so a script can use them by name - and they can use anim, TrackType and the rest. Run still runs the script.");
		}

		inline FText ScriptSaveSnippetHint()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptSaveSnippetHint",
				"Name, then Enter");
		}

		inline FText ScriptSaveSnippetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptSaveSnippetTooltip",
				"Saves the script as a snippet of this project's - a .py file in Content/Python/WidgetAnimSnippets, so it can go into source control with the rest of the project.");
		}

		inline FText ScriptOpenSnippetsFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptOpenSnippetsFolderTooltip",
				"Opens this project's snippets folder. Any .py file put there shows up under This Project's Snippets.");
		}

		inline FText ScriptDeleteSnippetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptDeleteSnippetTooltip",
				"Deletes one of this project's snippets, after asking.");
		}

		inline FText ScriptOpenFunctionsFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptOpenFunctionsFolderTooltip",
				"Opens the folder your functions are in. Every .py file there is loaded before each script - my_functions.py first, then the rest by name.");
		}

		inline FText ScriptPopOutTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptPopOutTooltip",
				"Open in Window: moves the Script area into a window of its own, which can go anywhere - another monitor. The Script button brings it back to front.");
		}

		inline FText ScriptWindowCloseTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptWindowCloseTooltip",
				"Close: closes this window. The script is kept; the Script button opens it under the Track Editor again.");
		}

		inline FText ScriptCodeHint()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptCodeHint",
				"Python that makes tracks - anim is this animation. Snippets has examples to start from.");
		}

		inline FText ScriptOutputHint()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptOutputHint",
				"Output Log");
		}

		inline FText ScriptPythonUnavailable()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptPythonUnavailable",
				"Scripts need Python. Turn on the Python Editor Script Plugin (Edit > Plugins) and restart the editor - nothing else in the panel needs it.");
		}

		inline FText ScriptEditorFailed()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptEditorFailed",
				"The code editor couldn't start, so the plain code box is in its place. The Output Log says why. Use Code Editor is in Editor Preferences > Plugins > Widget Animations Editor > Script Editor.");
		}

		/** What the code box holds the first time it is opened - a script, so written as one. */
		inline FText ScriptStarter()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ScriptStarter",
				"# Python that makes and changes this animation's tracks. Run (Ctrl+Enter) - one Ctrl+Z takes a run back.\n# anim is the animation, selection what is selected in the panel.\n# Snippets has scripts to start from; API Reference lists everything.\n\nanim.print_tracks()\n");
		}

		// -------------------------------------------------------------------------------------------------
		// The Graph Editor - the selected tracks as curves, in place of the Track Editor (see SWidgetAnimGraphEditor)
		// -------------------------------------------------------------------------------------------------

		inline FText GraphEditorButtonTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphEditorButtonTooltip",
				"Graph Editor: the selected tracks as curves - each value over time - in place of the Track Editor. Drag keys in time and value, add keys from the right-click menu, and drag the handles between two keys to shape how they ease. While it is open, Play plays only these tracks.");
		}

		inline FText OpenGraphEditor()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "OpenGraphEditor",
				"Open in Graph Editor");
		}

		inline FText OpenGraphEditorTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "OpenGraphEditorTooltip",
				"Shows these tracks as curves in place of the Track Editor - their keys moved in time and value, and the motion between two keys shaped by its handles. Play plays only them while it is open.");
		}

		inline FText LeaveGraphTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "LeaveGraphTooltip",
				"Back to the Track Editor (Escape, with no key selected in the graph).");
		}

		inline FText GraphAllTracksTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphAllTracksTooltip",
				"Back to all the tracks, in the Track Editor.");
		}

		inline FText GraphNoTracks()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphNoTracks",
				"Select a track with keys - any track that moves from a start value to an end value, or a keyframed one - to see its curves.");
		}

		inline FText GraphNormalizedLabel()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphNormalizedLabel",
				"Normalized: each curve fills the height from its lowest key to its highest");
		}

		inline FText GraphValuesViewTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphValuesViewTooltip",
				"Values: a curve for each value the keys hold - a Transform's Translation, Scale, Shear and Angle each on its own, a color's R, G, B and A. Drag a key up or down to change that one value; sideways to move the whole key, which every value shares.");
		}

		inline FText GraphEaseViewTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphEaseViewTooltip",
				"Ease: how each stretch between two keys gets there, from 0 at one key to 1 at the next - the one curve every value of that stretch follows. Drag its handles to shape it, past either end for an overshoot or a wind-up.");
		}

		inline FText GraphNormalizeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphNormalizeTooltip",
				"Normalize: with several channels shown, each is drawn from its lowest key to its highest, so a translation of hundreds and a scale of 1 can be read together. One channel shown is always drawn in its own values.");
		}

		inline FText GraphCurveTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphCurveTooltip",
				"Curve To Next Key: how each selected key moves on to the next. Bezier keeps the shape it has now and gives it handles to drag. On a track with no keys, the presets set its Easing.");
		}

		inline FText GraphCurveNone()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphCurveNone",
				"Select a key");
		}

		inline FText GraphFitTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphFitTooltip",
				"Fit: every shown curve in view (F).");
		}

		inline FText GraphShowAllTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphShowAllTooltip",
				"Show every channel of every track again.");
		}

		inline FText GraphDeleteKeysTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphDeleteKeysTooltip",
				"Deletes the selected keys - never a track's start or end, nor the last two keys of a Transform track (Delete).");
		}

		inline FText GraphAddKeyTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphAddKeyTooltip",
				"Adds a key here, at the value the track already has - so nothing changes until it is moved.");
		}

		inline FText GraphSpringNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphSpringNote",
				"A Spring track's motion is its spring: its keys are points along it, and there are no handles to shape. Set its Timing to Tween to shape it here.");
		}

		inline FText GraphValuesHint()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphValuesHint",
				"Right-click to add a key. Drag a key, or the handles of a selected one. Shift keeps a drag to one direction. Right-drag pans; the wheel zooms (Ctrl in time, Shift in value).");
		}

		inline FText GraphEaseHint()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphEaseHint",
				"Every stretch between two keys, from 0 to 1: drag its handles to shape how it eases. A hollow handle shows a curve with no handles of its own yet - dragging it gives it some.");
		}

		inline FText GraphTrackRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphTrackRowTooltip",
				"A track shown in the graph, in its own color. Select other tracks in the Animation Tracks list to show them instead.");
		}

		inline FText GraphTrackEyeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphTrackEyeTooltip",
				"Shows or hides every channel of this track.");
		}

		inline FText GraphChannelTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphChannelTooltip",
				"A value the track's keys hold, and its value at the playhead. Click to bring its curve to the front; double-click to show it alone.");
		}

		// -------------------------------------------------------------------------------------------------
		// Part of the Text - a track on an animated text widget limited to some of its words (see FWidgetAnimTextTarget)
		// -------------------------------------------------------------------------------------------------

		inline FText TextTargetEmptyNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextTargetEmptyNote",
				"Type words of this text in Part of the Text to animate just those - the rest is left as it is.");
		}

		inline FText TextTargetNoTextNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextTargetNoTextNote",
				"Part of the Text is only found on an animated text widget: this track's widget isn't one in the preview, so it plays on the whole text there.");
		}

		inline FText TextTargetMissingNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextTargetMissingNote",
				"\"{0}\" isn't in the text (\"{1}\"), so this track plays on nothing. Check the spelling, Match Case and Whole Words Only.");
		}

		inline FText TextTargetPartMissingNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextTargetPartMissingNote",
				"Found {0} {0}|plural(one=place,other=places) - but \"{1}\" isn't in the text, so that part plays on nothing. Check its spelling between the &&.");
		}

		inline FText TextTargetFoundNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextTargetFoundNote",
				"Found {0} {0}|plural(one=place,other=places) in the text - {1} {1}|plural(one=letter,other=letters) at the first.");
		}

		inline FText GraphChannelEyeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "GraphChannelEyeTooltip",
				"Shows or hides this channel's curve.");
		}

		inline FText TextEditorEditTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorEditTooltip",
				"Edit Text: the text and its tags, with buttons that write the tags for you - select some words, then pick a style, effect or color. Ctrl+Z / Ctrl+Y undo and redo.");
		}

		inline FText TextEditorDoneTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorDoneTooltip",
				"Done: stop editing the text (Escape).");
		}

		inline FText TextEditorStyleTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorStyleTooltip",
				"Wraps the selection in one of this widget's Text Style Set rows - your project's own rich text styles. With nothing selected, puts an empty pair at the cursor to type into.");
		}

		inline FText TextEditorStyleRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorStyleRowTooltip",
				"Wraps the selection in <{0}>...</>, styled as the {0} row of the Text Style Set.");
		}

		inline FText TextEditorNoStyleSetNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorNoStyleSetNote",
				"This widget has no Text Style Set. Set one under Appearance to list its styles here.");
		}

		inline FText TextEditorNoStyleRowsNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorNoStyleRowsNote",
				"The Text Style Set has no rows besides Default.");
		}

		inline FText TextEditorEffectTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorEffectTooltip",
				"Wraps the selection in one of the project's Text Tags - shake, wave and the rest - set in Project Settings > Plugins > Widget Animations > Text Tags.");
		}

		inline FText TextEditorEffectRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorEffectRowTooltip",
				"Wraps the selection in <{0}>...</>, the {0} Text Tag.");
		}

		inline FText TextEditorNoEffectsNote()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorNoEffectsNote",
				"No Text Tags yet. Add them in Project Settings > Plugins > Widget Animations > Text Tags.");
		}

		inline FText TextEditorColorTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorColorTooltip",
				"Colors the selection: <color=#RRGGBB>...</>. Pick a swatch, a color, or type a hex code or color name.");
		}

		inline FText TextEditorPickColorTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorPickColorTooltip",
				"Opens the color picker. The color is applied to the selection when the picker is closed with OK.");
		}

		inline FText TextEditorSizeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorSizeTooltip",
				"Scales the selection's size: <size=x>...</>.");
		}

		inline FText TextEditorGlowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorGlowTooltip",
				"Makes the selection glow: <glow>...</>.");
		}

		inline FText TextEditorClearTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorClearTooltip",
				"Takes off the tags around the selection, and any inside it - the words stay.");
		}

		inline FText TextEditorBoxTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorBoxTooltip",
				"The text, tags and all. Select some of it and pick a tag above to wrap it. The widget follows as you type; Escape or Done when finished.");
		}

		inline FText TextEditorHint()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TextEditorHint",
				"Select some text, then pick a tag above to wrap it.");
		}
	}

	/** The Set Widget Animation Value node's dropdown pins, in the Blueprint graph. */
	namespace WidgetAnimValuePins
	{
		inline FText AnimationPicker()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "AnimationPicker",
				"The Widget Animation the value belongs to: one of those played in Anim In and Anim Out by the Widget Animation Components on the Target and inside it, or their Additive Tracks. Only that animation's tracks take the value, so the same Value Name in another animation is left alone.");
		}

		inline FText ProgressAnimationPicker()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "ProgressAnimationPicker",
				"The percentage animation (Time In Percent) to move: one of those played in Anim In and Anim Out by the Widget Animation Components on the Target and inside it. Only the entries playing it take the progress.");
		}

		inline FText NoPercentAnimations()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "NoPercentAnimations",
				"No Widget Animation Component on the Target or inside it plays a percentage animation in Anim In or Anim Out. Set an animation's Time In to Percent in the Widget Animations editor and it shows up.");
		}

		inline FText ValuePicker()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "ValuePicker",
				"The Value Name of a value-driven track (Timing = Value) in the picked Animation, with the type it takes. The Value pin changes to that type.");
		}

		inline FText SourceValues()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "SourceValues",
				"Values: {0}");
		}

		inline FText SourceHasNoValues()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "SourceHasNoValues",
				"No value-driven tracks in here yet. Set a track's Timing to Value in the Widget Animations editor and it shows up.");
		}

		inline FText NoAnimations()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "NoAnimations",
				"No Widget Animation Component on the Target or inside it has an animation in Anim In or Anim Out. Unconnected, the Target is this widget; connected to one of its designer widgets, that widget and what is inside it.");
		}

		inline FText NoValues()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "NoValues",
				"Nothing here is value-driven. Set a track's Timing to Value in the Widget Animations editor and it shows up.");
		}

		inline FText AnimationMissing()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "AnimationMissing",
				"No Widget Animation Component on the Target or inside it plays this animation anymore - it was removed, or the Target changed. Pick one that is.");
		}

		inline FText ValueMissing()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "ValueMissing",
				"No value-driven track here has this Value Name anymore: it was renamed, removed or is no longer Value timed. Pick one that is.");
		}

		inline FText StateMachinePicker()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "StateMachinePicker",
				"The state machine (WASM_) whose input to set: one a Widget Animation Component on the Target or inside it plays, in its State Machines. Unconnected, the Target is this widget.");
		}

		inline FText StateMachineMissing()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "StateMachineMissing",
				"No Widget Animation Component on the Target or inside it plays this state machine anymore - it was removed, or the Target changed. The node still sets it on any that plays it at runtime; pick one that does to be sure.");
		}

		inline FText MachineInputPicker()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "MachineInputPicker",
				"The input of the picked state machine to set. A Bool input takes a bool, a Number a float, and a Trigger nothing - the node fires it. The Value pin changes to fit.");
		}

		inline FText MachineInputMissing()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "MachineInputMissing",
				"The picked state machine has no input by this name anymore: it was renamed or removed in the State Machine editor. Pick one it has.");
		}

		inline FText StateMachineInputs()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "StateMachineInputs",
				"Inputs: {0}");
		}

		inline FText StateMachineHasNoInputs()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "StateMachineHasNoInputs",
				"No inputs of its own yet - add some in the Widget Animation State Machine editor's Inputs panel. Its built-in inputs (Hovered, Pressed...) are read from the widget and aren't set from here.");
		}

		inline FText NoStateMachines()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "NoStateMachines",
				"No Widget Animation Component on the Target or inside it plays a state machine. Add one to a component's State Machines and it shows up.");
		}

		inline FText NoMachineInputs()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "NoMachineInputs",
				"Nothing to pick: pick a State Machine first, or add inputs to it in the Widget Animation State Machine editor.");
		}
	}

	/** The Widget Animation State Machine editor: its graph, Inputs panel, toolbar and drawer. */
	namespace StateMachineEditor
	{
		inline FText NoMachineNote()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "NoMachineNote",
				"No state machine open. Pick a Widget Animation State Machine (WASM_) above, or make one with New - then add it to a Widget Animation Component's State Machines to play it.");
		}

		inline FText EntryNodeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "EntryNodeTooltip",
				"Entry: where the layer starts, as the widget is constructed or plays its Anim In. Its transition picks the first state - drag from Entry to a state, or right-click a state > Set As Entry State.");
		}

		inline FText ExitNodeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "ExitNodeTooltip",
				"Exit: the layer's Anim Out. As the widget closes, the Closing input turns on; a transition into Exit (Closing Is True, by default) plays the way out, and a closing screen waits for it.");
		}

		inline FText AnyStateNodeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "AnyStateNodeTooltip",
				"Any State: its transitions can be taken from whichever state the layer is in - checked first, before the state's own. For what must always win, such as Disabled.");
		}

		inline FText StateNodeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "StateNodeTooltip",
				"{0}\nPlays: {1}\nPlayback: {2}\n\nDouble-click to jump the preview into it; right-click to open its animation. Drag from its edge to another state to add a transition.");
		}

		inline FText SetEntryStateTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "SetEntryStateTooltip",
				"Set As Entry State: the layer starts in this state.");
		}

		inline FText JumpToStateTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "JumpToStateTooltip",
				"Jump To State (or double-click the state): sets the preview's inputs so the layer goes there by its own transitions - the fewest from where it is, played one after another - then leaves it there. Inputs the way there doesn't test go back to their defaults, and anything that would take the layer back out is set so it doesn't.");
		}

		inline FText LayerMaskBadgeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "LayerMaskBadgeTooltip",
				"This layer animates only some of the machine's targets (its Affects, in the layer's settings), or only some kinds of what its states animate (its Channels: opacity, scale, position, shear, rotation, material parameters, Shape Box outlines, texts and the rest): what it leaves out is left to the layers under it.");
		}

		inline FText ActionsBadgeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "ActionsBadgeTooltip",
				"This state does something as it is entered or left - sets an input, fires a trigger or sends an event (its On Enter / On Exit).");
		}

		inline FText CollapseToGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "CollapseToGroupTooltip",
				"Collapse States: shows the selected states as one box, as a Blueprint collapses nodes - double-click the box to open it. Only how the graph looks: the states play exactly as before, and transitions into and out of them run to the box.");
		}

		inline FText GroupNodeHint()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "GroupNodeHint",
				"Double-click to open it. Right-click to expand its states or rename it.");
		}

		inline FText MachineModeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "MachineModeTooltip",
				"State Machine edits this Widget Blueprint's own state machine, saved with it - as Animation Timeline is the Widget Animations panel's; Save State Machine To makes an asset of it any widget can play. Edit Existing Asset edits a Widget Animation State Machine asset in place.");
		}

		inline FText DraftMachineModeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "DraftMachineModeTooltip",
				"This Widget Blueprint's own state machine: kept and saved with the Widget Blueprint until Save State Machine To makes it an asset. Previews in the designer like any machine; a Widget Animation Component plays the asset it is saved as.");
		}

		inline FText EditExistingMachineTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "EditExistingMachineTooltip",
				"Edit a Widget Animation State Machine asset in place - every widget playing it plays the changes once it is saved.");
		}

		inline FText SaveDraftToTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "SaveDraftToTooltip",
				"Save State Machine To: makes a Widget Animation State Machine asset of this Widget Blueprint's machine - in the Default Folder, or wherever the editor's Save Asset As window says - for a Widget Animation Component to play. The machine stays open here; Save puts later changes into that asset.");
		}

		inline FText SaveDraftToSavedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "SaveDraftToSavedTooltip",
				"Save: put this Widget Blueprint's machine into {0}, the asset it was saved as, and save it. Every widget playing it plays the change.");
		}

		inline FText UnreadInputValueTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "UnreadInputValueTooltip",
				"Nothing in the machine reads this input yet, so its value can't be set here. It becomes editable as soon as something uses it - a condition, a state's Progress, Speed or Blend Input, a layer's Weight Input, a Parameters From Inputs or an action.");
		}

		inline FText LockMachineTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "LockMachineTooltip",
				"Lock the machine so that it is read only: a preview, with its graph, layers, inputs and Details grayed out. The preview's inputs still work, to try it.");
		}

		inline FText LeaveGroupTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "LeaveGroupTooltip",
				"Back to the whole layer, the collapsed states shown as their box again. Each step of the trail is the way back to it.");
		}

		inline FText MoveInputToCategoryTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "MoveInputToCategoryTooltip",
				"Lists the input under a category in this panel, as a Blueprint variable's Category groups it - or under none, first. Only how the panel lists it; the machine is unchanged.");
		}

		inline FText NewInputCategoryTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "NewInputCategoryTooltip",
				"Moves the input into a new category - double-click its heading to name it, or type it in the input's Category.");
		}

		inline FText InputCategoryTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "InputCategoryTooltip",
				"A category of inputs. Click to fold or unfold it; double-click its name to rename it (every input in it follows). Move an input in or out from its right-click menu, or with its Category.");
		}

		inline FText JumpToEntryTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "JumpToEntryTooltip",
				"Jump To Entry (or double-click Entry): starts the preview again from Entry, every input back to its default - where the machine is when its widget is made.");
		}

		inline FText JumpToExitTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "JumpToExitTooltip",
				"Jump To Exit (or double-click Exit): sets the preview's inputs - Closing, usually - so the layer takes its way to Exit, as it does when its widget closes. Jump To Entry, or turning Closing off, brings it back in.");
		}

		inline FText OpenStateAnimationTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "OpenStateAnimationTooltip",
				"Open Animation: opens the animation this state plays in the Widget Animations editor.");
		}

		inline FText ResetTransitionNodeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "ResetTransitionNodeTooltip",
				"Reset Node Position: puts the transition node back on the middle of its wire, where it was before it was dragged. Q does the same for selected transition nodes.");
		}

		inline FText RerouteNodeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "RerouteNodeTooltip",
				"The wire runs through this point. Drag it to lead the wire where you want it; Q lines it up with the point before it. Delete, or Alt+click, takes it off. Double-click a wire to add another.");
		}

		inline FText RemoveRerouteTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "RemoveRerouteTooltip",
				"Remove Reroute Point: takes this point off its wire. The wire runs straight on from the points either side of it.");
		}

		inline FText RenameStateTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "RenameStateTooltip",
				"Rename: type the state's new name and press Enter. Conditions on other layers that name it follow it.");
		}

		inline FText RenameCollapsedStatesTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "RenameCollapsedStatesTooltip",
				"Rename: type the collapsed states' new name and press Enter. Only how the graph shows them - nothing that plays changes.");
		}

		inline FText DraftMachineNameTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "DraftMachineNameTooltip",
				"The name of this Widget Blueprint's own state machine. Double-click it on the bar to rename it, or type it in the Details under State Machine. Save State Machine To names the asset after it.");
		}

		inline FText StraightWireTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "StraightWireTooltip",
				"Straight Wire: draws this transition's wire as a straight line from state to state - corner to corner between states that lie on a slant - instead of curving out of one and into the other. Straighten Connections (Q) sets it on wires between states on a slant. Only how the graph looks.");
		}

		inline FText ClearReroutesTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "ClearReroutesTooltip",
				"Remove Reroute Points: takes every reroute point off this transition's wire, so it runs straight from state to state again.");
		}

		inline FText RaisePriorityTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "RaisePriorityTooltip",
				"Raise Priority: checked before the other transitions leaving the same state. The first whose conditions all hold is taken.");
		}

		inline FText LowerPriorityTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "LowerPriorityTooltip",
				"Lower Priority: checked after the other transitions leaving the same state.");
		}

		inline FText AddStateActionTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "AddStateActionTooltip",
				"Adds a state here. Pick the animation it plays in its Details.");
		}

		inline FText LayerTabTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "LayerTabTooltip",
				"Layer {0}: {1}, weight {2}. Each layer is its own state machine, playing at the same time as the others - Override layers replace what the layers under them animate, Additive ones add to it. Click for its settings; double-click to rename it.");
		}

		inline FText AddLayerTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "AddLayerTooltip",
				"Add Layer: another layer, playing on top of the others - an Additive pulse while Selected, say, on top of the Base layer's hover.");
		}

		inline FText RemoveLayerTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "RemoveLayerTooltip",
				"Remove Layer: deletes this layer, its states and transitions. A machine always keeps one.");
		}

		inline FText AddInputTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "AddInputTooltip",
				"Add Input: a Bool, a Number or a Trigger the transitions test - set from a Blueprint (Set State Machine Input), a View Model, or a Value Input source measured on the widget.");
		}

		inline FText InputsPanelTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "InputsPanelTooltip",
				"The machine's inputs. Changing them here drives the preview in the designer - and, in Play In Editor, the widget being watched.");
		}

		inline FText NoInputsNote()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "NoInputsNote",
				"No inputs yet. Add one with +, or test the built-in ones below.");
		}

		inline FText ChoiceNoOptionsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "ChoiceNoOptionsTooltip",
				"The Choice has no options yet: give it some in its Details (click its row).");
		}

		inline FText HoldButtonTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "HoldButtonTooltip",
				"Hold: press and keep the mouse down to hold the input - it fills as it goes. Let go before its Hold Time and the hold doesn't complete; keep holding and it fires. Ticking the built-in Pressed below holds it too, as the widget's own press does at runtime.");
		}

		inline FText FireTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "FireTooltip",
				"Fire: sets the Trigger for one step - the transitions testing it Fired see it once.");
		}

		inline FText RemoveInputTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "RemoveInputTooltip",
				"Remove Input: deletes it. The conditions testing it stay, and never hold, until they are changed.");
		}

		inline FText BuiltInPanelTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "BuiltInPanelTooltip",
				"The built-in inputs, read from the widget itself - a Common UI button's states when the widget is one, its mouse and focus otherwise. Toggle them here to see each state in the preview.");
		}

		inline FText NewMachineTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "NewMachineTooltip",
				"New: makes a Widget Animation State Machine asset (WASM_) and opens it here.");
		}

		inline FText SaveMachineTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "SaveMachineTooltip",
				"Save: saves the state machine asset.");
		}

		inline FText PreviewToggleTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewToggleTooltip",
				"Preview In Designer: plays the machine in the designer, driven by the Inputs panel - on its Preview Targets, or on the widgets whose Widget Animation Component plays it (see Preview, on the left).");
		}

		inline FText JumpToActiveTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "JumpToActiveTooltip",
				"Jump to Active State: brings the state the watched widget is in into view - switching to its layer if the shown one isn't in a state.");
		}

		inline FText PreviewPauseTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewPauseTooltip",
				"Pause: holds the designer preview where it is - its state, its transition, its animations. Inputs still change, and take effect once it plays on.");
		}

		inline FText PreviewPlayTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewPlayTooltip",
				"Play: lets the paused designer preview run on from where it was held.");
		}

		inline FText DebugMenuTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "DebugMenuTooltip",
				"Debug: which widget playing this machine in Play In Editor the graph follows. It lights the state each layer is in and the transition being taken.");
		}

		inline FText DockInLayoutTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "DockInLayoutTooltip",
				"Dock in Layout: opens the State Machine editor as a tab of this editor's layout, instead of in the drawer.");
		}

		inline FText NoDebugTargetsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "NoDebugTargetsTooltip",
				"No widget in the game being played has a Widget Animation Component playing this machine.");
		}

		inline FText DebugTargetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "DebugTargetTooltip",
				"Follow this widget: the graph shows the state each layer of its machine is in.");
		}

		inline FText DebuggingStatus()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "DebuggingStatus",
				"Watching the game");
		}

		inline FText NotPlayingStatus()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "NotPlayingStatus",
				"Not playing in the game");
		}

		inline FText AssetEditorStatus()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "AssetEditorStatus",
				"Open a Widget Blueprint that uses this machine to preview it");
		}

		inline FText PreviewOffStatus()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewOffStatus",
				"Preview off");
		}

		inline FText PreviewingStatus()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewingStatus",
				"Previewing on {0} {0}|plural(one=widget,other=widgets)");
		}

		inline FText NothingToPreviewStatus()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "NothingToPreviewStatus",
				"Pick where the preview plays, under Preview");
		}

		inline FText PreviewingTargetsStatus()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewingTargetsStatus",
				"Previewing on {0}");
		}

		inline FText NoComponentsStatus()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "NoComponentsStatus",
				"No Widget Animation Component in the designer plays this machine");
		}

		inline FText MachinePickerTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "MachinePickerTooltip",
				"State Machine: the Widget Animation State Machine (WASM_) edited here. Any one - not only those this Widget Blueprint plays.");
		}

		inline FText AddStateButtonTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "AddStateButtonTooltip",
				"Add State: a state in the middle of the view - a plain one, or one ready-named for a built-in input: Hovered, Pressed, Selected, Focused, Disabled. Right-click the graph for the same, where you click.");
		}

		inline FText AddIdleStateTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "AddIdleStateTooltip",
				"Idle: the state at rest - where Entry usually goes, and where the others come back to.");
		}

		inline FText AddHoldingStateTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "AddHoldingStateTooltip",
				"Holding: a state played while the widget is pressed and held - a button growing and shaking as it charges. It uses the machine's Press And Hold input, made for it if there is none. Dragged from a state, it is joined from there on Is Held and back on Is Let Go: let go before the Hold Time, it blends back. Add a transition on Completed for where a finished hold goes.");
		}

		inline FText AddPresetStateTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "AddPresetStateTooltip",
				"{0}: a state for the built-in {1} input. Dragged from a state, its transition comes with the condition {1} Is True.");
		}

		inline FText PresetStateExistsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PresetStateExistsTooltip",
				"{0}: this layer has a {0} state already.");
		}

		inline FText AddCommentTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "AddCommentTooltip",
				"Add Comment: a comment box here, as in a Blueprint graph. Select states first and press C to put one round them.");
		}

		inline FText NodeCommentTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "NodeCommentTooltip",
				"Node Comment: a note shown in a bubble over the state. Empty it to take the bubble away.");
		}

		inline FText ToggleTransitionTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "ToggleTransitionTooltip",
				"Enable / Disable: a disabled transition is never taken - for trying the machine without it, without deleting it.");
		}

		inline FText StraightenTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "StraightenTooltip",
				"Straighten Connections (Q): lines the selected states up with the states they connect to, so their wires run straight. With one state selected, it lines up with the state it shares the most transitions with. States that lie on a slant stay where they are: the wire between them is drawn dead straight, corner to corner (Straight Wire). Works on collapsed states as on states. Nothing is moved onto another state.");
		}

		inline FText StateNodeHint()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "StateNodeHint",
				"Double-click to open its animation. Drag from its edge to another state to add a transition.");
		}

		inline FText InputRowTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "InputRowTooltip",
				"{0}: {1}\n\nClick for its settings. Double-click its name to rename it - the conditions that test it follow. Right-click to rename or delete it.");
		}

		inline FText BuiltInUnusedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "BuiltInUnusedTooltip",
				"{0}: no transition or setting of this machine reads it, so it can't be set here. {1}");
		}

		inline FText PreviewPanelTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewPanelTooltip",
				"Preview: where the machine plays in the designer. Targets plays it on widgets picked here - no Widget Animation Component needed. Components plays it on the widgets whose Widget Animation Component plays it.");
		}

		inline FText PreviewTargetsSourceTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewTargetsSourceTooltip",
				"Targets: the preview plays on the widgets picked below - the widget the machine plays on, and one for each of its targets.");
		}

		inline FText PreviewComponentsSourceTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewComponentsSourceTooltip",
				"Components: the preview plays on every widget of the designer whose Widget Animation Component plays this machine - only the selected one's, when it has the machine.");
		}

		inline FText PreviewComponentsNote()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewComponentsNote",
				"Playing on the widgets whose Widget Animation Component plays this machine, with their own Target Widgets.");
		}

		inline FText PreviewHostTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewHostTooltip",
				"Plays On: the widget the machine plays on in the preview - what its targets fall back to, as a component's widget would be. None: the Widget Blueprint itself.");
		}

		inline FText PreviewTargetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PreviewTargetTooltip",
				"{0}: the widget the machine's {0} target moves in the preview. Starts on the Target Widget of the animation group it is named for. None: the widget it plays on.");
		}

		inline FText NoPreviewTargetsNote()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "NoPreviewTargetsNote",
				"No targets yet: they are the Layer Groups of the animations the states play, and appear here once a state plays one.");
		}

		inline FText PickTargetInDesignerTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PickTargetInDesignerTooltip",
				"Pick in Designer: click the widget in the designer - or select it in its hierarchy. Escape, or click this again, to stop.");
		}

		inline FText PickingTargetNote()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PickingTargetNote",
				"Click the widget in the designer. Escape to stop.");
		}

		inline FText PickTargetNotFoundNote()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "PickTargetNotFoundNote",
				"That widget can't be a preview target - pick one of this Widget Blueprint's own.");
		}

		inline FText StateMachineDrawerTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.StateMachineEditor", "StateMachineDrawerTooltip",
				"Widget Animation State Machine: states and transitions that move widgets on their own - hovered, pressed, selected, a value - built like an Animation Blueprint's state machine.");
		}
	}

	/** Shape widgets (Ellipse, Triangle, Star, N-gon, Custom Shape): their Details rows and the designer's path editor. */
	namespace ShapeEditor
	{
		inline FText EditShapeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "EditShapeTooltip",
				"Edit Shape: edit the outline's vertices on the canvas. Drag vertices and handles; double-click an edge to add a vertex; Delete removes the selected; the Pen draws new ones. Enter or Escape when done.");
		}

		inline FText DoneTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "DoneTooltip",
				"Done: stop editing the outline (Enter or Escape).");
		}

		inline FText SelectToolTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "SelectToolTooltip",
				"Select (V): pick vertices and handles and drag them. Shift adds to the selection and keeps a drag straight; Ctrl while dragging turns snapping off; Ctrl-drag a Straight vertex to pull handles out of it; Alt-drag a handle to break it from its partner; double-click a vertex to switch it between Straight and Mirrored; double-click an edge to add a vertex there.");
		}

		inline FText PenToolTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PenToolTooltip",
				"Pen (P): click to add vertices after the selected end of an open contour - or start a new contour - and drag as you click to pull out curve handles. Click the contour's first vertex to close it. Click an edge to add a vertex on it. Shift keeps each new edge to 45 degrees.");
		}

		inline FText StraightTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "StraightTooltip",
				"Straight: no handles - straight edges meet at the vertex, and Corner Radius can round them.");
		}

		inline FText MirroredTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "MirroredTooltip",
				"Mirrored: handles opposite each other and the same length - a smooth curve.");
		}

		inline FText AsymmetricTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "AsymmetricTooltip",
				"Asymmetric: handles opposite each other, each its own length - smooth, pulled harder on one side.");
		}

		inline FText DisconnectedTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "DisconnectedTooltip",
				"Disconnected: each handle its own direction and length - a sharp point between curves.");
		}

		inline FText CornerRadiusTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "CornerRadiusTooltip",
				"Corner Radius: rounds the selected Straight vertices, in Slate units, along the edges either side - held to what fits.");
		}

		inline FText CloseContourTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "CloseContourTooltip",
				"Close Path: join the selected contour's last vertex back to its first.");
		}

		inline FText OpenContourTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "OpenContourTooltip",
				"Open Path: cut the selected contour between its last vertex and its first. Still filled across the gap; stroked with Caps at the ends.");
		}

		inline FText ReverseTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "ReverseTooltip",
				"Reverse Direction: run the selected contours (or all of them) the other way - a Trim Path then draws them on from the other end.");
		}

		inline FText DeleteVerticesTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "DeleteVerticesTooltip",
				"Delete: remove the selected vertices (Delete or Backspace).");
		}

		inline FText FitToBoxTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "FitToBoxTooltip",
				"Fit To Widget: move and scale the outline so it fills the widget's box exactly.");
		}

		inline FText PresetRowName()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetRowName",
				"Preset");
		}

		inline FText PresetTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetTooltip",
				"Shape presets of this kind of shape, saved for the project. Pick one to give this shape exactly its settings - outline, fill, stroke and trim. The arrow beside a preset applies only its outline.");
		}

		inline FText PresetComboEmpty()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetComboEmpty",
				"Apply or Save a Preset...");
		}

		inline FText PresetNoneSaved()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetNoneSaved",
				"No presets of this shape yet. Name the current settings below to save the first.");
		}

		inline FText PresetSaveHint()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetSaveHint",
				"Name the current settings...");
		}

		inline FText PresetSaveTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetSaveTooltip",
				"Save the selected shape's settings as a preset under this name, for every shape of this kind in the project. A preset of the same name is replaced.");
		}

		inline FText PresetApplyShapeOnlyTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetApplyShapeOnlyTooltip",
				"Apply only the outline - keep this shape's own fill, stroke and trim.");
		}

		inline FText PresetDeleteTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetDeleteTooltip",
				"Delete this preset. Shapes it was applied to keep their settings.");
		}

		inline FText PresetOpenFolder()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetOpenFolder",
				"Open Presets Folder");
		}

		inline FText PresetOpenFolderTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetOpenFolderTooltip",
				"Show the presets' files: one per preset in Content/WidgetShapePresets, shared through source control like any project file.");
		}

		inline FText PresetSaved()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetSaved",
				"Saved shape preset \"{0}\".");
		}

		inline FText PresetReplaceConfirm()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetReplaceConfirm",
				"A {0} preset named \"{1}\" already exists. Replace it?");
		}

		inline FText PresetMixedSelection()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "PresetMixedSelection",
				"Presets need shapes of one kind selected.");
		}

		inline FText ShapeToolsRow()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "ShapeToolsRow",
				"Outline");
		}

		inline FText EditOnCanvas()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "EditOnCanvas",
				"Edit Shape");
		}

		inline FText EditOnCanvasActive()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "EditOnCanvasActive",
				"Editing...");
		}

		inline FText EditOnCanvasTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "EditOnCanvasTooltip",
				"Edit the outline on the designer's canvas: vertices, handles and the Pen, as in a vector tool. Select the shape in the designer to see it.");
		}

		inline FText StartFromTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "StartFromTooltip",
				"Start From: replace the outline with a shape's, as vertices to edit - a primitive with its settings, or one of this Widget Blueprint's own shape widgets, exactly as it is.");
		}

		inline FText StartFromLabel()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "StartFromLabel",
				"Start From");
		}

		inline FText StartFromPrimitives()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "StartFromPrimitives",
				"Shapes");
		}

		inline FText StartFromWidgets()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "StartFromWidgets",
				"Shapes In This Widget");
		}

		inline FText FlipHorizontal()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "FlipHorizontal",
				"Flip Horizontally");
		}

		inline FText FlipVertical()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "FlipVertical",
				"Flip Vertically");
		}

		inline FText OutlineSummary()
		{
			return NSLOCTEXT("NameTBDTooltips.ShapeEditor", "OutlineSummary",
				"{0} vertices in {1} contours");
		}
	}


	namespace FlowListPathEditor
	{
		inline FText EditPathTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "EditPathTooltip",
				"Edit Path: edit the Spline path's points on the canvas - a Flow List's or a Flow List Scroll Bar's. Drag points, or the focus marker along the curve; double-click the curve to add a point; Delete removes the selected; the Pen adds points at the ends. Enter or Escape when done.");
		}

		inline FText DoneTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "DoneTooltip",
				"Done: stop editing the path (Enter or Escape).");
		}

		inline FText SelectToolTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "SelectToolTooltip",
				"Select (V): pick points and drag them - snapping to the list's edges and middle and to the other points' lines. Shift adds to the selection and keeps a drag straight; Ctrl while dragging turns snapping off; drag on empty space to box-select. Drag the focus marker along the curve to move the focus; Shift snaps it to quarters. Double-click the curve to add a point there.");
		}

		inline FText PenToolTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "PenToolTooltip",
				"Pen (P): click to add a point after the selected end of the path - or the end nearer the click - and drag as you click to place it. Click the curve to add a point on it. Shift keeps each new piece to 45 degrees.");
		}

		inline FText ReverseTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "ReverseTooltip",
				"Reverse Direction: run the path the other way - the first item then sits at the other end. The focus stays where it is on the curve.");
		}

		inline FText DeletePointsTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "DeletePointsTooltip",
				"Delete: remove the selected points (Delete or Backspace). A path keeps at least two.");
		}

		inline FText PreviewEnterTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "PreviewEnterTooltip",
				"Enter: play every item's entrance in the preview, as Play Intro does - Enter, or Anim Enter Item without Use Basic Animations. Editing a transition plays it too.");
		}

		inline FText PreviewExitTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "PreviewExitTooltip",
				"Exit: play every item's exit in the preview - Exit, or Anim Exit Item without Use Basic Animations - then bring them back in. Nothing is removed.");
		}

		inline FText PreviewItemEnterTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "PreviewItemEnterTooltip",
				"Enter: play this item's entrance in the preview, as it arrives when added - Enter, or Anim Enter Item without Use Basic Animations.");
		}

		inline FText PreviewItemExitTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "PreviewItemExitTooltip",
				"Exit: play this item's exit in the preview, as it leaves when removed - Exit, or Anim Exit Item - then bring it back in. Nothing is removed.");
		}

		inline FText NotSplineTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.FlowListPathEditor", "NotSplineTooltip",
				"Edit Path: only a Spline path has points to edit - set a Flow List's Layout > Type, or a Scroll Bar's Path, to Spline.");
		}
	}

}