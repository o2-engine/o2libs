# o2libs - optional modules on top of the o2 engine.
#
# Attached to a game project next to o2:
#
#     add_subdirectory(o2)
#     set(O2LIBS_REMOTE_CONFIG ON CACHE BOOL "")
#     add_subdirectory(o2libs)
#     ...
#     o2libs_link(GameLib GameLibCodegen)
#
# Every module is OFF unless the project turns it on; a module pulls the modules it depends on.
# See README.md for the whole contract.

cmake_minimum_required(VERSION 3.20)

if(NOT TARGET o2Framework)
    message(FATAL_ERROR "o2libs: add_subdirectory(o2) must come before add_subdirectory(o2libs)")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/o2libs.cmake)

# The o2 checkout the modules are generated against; a project with o2 elsewhere sets it itself
if(NOT O2LIBS_O2_DIR)
    set(O2LIBS_O2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../o2")
endif()

# Modules, dependencies first. NAME is the folder in Modules/ and the suffix of the option:
#   RemoteConfig -> O2LIBS_REMOTE_CONFIG, target o2libsRemoteConfig
o2libs_declare_module(Core          OPTION O2LIBS_CORE          DESCRIPTION "Player identity, key-value storage, service settings")
o2libs_declare_module(RemoteConfig  OPTION O2LIBS_REMOTE_CONFIG DESCRIPTION "Remote configs and A/B tests client" DEPENDS Core)
o2libs_declare_module(Saves         OPTION O2LIBS_SAVES         DESCRIPTION "Player saves: modular, local, synchronized with the service" DEPENDS Core)

o2libs_finalize()
