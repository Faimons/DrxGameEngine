# Converts a binary file into a C++ header: cmake -DINPUT=... -DOUTPUT=... -DSYMBOL=... -P DRXEmbedFile.cmake
file(READ ${INPUT} content HEX)
string(LENGTH "${content}" hex_length)
math(EXPR byte_count "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
string(REGEX REPLACE "(0x[0-9a-f][0-9a-f],){32}" "\\0\n    " bytes "${bytes}")
file(WRITE ${OUTPUT}
"// Generated from ${INPUT} - do not edit\n#pragma once\n\n#include <cstddef>\n\ninline constexpr unsigned char ${SYMBOL}[] = {\n    ${bytes}\n};\ninline constexpr std::size_t ${SYMBOL}_size = ${byte_count};\n")
