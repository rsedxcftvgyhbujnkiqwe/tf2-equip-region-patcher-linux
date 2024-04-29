# TF2 Equip Region Patcher - Linux Edition
This is a simple C program which patches the memory of TF2 as it opens in order to disable all equip regions. This allows the equipping of conflicting items, such as two different hats with the same equip region.
# Concerns
VAC detects cheats via active memory editing, and with signatures of existing cheats. Therefore, as long as this patch is run before joining a VAC server, it should not be detected.

Checkout [this steam forum post](https://steamcommunity.com/sharedfiles/filedetails/?id=951010440) for an idea of what I'm talking about.
# Installation
Clone the repository
```
$ git clone https://github.com/rsedxcftvgyhbujnkiqwe/tf2-equip-region-patcher-linux equip-patcher
```
Run make in order to build the binary
```
$ cd equip-patcher
$ make
```
# Usage
Call the binary

NOTE: Requires root in order to access and modify the memory of the process. Consider adding exceptions into sudo/doas in order to allow it to run without password
```
# ./equip-patcher
```
This will start the process. It will continually search for TF2 to open, and once open, search for the memory pattern. Note that the module which contains the pattern is not opened immediately, and will be present after the intro video cutscene.

This must be run **before** starting your game. While the game is loading, before reaching the main menu, your inventory's equip regions are being calculated. So the patch must be put in before that happens, since equip regions are only generated once on startup. If you do not have the patch running and you open the game, any conflicting items will be unequipped. If you use the patcher on startup every time, items will stay equipped.
## Automatic running
This script can be run via the launch options for TF2.

Within launch options, set your options to be:

```
doas /path/to/equip-patcher & %command% -regular -launch-options
```
**NOTE:** You will have to run this with root permissions. This can be accomplished with sudo or doas by whitelisting the program to be runnable without password. In my case, I use doas but it could easily be sudo as long as the binary is whitelisted.
# Requirements
- GCC
# How it works
Since it took me days to learn how this was performed, from looking at other programs, I will describe the methodology in case this ever becomes outdated and someone else needs to maintain it.

Within `game/shared/econ/econ_item_schema.cpp`, there is a function which is run while the game is opening that calculates the equip region of all items in your inventory.

Specifically, the variables `m_unEquipRegionMask` and `m_unEquipRegionConflictMask` are set to zero, and then updated with the mask of which equip regions it has, and conflicts with.

What this code does is edits a patch of memory right after those are set to zero, in order to bypass the entire section of code in which the regions are acquired and set. This way, the equip regions for every item are zero and any item can be used with any other item.
