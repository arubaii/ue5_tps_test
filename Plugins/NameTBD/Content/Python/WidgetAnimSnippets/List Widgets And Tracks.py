# Prints the Widget Blueprint's widgets - names to target - and the animation's tracks. Changes nothing.

def show(widget, depth=0):
    print("  " * depth + f"{widget.name}  ({widget.class_name})")
    for child in widget.children:
        show(child, depth + 1)

if anim.root:
    show(anim.root)
else:
    print("No widgets to list: open the Designer so the preview exists.")

print()
print(anim)
anim.print_tracks()
