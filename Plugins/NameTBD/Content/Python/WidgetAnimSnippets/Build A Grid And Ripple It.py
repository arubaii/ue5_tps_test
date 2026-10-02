# Builds an N x N grid of borders in a Uniform Grid Panel, and a loading ripple across it - each border shrinks and
# grows back, in a diagonal wave from the top left. Change N and run again: the grid and the ripple follow.
#
# Needs a Uniform Grid Panel called GRID in the Widget Blueprint, with one styled Border in it to copy. With an empty
# grid, a plain Border is made to start from.

GRID = "Grid"       # the Uniform Grid Panel
N = 4               # cells along each side
STEP = 0.06         # seconds between one diagonal and the next
DURATION = 0.5      # how long each border's shrink and grow takes
SHRINK_TO = 0.0     # how small each border gets
NAME = "Ripple"     # the collapsed track it makes

grid = anim.widget(GRID)
if grid is None:
    grid = anim.add_widget("UniformGridPanel", into=anim.root, name=GRID)
    print(f"Made a Uniform Grid Panel called {GRID}.")

template = grid.children[0] if grid.children else "Border"
cells = anim.fill_grid(grid, rows=N, columns=N, template=template)

anim.remove(anim.collapsed(NAME))
made = []
for i, cell in enumerate(cells):
    row, column = divmod(i, N)
    made.append(anim.add_track(
        TrackType.SCALE, target=cell,
        start=(row + column) * STEP, duration=DURATION,
        values=(1.0, 1.0), keys=[(0.5, SHRINK_TO)],
        easing=Ease.EASE_IN_OUT))
anim.collapse(made, NAME)

# The cells' groups listed together in the panel's Animation Tracks list, under one List Folder - only for tidiness.
for track in made:
    if track.group is not None:
        track.group.folder = NAME

print(f"{N}x{N} grid, {len(made)} tracks, one wave every {anim.length:.2f} s.")
