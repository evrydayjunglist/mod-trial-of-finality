cmake_minimum_required(VERSION 3.5)

# Define the module name
set(MODULE_NAME "mod_trial_of_finality")

# Define the source files
set(MODULE_SOURCES
    src/mod_trial_of_finality.cpp
)

# Build your module as a shared library
add_library(${MODULE_NAME} MODULE ${MODULE_SOURCES})

# Include AzerothCore directories
target_include_directories(${MODULE_NAME} PRIVATE ${CMAKE_SOURCE_DIR})

# Link against AzerothCore targets
target_link_libraries(${MODULE_NAME} PRIVATE game shared)

# Set output directories for the built module
set_target_properties(${MODULE_NAME} PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/modules
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/modules
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/modules
)