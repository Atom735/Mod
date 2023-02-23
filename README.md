# Beta Release We The People v4.0-beta1
**!!!this is still incomplete!!!**

## Important installation instructions
This branch uses Intel Threading Building blocks to achieve concurrent AI calculations in order to speed up the AI (inter)turn. 
Before you start:

**copy tbb.dll and tbbmalloc.dll from "Project Files\tbb" to the directory where Colonization.exe resides!**

Otherwise the mod will not work!

## Known issues
* Attacking a stack of civilian units with a stack of own units can cause some of the civilian units to disappear ([#582](https://github.com/We-the-People-civ4col-mod/Mod/issues/582))
* Attacking a stack of civilian units with a unit having the "multiple attacks per turn" promotion can cause some of the civilian units to disappear ([#582](https://github.com/We-the-People-civ4col-mod/Mod/issues/582))
* Combat Forecast sometimes not shown ([#557](https://github.com/We-the-People-civ4col-mod/Mod/issues/557))
* On very large maps, zooming out might cause the terrain appear completely black ([#723](https://github.com/We-the-People-civ4col-mod/Mod/issues/723))


## Changes compared to We The People v3.x
### Big Changes
* Several new terrains
* Many new yields and manufacturing products
* Buildings and specialists for the new yields and manufacturing products
* Buildings require additional different resources (e.g. clay)
* Equipping units now requires additional different resources (bakery products, gunpowder)
* Artillery are now colonist units with equipment (cannons + gunpowder) and have specialists
* Diversified naval units and royal units

### Other gameplay changes
* City growth now gives different types of colonists depending on the people that work in the city (free/forced laborers, ancestry)
* Slaves will sometimes not run away, but start an armed rebellion against their oppression in the colonies
* Several new units have been added (buccaneer, slave overseer, slave hunter)
* A huge amount of new events

### Maps
* All maps have been revised for the new terrains
* Most of the new terrain is now generated by FaireWeatherTweakEx.py  

### Translations
* English and German translation have been updated
* Russian translation is quickly progressing
* Other translations might not be completely updated to the current version

### AI
* Many AI improvements (including several critical issues that greatly impaired the AI)  
* Logging of AI actions (to enable ensure that LoggingEnabled = 1 is set in CivilizationIV.ini, the log itself is found in Logs\BBAI.log  

### Bugfixes
* We fixed so many bugs we don't remember all of them ... ;)
* Multiple OOS (multiplayer desync) bugs have been fixed

### Internals
* Ported and adapted Karadoc's pathfinder (originally written for the Civ4 mod K-Mod)  
* Parallelized the job assigner subsystem and the K-mod pathfinder
* A new class Coordinates to be used instead of x, y parameters
* the fixes from optimizations2
* several changes of variables from int to the more specifix XYZTypes
* some other changes that were convenient to write during the merge of branches coordinate_class/optimizations2 (I did not make separate commits because the merge did not complete successfully without them)
