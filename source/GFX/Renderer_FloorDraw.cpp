#include "Renderer_Internal.h"

#include "Base/Endian.h"
#include "Base/FMath.h"
#include "Base/Tables.h"
#include "Blit.h"
#include "Game/Data.h"
#include "Textures.h"
#include "Video.h"

BEGIN_NAMESPACE(Renderer)

static uint32_t gSpanStart[MAXSCREENHEIGHT];

//----------------------------------------------------------------------------------------------------------------------
// Draws a horizontal span of the floor
//----------------------------------------------------------------------------------------------------------------------
static void drawFloorColumn(
    const uint32_t colY,
    const uint32_t colX,
    const uint32_t numPixels,
    const float texXFrac,
    const float texYFrac,
    const float texXStep,
    const float texYStep,
    const float lightMultiplier,
    const ImageData& texData
) noexcept {
    Blit::blitColumn<
        Blit::BCF_HORZ_COLUMN |     // Column is horizontal rather than vertical
        Blit::BCF_ROW_MAJOR_IMG |   // Floor textures are stored in row major format (unlike sprites and walls)
        Blit::BCF_STEP_X |
        Blit::BCF_STEP_Y |
        Blit::BCF_H_WRAP_64 |       // Floor textures are always 64x64!
        Blit::BCF_V_WRAP_64 |
        Blit::BCF_COLOR_MULT_RGB
    >(
        texData,
        texXFrac,
        texYFrac,
        0.0f,
        Video::gFrameBuffer,
        Video::SCREEN_WIDTH,
        Video::SCREEN_HEIGHT,
        colX + gScreenXOffset,
        colY + gScreenYOffset,
        numPixels,
        texXStep,
        texYStep,
        lightMultiplier,
        lightMultiplier,
        lightMultiplier
    );
}

//----------------------------------------------------------------------------------------------------------------------
// This is the basic function to draw horizontal lines quickly 
//----------------------------------------------------------------------------------------------------------------------
static void mapPlane(
    const uint32_t x2,
    const uint32_t y,
    const float planeY,
    const float planeHeight,
    const float baseXScale,
    const float baseYScale,
    const LightParams& lightParams,
    const ImageData& texData
) noexcept {
    // Start x, distance to floor column, length and angle
    const uint32_t x1 = gSpanStart[y];
    const float distance = gYSlope[y] * planeHeight;    // Get the offset for the plane height
    const float length = gDistScale[x1] * distance;
    const float angle = -getViewAngleForX(x1) + FMath::doomAngleToRadians<float>(gViewAngleBAM);

    // xfrac, yfrac, xstep, ystep
    const float xfrac = std::cos(angle) * length + FMath::doomFixed16ToFloat<float>(gViewXFrac);
    const float yfrac = planeY - std::sin(angle) * length;
    const float xstep = distance * baseXScale;
    const float ystep = distance * baseYScale;

    // Figure out the light multiplier to use and then draw the floor column
    const float lightMul = lightParams.getLightMulForDist(distance);
    drawFloorColumn(y, x1, x2 - x1, xfrac, yfrac, xstep, ystep, lightMul, texData);
}

//----------------------------------------------------------------------------------------------------------------------
// Draw a plane by scanning the open records. 
// The open records are an array of top and bottom Y's for a graphical plane.
// I traverse the array to find out the horizontal spans I need to draw; this is a bottleneck routine.
//----------------------------------------------------------------------------------------------------------------------
void drawVisPlane(
    visplane_t& plane,
    const float planeY,
    const float baseXScale,
    const float baseYScale
) noexcept {
    // Basic plane info
    const Texture* const pTex = getFlatAnimTexture((uint32_t) plane.picHandle);
    const ImageData& texData = pTex->data;
    const float planeHeight = std::abs(plane.height);
    const LightParams lightParams = getLightParams(plane.planeLight);
    
    // Start the draw loop
    const int32_t stop = plane.maxX + 1;    // Maximum x coord
    int32_t x = plane.minX;                 // Starting x

    ScreenYPair prevCol = { MAXSCREENHEIGHT - 1, 0 };       // Set posts to stop drawing
    ScreenYPair* const pPlaneCols = plane.cols;             // Init the pointer to the open Y's
    ASSERT(stop < C_ARRAY_SIZE(plane.cols));

    do {
        // Fetch the NEW top and bottom
        ASSERT((uint32_t) x < C_ARRAY_SIZE(plane.cols));
        const ScreenYPair newCol = pPlaneCols[x];         
        
        if (prevCol == newCol)
            continue;
        
        uint32_t prevTopY = prevCol.ty;         // Previous and dest Y coords for top and bottom line
        uint32_t prevBottomY = prevCol.by;
        uint32_t newTopY = newCol.ty;
        uint32_t newBottomY = newCol.by;
        
        // For lines on the top, check if the entry is going down            
        if (prevTopY < newTopY && prevTopY <= prevBottomY) {        // Valid?
            uint32_t count = prevBottomY + 1;                       // Convert to <
            if (newTopY < count) {                                  // Use the lower
                count = newTopY;                                    // This is smaller
            }

            do {
                // Draw to this x
                mapPlane(
                    x,
                    prevTopY,
                    planeY,
                    planeHeight,
                    baseXScale,
                    baseYScale,
                    lightParams,
                    texData
                );
            } while (++prevTopY < count);   // Keep counting
        }

        if (newTopY < prevTopY && newTopY <= newBottomY) {
            uint32_t count = newBottomY + 1;
            if (prevTopY < count) {
                count = prevTopY;
            }

            // Mark the starting x's
            do {
                ASSERT(newTopY < C_ARRAY_SIZE(gSpanStart));
                gSpanStart[newTopY] = x;
            } while (++newTopY < count);
        }
        
        if (prevBottomY > newBottomY && prevBottomY >= prevTopY) {
            int32_t count = (int32_t) prevTopY - 1;
            if (count < (int32_t) newBottomY) {
                count = newBottomY;
            }

            do {
                // Draw to this x
                mapPlane(
                    x,
                    prevBottomY,
                    planeY,
                    planeHeight,
                    baseXScale,
                    baseYScale,
                    lightParams,
                    texData
                );
            } while ((int32_t) --prevBottomY > count);
        }

        if (newBottomY > prevBottomY && newBottomY >= newTopY) {
            int32_t count = (int32_t) newTopY - 1;
            if (count < (int32_t) prevBottomY) {
                count = prevBottomY;
            }

            // Mark the starting x's
            do {
                ASSERT(newBottomY < C_ARRAY_SIZE(gSpanStart));
                gSpanStart[newBottomY] = x;
            } while ((int32_t) --newBottomY > count);
        }

        prevCol = newCol;
    } while (++x <= stop);
}

void drawAllVisPlanes() noexcept {
    const float viewAngle = FMath::doomAngleToRadians<float>(gViewAngleBAM);
    const float wallScaleAngle = viewAngle - FMath::ANGLE_90<float>;        // Left to right mapping

    const float baseXScale = +std::cos(wallScaleAngle) / ((float) gScreenWidth * 0.5f);
    const float baseYScale = -std::sin(wallScaleAngle) / ((float) gScreenWidth * 0.5f);
    const float planeY = FMath::doomFixed16ToFloat<float>(-gViewYFrac);     // Get the Y coord for camera

    visplane_t* pPlane = gVisPlanes + 1;
    visplane_t* const pEndPlane = gpEndVisPlane;
    
    while (pPlane < pEndPlane) {
        drawVisPlane(*pPlane, planeY, baseXScale, baseYScale);
        ++pPlane;
    }
}

void drawFlatColumn(const FlatFragment flatFrag) noexcept {
    // Cache some useful values
    const float viewX = gViewX;
    const float viewY = gViewY;
    const float viewZ = gViewZ;
    const float flatPlaneZ = flatFrag.worldZ;
    const float nearPlaneTz = gNearPlaneTz;
    const float nearPlaneZStep = gNearPlaneZStepPerViewColPixel;

    const uint16_t* const pSrcPixels = flatFrag.pImageData->pPixels;

    // The x and y coordinate in world space of the screen column being drawn.
    // Note: take the center position of the pixel to improve accuracy, hence + 0.5 here:
    const float nearPlaneX = gNearPlaneP1x + ((float) flatFrag.x + 0.5f) * gNearPlaneXStepPerViewCol;
    const float nearPlaneY = gNearPlaneP1y + ((float) flatFrag.x + 0.5f) * gNearPlaneYStepPerViewCol;

    // This is where we store the intersection of a ray going from the view to the flat plane.
    // For the first pixel in the flat column however, always use the world position of the start of the column as the 'intersection'.
    // This helps prevent artifacts/inaccuracies where sometimes we step past parts of the texture and wraparound when we shouldn't.
    // Fixes artifacts on some levels with 64x64 teleporter textures for example:
    float intersectX = flatFrag.worldX;
    float intersectY = flatFrag.worldY;
    float intersectZ = flatFrag.worldZ;

    // Where to start outputting to
    const uint32_t startScreenX = gScreenXOffset + flatFrag.x;
    const uint32_t startScreenY = gScreenYOffset + flatFrag.y;
    uint32_t* const pDstPixels = Video::gFrameBuffer + (startScreenY * Video::SCREEN_WIDTH) + startScreenX;

    // Draw the column!
    uint16_t curDstY = flatFrag.y;
    const uint16_t endDstY = flatFrag.y + flatFrag.height;
    uint32_t* pDstPixel = pDstPixels;

    while (curDstY < endDstY) {
        // Get the source pixel (RGBA5551 format).
        // Note that the flat texture is always expected to be 64x64, hence we can wraparound with a simple bitwise AND:
        const uint32_t curSrcXInt = (uint32_t) intersectX & 63;
        const uint32_t curSrcYInt = (uint32_t) intersectY & 63;
        const uint16_t srcPixelRGBA5551 = pSrcPixels[curSrcYInt * 64 + curSrcXInt];
        
        // Extract RGB components and shift such that the maximum value is 255 instead of 31.
        const uint16_t texR = (srcPixelRGBA5551 & uint16_t(0b1111100000000000)) >> 8;
        const uint16_t texG = (srcPixelRGBA5551 & uint16_t(0b0000011111000000)) >> 3;
        const uint16_t texB = (srcPixelRGBA5551 & uint16_t(0b0000000000111110)) << 2;

        // Get the texture colors in 0-255 float format.
        // Note that if we are not doing any color multiply these conversions would be redundant, but I'm guessing
        // that the compiler would be smart enough to optimize out the useless operations in those cases (hopefully)!
        float r = (float) texR;
        float g = (float) texG;
        float b = (float) texB;

        /*
        if constexpr ((BC_FLAGS & BCF_COLOR_MULT_RGB) != 0) {
            r = std::min(r * rMul, 255.0f);
            g = std::min(g * gMul, 255.0f);
            b = std::min(b * bMul, 255.0f);
        }
        */

        // Write out the pixel value
        *pDstPixel = (
            (uint32_t(r) << 24) |
            (uint32_t(g) << 16) |
            (uint32_t(b) << 8)
        );

        // Move onto the next pixel
        pDstPixel += Video::SCREEN_WIDTH;
        ++curDstY;

        // For the upcoming pixel, compute the intersection of a ray going from the camera through the near plane (screen) point.
        // Find out where this ray will hit the plane that we are drawing. This gives us a world XY coordinate which allows us
        // to decide which pixel to use. World XY also allows us to find distance from the viewer, for shading...
        {
            // Compute the near plane height value for this column pixel.
            // Note: take the center position of the pixel to improve accuracy, hence + 0.5 here:
            const float nearPlaneZ = nearPlaneTz + nearPlaneZStep * ((float) curDstY + 0.5f);

            // This is the ray direction
            const float rayDirX = nearPlaneX - gViewX;
            const float rayDirY = nearPlaneY - gViewY;
            const float rayDirZ = nearPlaneZ - gViewZ;

            // Calculate first: AXd + BYd + CZd
            // I.E - The dot product of the plane normal (0, 0, 1) with the ray direction:
            const float divisor = rayDirZ;

            // Calculate: AX0 + BY0 + CZ0 + D
            // I.E - The dot product of the ray origin with the plane normal (0, 0, 1) plus
            // the distance of the plane from the origin along it's normal:
            const float dividend = viewZ - flatPlaneZ;

            // Compute the ray intersection time: -(AX0 + BY0 + CZ0 + D) / (AXd + BYd + CZd)
            const float intersectT = -dividend / divisor;

            // Using the intersect time, compute the world intersect point
            intersectX = viewX + rayDirX * intersectT;
            intersectY = viewY + rayDirY * intersectT;
            intersectZ = viewZ + rayDirZ * intersectT;
        }
    }
}

void drawAllFloorFragments() noexcept {
    for (const FlatFragment& flatFrag : gFloorFragments) {
        drawFlatColumn(flatFrag);
    }
}

void drawAllCeilingFragments() noexcept {
    /*
    const float viewX = gViewX;
    const float viewY = gViewY;

    for (const FlatFragment& flatFrag : gCeilFragments) {
        // Get the Z distance away at which the flat fragment will meet the view plane.
        // Note that this assumes a vertical FOV of 90 degrees!
        const float clipDistFromView = std::fabsf(flatFrag.endWorldZ - gViewZ);

        // Get the distance of the flat fragment to the view
        const float fragEndX = flatFrag.endWorldX;
        const float fragEndY = flatFrag.endWorldY;
        const float viewToEndX = fragEndX - viewX;
        const float viewToEndY = fragEndY - viewY;
        const float endToViewDist = std::sqrtf(viewToEndX * viewToEndX + viewToEndY * viewToEndY);

        // Figure out the world X and Y coordinate for the flat where it intersects the view
        const float clipT = clipDistFromView / endToViewDist;
        const float clipWorldX = viewX + viewToEndX * clipT;
        const float clipWorldY = viewY + viewToEndY * clipT;

        Blit::blitColumn<
            Blit::BCF_STEP_X |
            Blit::BCF_STEP_Y |
            Blit::BCF_H_WRAP_64 |
            Blit::BCF_V_WRAP_64 |
            Blit::BCF_COLOR_MULT_RGB
        >(
            *flatFrag.pImageData,
            flatFrag.endWorldX,
            flatFrag.endWorldY,
            0.0f,
            Video::gFrameBuffer,
            Video::SCREEN_WIDTH,
            Video::SCREEN_HEIGHT,
            flatFrag.x + gScreenXOffset,
            flatFrag.y + gScreenYOffset,
            flatFrag.height,
            viewToEndX / flatFrag.height,   // TODO: X STEP
            viewToEndY / flatFrag.height,   // TODO: Y STEP
            1.0f,   // TODO
            1.0f,   // TODO
            1.0f    // TODO
        );
    }
    */
}

END_NAMESPACE(Renderer)
