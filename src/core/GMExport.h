#pragma once

#include <cstdint>

// Calling conventions and exports for GameMaker (Windows)
#if defined(_WIN32) || defined(__WIN32__)
    #define GM_EXPORT extern "C" __declspec(dllexport)
    #define GM_CALL __cdecl
#else
    #define GM_EXPORT extern "C" __attribute__((visibility("default")))
    #define GM_CALL
#endif

/**
 * @brief Status codes returned to GameMaker (double precision).
 */
namespace GMResult {
    inline constexpr double OK                          =  1.0;
    inline constexpr double ERR_GENERAL                 = -1.0;
    inline constexpr double ERR_INVALID_BUFFER          = -2.0;
    inline constexpr double ERR_NOT_INITIALIZED         = -3.0;
    inline constexpr double ERR_ALREADY_INITIALIZED     = -4.0;
    inline constexpr double ERR_INVALID_ARGUMENT        = -5.0;
    inline constexpr double ERR_BUFFER_OVERFLOW         = -6.0;
}
