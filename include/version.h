#pragma once

// Define version components
#define VERSION_MAJOR 0
#define VERSION_MINOR 3
#define VERSION_PATCH 3

// Helper macros for string conversion
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Version as string in format "MAJOR.MINOR.PATCH"
#define VERSION_STRING TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH)

// Version as numeric value (10000*MAJOR + 100*MINOR + PATCH)
#define VERSION_NUM ((VERSION_MAJOR * 10000) + (VERSION_MINOR * 100) + VERSION_PATCH)
