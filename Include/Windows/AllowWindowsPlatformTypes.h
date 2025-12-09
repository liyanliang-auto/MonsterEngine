/**
 * AllowWindowsPlatformTypes.h
 * 
 * Allows Windows platform types to be used without conflicts.
 * Push macro state before including Windows headers.
 * Reference: UE5 Windows/AllowWindowsPlatformTypes.h
 */

#pragma once

// Save current macro definitions
#pragma push_macro("TEXT")
#pragma push_macro("TRUE")
#pragma push_macro("FALSE")
#pragma push_macro("BOOL")

// Undefine conflicting macros
#undef TEXT
#undef TRUE
#undef FALSE
#undef BOOL
