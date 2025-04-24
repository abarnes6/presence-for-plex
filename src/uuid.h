#pragma once

// Standard library headers
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

// Platform-specific headers
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#include <sys/time.h>
#include <unistd.h>
#endif

namespace uuid
{
    /**
     * Generates a random UUID (Universally Unique Identifier) version 4.
     *
     * UUID v4 format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
     * where x is any hexadecimal digit and y is one of 8, 9, A, or B.
     *
     * @return A string containing the generated UUID v4.
     */
    std::string generate_uuid_v4();
}