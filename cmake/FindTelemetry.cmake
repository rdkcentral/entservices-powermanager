# - Try to find Telemetry library
# Once done this will define
#  TELEMETRY_FOUND - System has Telemetry
#  TELEMETRY_LIBRARIES - The libraries needed to use Telemetry
#  TELEMETRY_INCLUDE_DIRS - The headers needed to use Telemetry

find_package(PkgConfig)

find_library(TELEMETRY_LIBRARIES NAMES telemetry_msgsender)
find_path(TELEMETRY_INCLUDE_DIRS NAMES telemetry_busmessage_sender.h)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Telemetry DEFAULT_MSG TELEMETRY_INCLUDE_DIRS TELEMETRY_LIBRARIES)

mark_as_advanced(
    TELEMETRY_FOUND
    TELEMETRY_INCLUDE_DIRS
    TELEMETRY_LIBRARIES)
