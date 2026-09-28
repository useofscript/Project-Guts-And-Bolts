# gb_embed(<file> <variable> <out header>): bake a file into the program as a
# byte array, so it's there on every platform (phones have no loose files).
# Re-runs automatically when the file changes.
function(gb_embed file var out)
    file(READ "${file}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])" "\\1\n" hex "${hex}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
    set(text "#pragma once\n// Made by cmake/Embed.cmake from ${file}. Don't edit: edit that file instead.\n#include <cstddef>\ninline const unsigned char ${var}[] = {\n${hex}0x00};\ninline constexpr std::size_t ${var}Size = sizeof(${var}) - 1;\n")
    if(EXISTS "${out}")
        file(READ "${out}" old)
    endif()
    if(NOT "${old}" STREQUAL "${text}")
        file(WRITE "${out}" "${text}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
endfunction()
