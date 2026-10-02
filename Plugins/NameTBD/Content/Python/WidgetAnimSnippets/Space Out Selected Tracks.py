# The selected tracks start one after another, STEP seconds apart, in the order they start now.
# Select some tracks in the panel, then Run.

STEP = 0.1

tracks = sorted(selection.tracks, key=lambda track: track.start)
if not tracks:
    raise ScriptError("Select some tracks in the panel first.")

first = tracks[0].start
for i, track in enumerate(tracks):
    track.start = first + i * STEP
print(f"Spaced out {len(tracks)} tracks.")
