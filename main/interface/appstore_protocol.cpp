#include "appstore_protocol.hpp"

namespace appstore {

std::string tsv_unescape(const std::string &value)
{
    std::string output;
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\\' && index + 1 < value.size()) {
            const char next = value[++index];
            if (next == 't') output += '\t';
            else if (next == 'n') output += '\n';
            else if (next == 'r') output += '\r';
            else output += next;
        } else {
            output += value[index];
        }
    }
    return output;
}

std::vector<std::string> split_tab(const std::string &line)
{
    std::vector<std::string> fields;
    std::string current;
    for (char ch : line) {
        if (ch == '\t') {
            fields.push_back(tsv_unescape(current));
            current.clear();
        } else {
            current += ch;
        }
    }
    fields.push_back(tsv_unescape(current));
    return fields;
}

} // namespace appstore
