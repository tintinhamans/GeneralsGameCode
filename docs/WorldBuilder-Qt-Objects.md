# Selection and objects

* Shift+Ctrl+drag over empty ground removes the boxed objects from the selection. The box draws red.
* Clicking a tree selects the tree under the cursor. Objects hidden under the ground are not picked
through it.
* Object Options - (A category header places every object in the category at once, with manual
spacing. Leaves tinted green are objects the map's `map.ini` adds; orange ones are stock objects
it redefines.)
* Object Properties - (A Listen button plays the object's attached sound. Scale sets the object's
visual size on top of the template `Scale`, for the whole selection.)
* Selecting objects skips thumbnail rendering while the Object Options browser is hidden.
* Selecting objects updates the placement template without searching the hidden legacy tree.
* Placing an object whose template has `InstanceScaleFuzziness` gives it a random scale in that range.
* Build List - (Replace swaps the selected building for another and keeps its position, angle,
rebuild count and flags. Follow Object moves the view to the selected entry. Import of a file
with several `SkirmishBuildList` blocks, such as `AIData.ini`, gives every player the list matching
its faction side, so one import fills all players of a skirmish script file. A file with one list
imports it into the selected player. Show Selected Player Only draws and picks only the selected
player's buildings while the tool is open.)
* Select Similar works on roads and bridges.
* Fence Tool - (The search box filters the fence-only list as well as the full object list.)
* The Object tool keeps its picked object and owning team through a camera pan.

