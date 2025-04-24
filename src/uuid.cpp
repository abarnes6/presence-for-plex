#include "uuid.h"

namespace uuid
{
    namespace
    {
        // Random number generators
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        static thread_local std::uniform_int_distribution<unsigned int> hex_dist(0, 15);
        static thread_local std::uniform_int_distribution<unsigned int> variant_dist(8, 11);

        // UUID v4 format constants
        constexpr char UUID_VERSION = '4';
        constexpr char UUID_SEPARATOR = '-';

        // Group sizes in characters
        constexpr int GROUP1_SIZE = 8;
        constexpr int GROUP2_SIZE = 4;
        constexpr int GROUP3_SIZE = 4;
        constexpr int GROUP4_SIZE = 4;
        constexpr int GROUP5_SIZE = 12;

        // Helper function to generate a random hex character
        char random_hex_char()
        {
            unsigned int val = hex_dist(gen);
            return val < 10 ? '0' + val : 'a' + (val - 10);
        }

        // Helper function to generate a sequence of random hex characters
        void append_random_hex(std::stringstream &ss, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                ss << random_hex_char();
            }
        }
    }

    std::string generate_uuid_v4()
    {
        std::stringstream ss;

        // Group 1: 8 random hex characters
        append_random_hex(ss, GROUP1_SIZE);
        ss << UUID_SEPARATOR;

        // Group 2: 4 random hex characters
        append_random_hex(ss, GROUP2_SIZE);
        ss << UUID_SEPARATOR;

        // Group 3: 4 hex characters with version 4
        ss << UUID_VERSION;
        append_random_hex(ss, GROUP3_SIZE - 1);
        ss << UUID_SEPARATOR;

        // Group 4: 4 hex characters with variant (8, 9, A, or B)
        ss << static_cast<char>('8' + (variant_dist(gen) - 8));
        append_random_hex(ss, GROUP4_SIZE - 1);
        ss << UUID_SEPARATOR;

        // Group 5: 12 random hex characters
        append_random_hex(ss, GROUP5_SIZE);

        return ss.str();
    }
}