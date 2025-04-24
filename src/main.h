#pragma once

// Standard library headers
#include <iostream>
#include <signal.h>

// Platform-specific headers
#ifdef _WIN32
#include <WinSock2.h>
#endif

// Project headers
#include "application.h"
#include "logger.h"

// Function declarations
void signalHandler(int sig);