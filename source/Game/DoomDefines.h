#pragma once

#include "Base/Fixed.h"

//----------------------------------------------------------------------------------------------------------------------
// Global defines for Doom
//----------------------------------------------------------------------------------------------------------------------

// View related
static constexpr uint32_t   MAXSCREENHEIGHT = 160;                  // Maximum height allowed
static constexpr uint32_t   MAXSCREENWIDTH  = 280;                  // Maximum width allowed
static constexpr Fixed      VIEWHEIGHT      = 41 * FRACUNIT;        // Height to render from

// Gameplay/simulation related constants
static constexpr uint32_t   TICKSPERSEC     = 60;                   // The game timebase (ticks per second)
static constexpr uint32_t   MAPBLOCKSHIFT   = FRACBITS + 7;         // Shift value to convert Fixed to 128 pixel blocks
static constexpr Fixed      ONFLOORZ        = FIXED_MIN;            // Attach object to floor with this z
static constexpr Fixed      ONCEILINGZ      = FIXED_MAX;            // Attach object to ceiling with this z
static constexpr Fixed      GRAVITY         = 4 * FRACUNIT;         // Rate of fall
static constexpr Fixed      MAXMOVE         = 16 * FRACUNIT;        // Maximum velocity
static constexpr Fixed      MAXRADIUS       = 32 * FRACUNIT;        // Largest radius of any critter
static constexpr Fixed      MELEERANGE      = 70 * FRACUNIT;        // Range of hand to hand combat
static constexpr Fixed      MISSILERANGE    = 32 * 64 * FRACUNIT;   // Range of guns targeting
static constexpr Fixed      FLOATSPEED      = 8 * FRACUNIT;         // Speed an object can float vertically
static constexpr Fixed      SKULLSPEED      = 40 * FRACUNIT;        // Speed of the skull to attack

//----------------------------------------------------------------------------------------------------------------------
// Globa enums
//----------------------------------------------------------------------------------------------------------------------

// Index for a bounding box coordinate
enum {
    BOXTOP,
    BOXBOTTOM,
    BOXLEFT,
    BOXRIGHT,
    BOXCOUNT
};

//----------------------------------------------------------------------------------------------------------------------
// A utility to convert a tick count from PC Doom's original 35Hz timebase to the timebase used by this game version.
// Tries to round so the answer is as close as possible.
//----------------------------------------------------------------------------------------------------------------------
static inline constexpr uint32_t convertPcTicks(const uint32_t ticks35Hz) noexcept {
    // Get the tick count in 31.1 fixed point format by multiplying by TICKSPERSEC/35 (in 31.1 format).
    // When returning the integer answer round up if the fractional part is '.5':
    const uint32_t tickCountFixed = ((ticks35Hz * uint32_t(TICKSPERSEC)) << 2) / (uint32_t(35) << 1);
    return (tickCountFixed & 1) ? (tickCountFixed >> 1) + 1 : (tickCountFixed >> 1);
}

//----------------------------------------------------------------------------------------------------------------------
// Convert an uint32 speed defined in the PC 35Hz timebase to the 60Hz timebase used by used by this game version.
// Tries to round so the answer is as close as possible.
//----------------------------------------------------------------------------------------------------------------------
static inline constexpr uint32_t convertPcUintSpeed(const uint32_t speed35Hz) noexcept {
    // Get the tick count in 31.1 fixed point format by multiplying by 35/TICKSPERSEC (in 31.1 format).
    // When returning the integer answer round up if the fractional part is '.5':
    const uint32_t speedFixed = ((speed35Hz * uint32_t(35)) << 2) / (uint32_t(TICKSPERSEC) << 1);    
    return (speedFixed & 1) ? (speedFixed >> 1) + 1 : (speedFixed >> 1);
}