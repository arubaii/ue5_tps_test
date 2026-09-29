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
				"Letters, words and lines can only be moved by Fade, Scale, Slide, Rotate, Pulse and Transform tracks. This track plays once, on the whole widget.");
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

		// -------------------------------------------------------------------------------------------------
		// Panel
		// -------------------------------------------------------------------------------------------------

		inline FText NothingToEdit()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NothingToEdit",
				"Pick the Widget Animation asset to edit above, or switch to New Animation.");
		}

		// -------------------------------------------------------------------------------------------------
		// Panel header
		// -------------------------------------------------------------------------------------------------

		inline FText ModeTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "ModeTooltip",
				"New Animation builds a new one, saved with Save as Animation Asset. Edit Existing Asset edits a Widget Animation asset in place, saved with Update Animation Asset.");
		}

		inline FText NewAnimationTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "NewAnimationTooltip",
				"Build a new animation, and save it as a new Widget Animation asset when it is ready.");
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
				"Save this animation as a new WA_ Widget Animation asset beside this Widget Blueprint, and go on editing it.");
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
				"Playback Speed: How fast the preview plays, as an entry's own Playback Speed would play it - delays included. Only the preview: the animation's own Animation Speed Multiplier is in Animation Properties.");
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
				"Go to End");
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
				"The widget this group animates here. Change it by selecting the group.");
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
				"Unwrap another Widget Animation's tracks into this one, where a new track would go - after the selected track, or at the end - to build on it.");
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
				"Pick the Widget Animation asset to edit, or switch to New Animation.");
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
				"Add a key at this moment, starting at the value the track already has there. Double-clicking the track does the same.");
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
				"Pick which registered type this track is in its Type Params, in the Selection panel. Until then it does nothing.");
		}

		inline FText TransformShapeBoxWarningTooltip()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimEditor", "TransformShapeBoxWarningTooltip",
				"Animate Shape needs a Shape Box: the target widget itself, or the Shape Box it sits directly inside.");
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
	}

	/** The Set Widget Animation Value node's dropdown pins, in the Blueprint graph. */
	namespace WidgetAnimValuePins
	{
		inline FText AnimationPicker()
		{
			return NSLOCTEXT("NameTBDTooltips.WidgetAnimValuePins", "AnimationPicker",
				"The Widget Animation the value belongs to: one of those played in Anim In and Anim Out by the Widget Animation Components on the Target and inside it, or their Additive Tracks. Only that animation's tracks take the value, so the same Value Name in another animation is left alone.");
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
	}
}