#include <random>
#include <sstream>
#include <iomanip>

namespace uuid
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::string generate_uuid_v4()
    {
        std::stringstream ss;
        int i;
        ss << std::hex << std::setfill('0');
        for (i = 0; i < 8; i++)
        {
            ss << std::setw(2) << dis(gen);
        }
        ss << "-";
        for (i = 0; i < 4; i++)
        {
            ss << std::setw(2) << dis(gen);
        }
        ss << "-4";
        for (i = 0; i < 3; i++)
        {
            ss << std::setw(2) << dis(gen);
        }
        ss << "-";
        ss << std::setw(2) << dis2(gen);
        for (i = 0; i < 3; i++)
        {
            ss << std::setw(2) << dis(gen);
        }
        ss << "-";
        for (i = 0; i < 12; i++)
        {
            ss << std::setw(2) << dis(gen);
        }
        return ss.str();
    }
}