cmake_minimum_required(VERSION 3.22)

# Set the path to the Plugin SDK.
if(NOT "${METAMODULE_SDK_DIR}" STREQUAL "")
	message("METAMODULE_SDK_DIR set by CMake variable ${METAMODULE_SDK_DIR}")
elseif (DEFINED ENV{METAMODULE_SDK_DIR})
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
	message("METAMODULE_SDK_DIR set by environment variable ${METAMODULE_SDK_DIR}")
else()
    set(METAMODULE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../metamodule-plugin-sdk")
	message("METAMODULE_SDK_DIR set to default: ${METAMODULE_SDK_DIR}")
endif()

include(${METAMODULE_SDK_DIR}/plugin.cmake)


project(Stoermelder-P1-Meta
    VERSION 0.1 
    DESCRIPTION "Stoermelder PackOne for MetaModule" 
    LANGUAGES C CXX ASM
)

add_library(Stoermelder-P1 STATIC)

# Add sources from the Rack source repo
set(SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../src)

target_sources(Stoermelder-P1
    PRIVATE
    ${SOURCE_DIR}/plugin.cpp
    ${SOURCE_DIR}/pluginsettings.cpp
    ${SOURCE_DIR}/Bolt.cpp
    ${SOURCE_DIR}/FourRounds.cpp
    ${SOURCE_DIR}/Hive.cpp
    ${SOURCE_DIR}/Maze.cpp
    ${SOURCE_DIR}/MidiStep.cpp
    ${SOURCE_DIR}/Pile.cpp
    ${SOURCE_DIR}/Orbit.cpp
    ${SOURCE_DIR}/Raw.cpp
)

# Add includes and compile options for source repo
target_include_directories(Stoermelder-P1
    PRIVATE
    ${SOURCE_DIR}
)

# Call this to link and create the plugin file
create_plugin(
    SOURCE_LIB      Stoermelder-P1                                  # The cmake target name (defined in add_target)
    PLUGIN_NAME     Stoermelder-P1                                  # This must match the brand "slug" used in VCV Rack
    PLUGIN_JSON     ${SOURCE_DIR}/../plugin.json                    # Path to the plugin.json file used by VCV Rack
    SOURCE_ASSETS   ${CMAKE_CURRENT_LIST_DIR}/assets                # Path to the assets/ dir containing the PNGs
    DESTINATION     ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins    # Path to where you want the plugin file output
)