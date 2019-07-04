#include "Renderer_Internal.h"

#include "Base/Tables.h"
#include "Burger.h"
#include "Map/MapData.h"
#include "Map/MapUtil.h"
#include "Textures.h"

BEGIN_NAMESPACE(Renderer)

/**********************************

    Check all the visible walls and fill in all the "Blanks" such
    as texture pointers and sky hack variables.
    When finished all the viswall records are filled in.
    
**********************************/

static sector_t gEmptySector = { 0,0,-2,-2,-2 }; // -2 floorpic, ceilingpic, light

Fixed point2dToDist(const Fixed px, const Fixed py) noexcept {
    //------------------------------------------------------------------------------------------------------------------
    // Get the distance from the view x,y from a point in 2D space.
    // The normal formula for doing this is 'dist = sqrt(x*x + y*y)' but that can be slow to do. 
    // Instead I first get the angle of the point and then rotate it so that it is directly ahead.
    // The resulting point then is the distance...
    //------------------------------------------------------------------------------------------------------------------    
    Fixed x = std::abs(px - gViewX);    // Get the absolute value point offset from the camera 
    Fixed y = std::abs(py - gViewY);
    
    if (y > x) {
        const Fixed temp = x;
        x = y;
        y = temp;
    }

    const angle_t angle = SlopeAngle(y, x) >> ANGLETOFINESHIFT;     // x = denominator
    x = (x >> (FRACBITS - 3)) * gFineCosine[angle];                 // Rotate the x
    x += (y >> (FRACBITS - 3)) * gFineSine[angle];                  // Rotate the y and add it
    x >>= 3;                                                        // Convert to fixed (I added 3 extra bits of precision)
    return x;                                                       // This is the true distance
}

/**********************************

    Returns the texture mapping scale for the current line at the given angle
    rw_distance must be calculated first

**********************************/

static Fixed ScaleFromGlobalAngle(Fixed rw_distance,angle_t anglea,angle_t angleb)
{
    Fixed num,den;
    Fixed *SineTbl;

// both sines are always positive

    SineTbl = &gFineSine[ANG90>>ANGLETOFINESHIFT];
    den = SineTbl[anglea>>ANGLETOFINESHIFT];
    num = SineTbl[angleb>>ANGLETOFINESHIFT];
    
    num = fixedMul(gStretchWidth ,num);
    den = fixedMul(rw_distance, den);

    if (den > num>>16) {
        num = fixedDiv(num, den);       // Place scale in numerator
        if (num < 64*FRACUNIT) {
            if (num >= 256) {
                return num;
            }
            return 256;     // Minimum scale value
        }
    }
    return 64*FRACUNIT;     // Maximum scale value
}

/**********************************

    Calculate the wall scaling constants

**********************************/

static void LatePrep(viswall_t *wc,seg_t *LineSeg,angle_t LeftAngle)
{
    angle_t normalangle;        // Angle to wall
    Fixed PointDistance;        // Distance to end wall point
    Fixed rw_distance;
    angle_t offsetangle;
    Fixed scalefrac;
    Fixed scale2;

//
// calculate normalangle and rw_distance for scale calculation
// and texture mapping
//

    normalangle = LineSeg->angle + ANG90;
    offsetangle = (normalangle - LeftAngle);
    if ((int)offsetangle < 0) {
        offsetangle = -offsetangle;
    }
    if (offsetangle > ANG90) {
        offsetangle = ANG90;
    }
    PointDistance = point2dToDist(LineSeg->v1.x, LineSeg->v1.y);
    wc->distance = rw_distance = fixedMul(
        PointDistance,
        gFineSine[(ANG90 - offsetangle)>>ANGLETOFINESHIFT]
    );

//
// calc scales
//

    offsetangle = gXToViewAngle[wc->LeftX];
    scalefrac = scale2 = wc->LeftScale = ScaleFromGlobalAngle(rw_distance,
        offsetangle,(offsetangle+gViewAngle)-normalangle);
    if (wc->RightX > wc->LeftX) {
        offsetangle = gXToViewAngle[wc->RightX];
        scale2 = ScaleFromGlobalAngle(rw_distance,offsetangle,
            (offsetangle+gViewAngle)-normalangle);
        wc->ScaleStep = (int)(scale2 - scalefrac) / (int)(wc->RightX-wc->LeftX);
    }
    wc->RightScale = scale2;
    
    if (scale2<scalefrac) {
        wc->SmallScale = scale2;
        wc->LargeScale = scalefrac;
    } else {
        wc->LargeScale = scale2;
        wc->SmallScale = scalefrac;
    }
    
    if (wc->WallActions & (AC_TOPTEXTURE|AC_BOTTOMTEXTURE) ) {
        offsetangle = normalangle - LeftAngle;
        if (offsetangle > ANG180) {
            offsetangle = -offsetangle;     // Force unsigned
        }
        if (offsetangle > ANG90) {
            offsetangle = ANG90;        // Clip to maximum           
        }
        scale2 = fixedMul(PointDistance, gFineSine[offsetangle >> ANGLETOFINESHIFT]);
        if (normalangle - LeftAngle < ANG180) {
            scale2 = -scale2;       // Reverse the texture anchor
        }
        wc->offset += scale2;
        wc->CenterAngle = ANG90 + gViewAngle - normalangle;
    }
}

/**********************************

    Calculate the wall scaling constants

**********************************/

void WallPrep(uint32_t LeftX, uint32_t RightX, seg_t* LineSeg, angle_t LeftAngle)
{
    viswall_t* CurWallPtr;      // Pointer to work record
    uint32_t LineFlags;         // Render flags for current line
    side_t* SidePtr;            // Pointer to line side record
    sector_t* FrontSecPtr;      // Pointer to front facing sector
    sector_t* BackSecPtr;       // Pointer to rear sector (Or empty_sector if single sided)
    uint32_t actionbits;        // Flags
    uint32_t f_ceilingpic;      // Front sector ceiling image #
    uint32_t f_lightlevel;      // Front sector light level
    Fixed f_floorheight;        // Front sector floor height - viewz
    Fixed f_ceilingheight;      // Front sector ceiling height - viewz
    uint32_t b_ceilingpic;      // Back sector ceiling image #
    uint32_t b_lightlevel;      // Back sector light level
    Fixed b_floorheight;
    Fixed b_ceilingheight;
    
    CurWallPtr = gLastWallCmd;          // Get the first wall pointer
    gLastWallCmd = CurWallPtr + 1;      // Inc my pointer
    CurWallPtr->LeftX = LeftX;          // Set the edges of the visible wall
    CurWallPtr->RightX = RightX;        // Right is inclusive!
    CurWallPtr->SegPtr = LineSeg;       // For clipping
    
    {
        line_t *LinePtr;
        LinePtr = LineSeg->linedef;                 // Get the line record
        LineFlags = LinePtr->flags;                 // Copy flags into a global
        LinePtr->flags = LineFlags | ML_MAPPED;     // Mark as seen...
    }
    
    SidePtr = LineSeg->sidedef;                             // Get the line side
    FrontSecPtr = LineSeg->frontsector;                     // Get the front sector
    f_ceilingpic = FrontSecPtr->CeilingPic;                 // Store into locals
    f_lightlevel = FrontSecPtr->lightlevel;
    f_floorheight = FrontSecPtr->floorheight - gViewZ;      // Adjust for camera z
    f_ceilingheight = FrontSecPtr->ceilingheight - gViewZ;
    
    // Set the floor and ceiling shape handles & look up animated texture numbers.
    // Note that ceiling might NOT exist!
    CurWallPtr->FloorPic = FrontSecPtr->FloorPic;
    
    if (f_ceilingpic == -1) {
        CurWallPtr->CeilingPic = 0;
    } else {
        CurWallPtr->CeilingPic = f_ceilingpic;
    }
    
    BackSecPtr = LineSeg->backsector;   // Get the back sector
    if (!BackSecPtr) {                  // Invalid?
        BackSecPtr = &gEmptySector;
    }
    
    b_ceilingpic = BackSecPtr->CeilingPic;                  // Get backsector data into locals
    b_lightlevel = BackSecPtr->lightlevel;
    b_floorheight = BackSecPtr->floorheight - gViewZ;       // Adjust for camera z
    b_ceilingheight = BackSecPtr->ceilingheight - gViewZ;
    actionbits = 0;                                         // Reset vars for future storage
    
    // Add floors and ceilings if the wall needs one
    if (f_floorheight < 0 && (                                  // Is the camera above the floor?
            (FrontSecPtr->FloorPic != BackSecPtr->FloorPic) ||  // Floor texture changed?
            (f_floorheight != b_floorheight) ||                 // Differant height?
            (f_lightlevel != b_lightlevel) ||                   // Differant light?
            (b_ceilingheight == b_floorheight)                  // No thickness line?
        )
    ) {
        CurWallPtr->floorheight = CurWallPtr->floornewheight = f_floorheight>>FIXEDTOHEIGHT;
        actionbits = (AC_ADDFLOOR|AC_NEWFLOOR); // Create floor
    }

    if ((f_ceilingpic != -1 || b_ceilingpic != -1) &&       // Normal ceiling?
        (f_ceilingheight > 0 || f_ceilingpic == -1) && (    // Camera below ceiling? Sky ceiling?
            (f_ceilingpic != b_ceilingpic) ||               // New ceiling image?
            (f_ceilingheight != b_ceilingheight) ||         // Differant ceiling height?
            (f_lightlevel != b_lightlevel) ||               // Differant ceiling light?
            (b_ceilingheight == b_floorheight)              // Thin dividing line?
        )
    ) {
        CurWallPtr->ceilingheight = CurWallPtr->ceilingnewheight = f_ceilingheight >>FIXEDTOHEIGHT;
        
        if (f_ceilingpic == -1) {
            actionbits |= AC_ADDSKY | AC_NEWCEILING;        // Add sky to the ceiling
        } else {
            actionbits |= AC_ADDCEILING | AC_NEWCEILING;    // Add ceiling texture
        }
    }
    
    CurWallPtr->t_topheight = f_ceilingheight>>FIXEDTOHEIGHT;   // Y coord of the top texture

    // Single sided line? They only have a center texture
    if (BackSecPtr == &gEmptySector) {  // Bogus back sector?
        CurWallPtr->t_texture = getWallAnimTexture(SidePtr->midtexture);
        int t_texturemid;
        
        if (LineFlags & ML_DONTPEGBOTTOM) {     // Bottom of texture at bottom
            t_texturemid = f_floorheight + (CurWallPtr->t_texture->height << FRACBITS);
        } else {
            t_texturemid = f_ceilingheight;     // Top of texture at top
        }
        
        t_texturemid += SidePtr->rowoffset;                         // Add texture anchor offset
        CurWallPtr->t_texturemid = t_texturemid;                    // Save the top texture anchor var
        CurWallPtr->t_bottomheight = f_floorheight>>FIXEDTOHEIGHT;
        actionbits |= (AC_TOPTEXTURE | AC_SOLIDSIL);                // Draw the middle texture only
    } else {
        // Two sided lines are more tricky since I may be able to see through it.
        if (b_floorheight > f_floorheight) {    // Is the bottom wall texture visible?
            // Draw the bottom texture
            CurWallPtr->b_texture = getWallAnimTexture(SidePtr->bottomtexture);
            int b_texturemid;
            
            if (LineFlags & ML_DONTPEGBOTTOM) {
                b_texturemid = f_ceilingheight;     // bottom of texture at bottom
            } else {
                b_texturemid = b_floorheight;       // Top of texture at top
            }
            
            b_texturemid += SidePtr->rowoffset;     // Add the adjustment

            CurWallPtr->b_texturemid = b_texturemid;
            CurWallPtr->b_topheight = CurWallPtr->floornewheight = b_floorheight>>FIXEDTOHEIGHT;
            CurWallPtr->b_bottomheight = f_floorheight>>FIXEDTOHEIGHT;
            actionbits |= AC_NEWFLOOR|AC_BOTTOMTEXTURE; // Generate a floor and bottom wall texture
        }


        if (b_ceilingheight < f_ceilingheight && (f_ceilingpic != -1 || b_ceilingpic != -1)) {  // Ceiling wall without sky
            // Draw the top texture
            CurWallPtr->t_texture = getWallAnimTexture(SidePtr->toptexture);
            int t_texturemid;
            
            if (LineFlags & ML_DONTPEGTOP) {
                t_texturemid = f_ceilingheight; // top of texture at top
            } else {
                t_texturemid = b_ceilingheight + (CurWallPtr->t_texture->height<<FRACBITS);
            }
            
            t_texturemid += SidePtr->rowoffset;             // Anchor the top texture
            CurWallPtr->t_texturemid = t_texturemid;        // Save the top texture anchor var
            CurWallPtr->t_bottomheight = CurWallPtr->ceilingnewheight = b_ceilingheight>>FIXEDTOHEIGHT;
            actionbits |= AC_NEWCEILING | AC_TOPTEXTURE;    // Generate the top texture
        }
        
        // Check if this wall is solid (This is for sprite clipping)
        if (b_floorheight >= f_ceilingheight || b_ceilingheight <= f_floorheight) {
            actionbits |= AC_SOLIDSIL;      // This is solid (For sprite masking)
        } else {
            int width = (RightX-LeftX+1);   // Get width of opening
            
            if ((b_floorheight > 0 && b_floorheight > f_floorheight) ||
                (f_floorheight < 0 && f_floorheight > b_floorheight)
            ) {
                actionbits |= AC_BOTTOMSIL;     // There is a mask on the bottom
                CurWallPtr->BottomSil = gLastOpening - LeftX;
                gLastOpening += width;
            }
            
            if (f_ceilingpic != -1 || b_ceilingpic != -1) {                             // Only if no sky
                if ((b_ceilingheight <= 0 && b_ceilingheight < f_ceilingheight) ||
                    (f_ceilingheight > 0 && b_ceilingheight > f_ceilingheight)          // Top sil?
                ) {
                    actionbits |= AC_TOPSIL;    // There is a mask on the bottom
                    CurWallPtr->TopSil = gLastOpening - LeftX;
                    gLastOpening += width;
                }
            }
        }
    }
    
    CurWallPtr->WallActions = actionbits;   // Save the action bits
    
    if (f_lightlevel < 240) {               // Get the light level
        f_lightlevel += gExtraLight;        // Add the light factor
        
        if (f_lightlevel > 240) {
            f_lightlevel = 240;
        }
    }
    
    CurWallPtr->seglightlevel = f_lightlevel;                       // Save the light level
    CurWallPtr->offset = SidePtr->textureoffset + LineSeg->offset;  // Texture anchor X
    LatePrep(CurWallPtr, LineSeg, LeftAngle);
}

END_NAMESPACE(Renderer)