/**
 * HideWindowsPlatformTypes.h
 * 
 * Restores macro state after including Windows headers.
 * Pop macro state to restore engine definitions.
 * Reference: UE5 Windows/HideWindowsPlatformTypes.h
 */

#pragma once

// Restore saved macro definitions
#pragma pop_macro("TEXT")
#pragma pop_macro("TRUE")
#pragma pop_macro("FALSE")
#pragma pop_macro("BOOL")
