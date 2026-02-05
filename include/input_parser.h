#ifndef INPUT_PARSER_H
#define INPUT_PARSER_H

#include "types.h"
#include <string>

class InputParser {
public:
    static InputParams parse(const std::string& filename);
};

#endif // INPUT_PARSER_H
