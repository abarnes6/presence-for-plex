#include "uuid.h"
#include <random>
#include <sstream>
#include <iomanip>

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#include <unistd.h>
#include <sys/time.h>
#endif

namespace uuid
{
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<unsigned int> dis(0, 15);
    static thread_local std::uniform_int_distribution<unsigned int> dis2(8, 11);

    std::string generate_uuid_v4()
    {
        std::stringstream ss;
        ss << std::hex;

        // First group (8 chars)
        for (int i = 0; i < 8; i++)
        {
            ss << dis(gen);
        }
        ss << "-";

        // Second group (4 chars)
        for (int i = 0; i < 4; i++)
        {
            ss << dis(gen);
        }
        ss << "-";

        // Third group (4 chars)
        ss << "4";
        for (int i = 0; i < 3; i++)
        {
            ss << dis(gen);
        }
        ss << "-";

        // Fourth group (4 chars)
        ss << dis2(gen);
        for (int i = 0; i < 3; i++)
        {
            ss << dis(gen);
        }
        ss << "-";

        // Fifth group (12 chars)
        for (int i = 0; i < 12; i++)
        {
            ss << dis(gen);
        }

        return ss.str();
    }
}