# Data binding

## Widget Data Binder
A UI component that connects a View Model (MVVM, Field Notify) to the widget: Field Bindings (field -> widget property), Triggers (rules), List Bindings (arrays -> entry widgets), Mock Values and Sample Sets (designer data).

## Triggers
Condition: Changed, Is True / False, compare to a value or Another Field (Compare To), Went Up / Went Down. Action: Play / Play While True / Stop an animation (by Anim ID on the Animation Widget), Set Animation Parameter, Count Into Field (a number ticks to the new value over Count Duration, written as a number or formatted text). Hysteresis avoids flicker.

## Lists
List Binding: ViewModel Field (an array of structs or View Models), List Widget (a Flow List animates; any panel works), Entry Widget Class, Key Field (identity across updates), Position Field (1-based place written into each entry), Animate Changes. Each entry's own binder gets its element.

## Designer preview
Run Triggers In Designer (default on) plays rules in the designer. The binder's Details show Data Preview: step Sample Sets, edit values live, add / remove / shuffle list rows - no play needed.
