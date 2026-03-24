cmake_minimum_required (VERSION 3.11)

project(Database VERSION 1.0)


if (POLICY CMP0141)
cmake_policy(SET CMP0141 NEW)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
# Windows
add_compile_options(/W4)
else()
# Linux/Mac
add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# if (CMAKE_VERSION VERSION_GREATER 3.12)
# set_property(TARGET CMakeTarget PROPERTY CXX_STANDARD 20)
# endif()

include_directories(include)

add_executable (
  CMakeTarget
  "src/main.cpp"
  "src/main.h"
  "src/Pager.cpp"
  "src/Pager.h"
  "src/SlottedPage.cpp"
  "src/SlottedPage.h"
  "src/BTree.h"
  "src/BTree.cpp"
  "src/Table.h"
  "src/Table.cpp"
  "src/Serializer.h"
  "src/Serializer.cpp"
)


# TODO: Add tests and install targets if needed.
