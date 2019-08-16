#include "Change.h"

#include "Base/Random.h"
#include "Game/Data.h"
#include "Game/Tick.h"
#include "Map.h"
#include "MapData.h"
#include "MapUtil.h"
#include "Things/Info.h"
#include "Things/Interactions.h"
#include "Things/MapObj.h"
#include "Things/Move.h"

/**********************************

        SECTOR HEIGHT CHANGING

    After modifying a sectors floor or ceiling height, call this
    routine to adjust the positions of all things that touch the
    sector.

    If anything doesn't fit anymore, true will be returned.
    If crunch is true, they will take damage as they are being crushed
    If Crunch is false, you should set the sector height back the way it
    was and call ChangeSector again to undo the changes

**********************************/

static uint32_t gCrushChange;       // If true, then crush bodies to blood 
static uint32_t gNoFit;             // Set to true if something is blocking 

/**********************************

    Takes a valid thing and adjusts the thing->floorz, thing->ceilingz,
    and possibly thing->z

    This is called for all nearby monsters whenever a sector changes height

    If the thing doesn't fit, the z will be set to the lowest value and
    false will be returned

**********************************/

static uint32_t ThingHeightClip(mobj_t *thing)
{
    uint32_t onfloor;
    onfloor = (thing->z == thing->floorz);  // Already on the floor? 

    // Get the floor and ceilingz from the monsters position 

    P_CheckPosition(thing,thing->x,thing->y);

    // What about stranding a monster partially off an edge? 

    thing->floorz = gTmpFloorZ;       // Save off the variables 
    thing->ceilingz = gTmpCeilingZ;

    if (onfloor) {  // walking monsters rise and fall with the floor 
        thing->z = thing->floorz;   // Pin to the floor 
    } else {    // don't adjust a floating monster unless forced to 
        if ((thing->z+thing->height) > thing->ceilingz) {
            thing->z = thing->ceilingz - thing->height;
        }
    }

    if ((thing->ceilingz - thing->floorz) < thing->height) {
        return false;       // Can't fit!! 
    }
    return true;        // I can fit! 
}

/**********************************

    This is called from BlockThingsIterator

**********************************/

static uint32_t PIT_ChangeSector(mobj_t *thing)
{
    if (ThingHeightClip(thing)) {       // Too small? 
        return true;        // Keep checking 
    }

    // crunch bodies to giblets 
    if (!thing->MObjHealth) {           // No health... 
        SetMObjState(thing,&gStates[S_GIBS]);    // Change to goo 
        thing->height = 0;  // No height 
        thing->radius = 0;  // No radius 
        return true;        // keep checking 
    }

    // crunch dropped items 

    if (thing->flags & MF_DROPPED) {
        P_RemoveMobj(thing);        // Get rid of it 
        return true;        // keep checking 
    }

    if (!(thing->flags & MF_SHOOTABLE)) {   // Don't squash critters 
        return true;                // assume it is bloody gibs or something 
    }

    gNoFit = true;       // Can't fit 
    if (gCrushChange && gTick4) {    // Crush it? 
        mobj_t *mo;
        DamageMObj(thing,0,0,10);       // Take some damage 
        // spray blood in a random direction 
        mo = SpawnMObj(thing->x,thing->y,thing->z + thing->height/2,&gMObjInfo[MT_BLOOD]);
        mo->momx = (255 - (Fixed) Random::nextU32(511)) << (FRACBITS - 4);  // Have it jump out 
        mo->momy = (255 - (Fixed) Random::nextU32(511)) << (FRACBITS - 4);
    }
    return true;        // keep checking (crush other things) 
}

/**********************************

    Scan all items that are on a specific block
    to see if it can be crushed.

**********************************/

uint32_t ChangeSector(sector_t *sector, uint32_t crunch)
{
    uint32_t x,y;
    uint32_t x2,y2;

// force next sound to reflood 


    gPlayer.lastsoundsector = 0;

    gNoFit = false;          // Assume that it's ok 
    gCrushChange = crunch;   // Can I crush bodies 

// recheck heights for all things near the moving sector 

    x2 = sector->blockbox[BOXRIGHT];
    y2 = sector->blockbox[BOXTOP];
    x = sector->blockbox[BOXLEFT];
    do {
        y = sector->blockbox[BOXBOTTOM];
        do {
            BlockThingsIterator(x,y,PIT_ChangeSector);  // Test everything 
        } while (++y<y2);
    } while (++x<x2);
    return gNoFit;       // Return flag 
}
