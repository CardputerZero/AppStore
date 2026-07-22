#include "appstore_text.hpp"

#include <cctype>

namespace appstore {

std::string match_key(std::string value)
{
    std::string output;
    for (char ch : value) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (!std::isspace(byte)) output += static_cast<char>(std::tolower(byte));
    }
    return output;
}

} // namespace appstore
