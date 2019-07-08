#include "Renderer_Internal.h"

BEGIN_NAMESPACE(Renderer)

// FIXME: FIND homes for these

/**********************************

    Perform a "Doom" like screen wipe
    I require that VideoPointer is set to the current screen

**********************************/

#define WIPEWIDTH 320       // Width of the 3DO screen to wipe 
#define WIPEHEIGHT 200

// DC: TODO: unused currently
#if 0
void WipeDoom(LongWord *OldScreen,LongWord *NewScreen)
{
    LongWord Mark;  // Last time mark 
    Word TimeCount; // Elapsed time since last mark 
    Word i,x;
    Word Quit;      // Finish flag 
    int delta;      // YDelta (Must be INT!) 
    LongWord *Screenad;     // I use short pointers so I can 
    LongWord *SourcePtr;        // move in pixel pairs... 
    int YDeltaTable[WIPEWIDTH/2];   // Array of deltas for the jagged look 

// First thing I do is to create a ydelta table to 
// allow the jagged look to the screen wipe 

    delta = -GetRandom(15); // Get the initial position 
    YDeltaTable[0] = delta; // Save it 
    x = 1;
    do {
        delta += (GetRandom(2)-1);  // Add -1,0 or 1 
        if (delta>0) {      // Too high? 
            delta = 0;
        }
        if (delta == -16) { // Too low? 
            delta = -15;
        }
        YDeltaTable[x] = delta; // Save the delta in table 
    } while (++x<(WIPEWIDTH/2));    // Quit? 

// Now perform the wipe using ReadTick to generate a time base 
// Do NOT go faster than 30 frames per second 

    Mark = ReadTick()-2;    // Get the initial time mark 
    do {
        do {
            TimeCount = ReadTick()-Mark;    // Get the time mark 
        } while (TimeCount<(TICKSPERSEC/30));           // Enough time yet? 
        Mark+=TimeCount;        // Adjust the base mark 
        TimeCount/=(TICKSPERSEC/30);    // Math is for 30 frames per second 

// Adjust the YDeltaTable "TimeCount" times to adjust for slow machines 

        Quit = true;        // Assume I already am finished 
        do {
            x = 0;      // Start at the left 
            do {
                delta = YDeltaTable[x];     // Get the delta 
                if (delta<WIPEHEIGHT) { // Line finished? 
                    Quit = false;       // I changed one! 
                    if (delta < 0) {
                        ++delta;        // Slight delay 
                    } else if (delta < 16) {
                        delta = delta<<1;   // Double it 
                        ++delta;
                    } else {
                        delta+=8;       // Constant speed 
                        if (delta>WIPEHEIGHT) {
                            delta=WIPEHEIGHT;
                        }
                    }
                    YDeltaTable[x] = delta; // Save new delta 
                }
            } while (++x<(WIPEWIDTH/2));
        } while (--TimeCount);      // All tics accounted for? 

// Draw a frame of the wipe 

        x = 0;          // Init the x coord 
        do {
            Screenad = (LongWord *)&VideoPointer[x*8];  // Dest pointer 
            i = YDeltaTable[x];     // Get offset 
            if ((int)i<0) { // Less than zero? 
                i = 0;      // Make it zero 
            }
            i>>=1;      // Force even for 3DO weirdness 
            if (i) {
                TimeCount = i;
                SourcePtr = &NewScreen[x*2];    // Fetch from source 
                do {
                    Screenad[0] = SourcePtr[0]; // Copy 2 longwords 
                    Screenad[1] = SourcePtr[1];
                    Screenad+=WIPEWIDTH;
                    SourcePtr+=WIPEWIDTH;
                } while (--TimeCount);
            }
            if (i<(WIPEHEIGHT/2)) {     // Any of the old image to draw? 
                i = (WIPEHEIGHT/2)-i;
                SourcePtr = &OldScreen[x*2];
                do {
                    Screenad[0] = SourcePtr[0]; // Copy 2 longwords 
                    Screenad[1] = SourcePtr[1];
                    Screenad+=WIPEWIDTH;
                    SourcePtr+=WIPEWIDTH;
                } while (--i);
            }
        } while (++x<(WIPEWIDTH/2));
    } while (!Quit);        // Wipe finished? 
}
#endif

/**********************************

    Draw a sprite in the center of the screen.
    This is used for the finale.
    (Speed is NOT an issue here...)

**********************************/

void DrawSpriteCenter(uint32_t SpriteNum)
{
    // DC: FIXME: implement/replace
    #if 0
        Word x,y;
        patch_t *patch;
        LongWord Offset;

        patch = (patch_t *)LoadAResource(SpriteNum>>FF_SPRITESHIFT);    // Get the sprite group 
        Offset = ((LongWord *)patch)[SpriteNum & FF_FRAMEMASK];
        if (Offset&PT_NOROTATE) {       // Do I rotate? 
            patch = (patch_t *) &((Byte *)patch)[Offset & 0x3FFFFFFF];      // Get pointer to rotation list 
            Offset = ((LongWord *)patch)[0];        // Use the rotated offset 
        }
        patch = (patch_t *)&((Byte *)patch)[Offset & 0x3FFFFFFF];   // Get pointer to patch 
    
        x = patch->leftoffset;      // Get the x and y offsets 
        y = patch->topoffset;
        x = 80-x;           // Center on the screen 
        y = 90-y;
        ((LongWord *)patch)[7] = 0;     // Compensate for sideways scaling 
        ((LongWord *)patch)[10] = 0;
        if (Offset&PT_FLIP) {
            ((LongWord *)patch)[8] = -0x2<<20;  // Reverse horizontal 
            x+=GetShapeHeight(&patch->Data);    // Adjust the x coord 
        } else {
            ((LongWord *)patch)[8] = 0x2<<20;   // Normal horizontal 
        }
        ((LongWord *)patch)[9] = 0x2<<16;       // Double vertical 
        DrawMShape(x*2,y*2,&patch->Data);       // Scale the x and y and draw 
        ReleaseAResource(SpriteNum>>FF_SPRITESHIFT);    // Let go of the resource 
    #endif
}

END_NAMESPACE(Renderer)