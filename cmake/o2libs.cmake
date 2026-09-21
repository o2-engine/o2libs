# Module machinery of o2libs. A module is a folder:
#
#   Modules/<Name>/Sources/o2libs/<Name>/...   the static library o2libs<Name>, included as "o2libs/<Name>/X.h"
#   Modules/<Name>/Tests/*.cpp                 gtest sources, built into o2libsTests
#
# and one o2libs_declare_module() line in the root CMakeLists.txt. Nothing else knows the list of
# modules: the umbrella header Sources/o2libs/o2libs.h looks at the O2LIBS_<MODULE> definitions that
# the enabled module targets publish.

set_property(GLOBAL PROPERTY O2LIBS_MODULES "")

# o2libs_declare_module(<Name> OPTION <O2LIBS_X> DESCRIPTION <text> [DEPENDS <Name>...])
function(o2libs_declare_module NAME)
    cmake_parse_arguments(M "" "OPTION;DESCRIPTION" "DEPENDS" ${ARGN})
    if(NOT M_OPTION)
        message(FATAL_ERROR "o2libs: module ${NAME} has no OPTION")
    endif()

    option(${M_OPTION} "o2libs: ${M_DESCRIPTION}" OFF)

    set_property(GLOBAL APPEND PROPERTY O2LIBS_MODULES ${NAME})
    set_property(GLOBAL PROPERTY O2LIBS_MODULE_${NAME}_OPTION ${M_OPTION})
    set_property(GLOBAL PROPERTY O2LIBS_MODULE_${NAME}_DEPENDS "${M_DEPENDS}")
endfunction()

# Marks the module and everything it depends on as enabled
function(_o2libs_enable NAME)
    get_property(_known GLOBAL PROPERTY O2LIBS_MODULES)
    if(NOT NAME IN_LIST _known)
        message(FATAL_ERROR "o2libs: unknown module ${NAME}")
    endif()

    set_property(GLOBAL PROPERTY O2LIBS_MODULE_${NAME}_ENABLED ON)

    get_property(_deps GLOBAL PROPERTY O2LIBS_MODULE_${NAME}_DEPENDS)
    foreach(_dep ${_deps})
        _o2libs_enable(${_dep})
    endforeach()
endfunction()

# Creates the library, the codegen target and the test sources of one enabled module
function(_o2libs_add_module NAME)
    get_property(_option GLOBAL PROPERTY O2LIBS_MODULE_${NAME}_OPTION)
    get_property(_deps GLOBAL PROPERTY O2LIBS_MODULE_${NAME}_DEPENDS)

    set(_target o2libs${NAME})
    set(_root "${O2LIBS_ROOT}/Modules/${NAME}")
    set(_sources_dir "${_root}/Sources")

    set(_globs "${_sources_dir}/*.h" "${_sources_dir}/*.cpp")
    if(APPLE)
        list(APPEND _globs "${_sources_dir}/*.mm")
    endif()
    file(GLOB_RECURSE _sources CONFIGURE_DEPENDS ${_globs})

    add_library(${_target} STATIC ${_sources})
    source_group(TREE ${_root} FILES ${_sources})

    target_include_directories(${_target} PUBLIC "${_sources_dir}" "${O2LIBS_ROOT}/Sources")
    target_compile_definitions(${_target} PRIVATE ${O2_COMPILE_DEFINITIONS_EXPORT})
    # What the umbrella header and the game code look at: is the module in this build
    target_compile_definitions(${_target} PUBLIC ${_option}=1)
    target_link_libraries(${_target} PUBLIC o2Framework)

    foreach(_dep ${_deps})
        target_link_libraries(${_target} PUBLIC o2libs${_dep})
    endforeach()

    if(MSVC)
        target_compile_options(${_target} PRIVATE ${O2_MSVC_MP_FLAG} "/Zc:__cplusplus" "/bigobj")
    elseif(O2_CXX_FLAGS)
        # The engine's warning set; the project applies it to itself only after this directory
        separate_arguments(_o2_flags NATIVE_COMMAND "${O2_CXX_FLAGS}")
        target_compile_options(${_target} PRIVATE ${_o2_flags})
    endif()

    set_target_properties(${_target} PROPERTIES FOLDER o2libs)

    # Reflection: the tool rewrites the META blocks, <target>.cpp and CodeToolCache.xml inside the
    # module sources (they are committed, cross builds have no host tool). Host builds only
    set(_cache "${_sources_dir}/CodeToolCache.xml")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT EMSCRIPTEN AND NOT ANDROID)
        set(_parents "${O2LIBS_O2_DIR}/Framework/Sources/o2/CodeToolCache.xml")
        foreach(_dep ${_deps})
            string(APPEND _parents " ${O2LIBS_ROOT}/Modules/${_dep}/Sources/CodeToolCache.xml")
        endforeach()

        add_custom_target(${_target}Codegen
                          COMMAND ${O2LIBS_O2_DIR}/CodeTool/Bin/o2CodeTool
                                  -project ${_target}
                                  -sources "${_sources_dir}"
                                  -parent_projects "${_parents}"
                          COMMENT "Run CodeTool: -project ${_target} -sources \"${_sources_dir}\"")
        add_dependencies(${_target}Codegen o2CodeTool o2FrameworkCodegen)
        foreach(_dep ${_deps})
            add_dependencies(${_target}Codegen o2libs${_dep}Codegen)
        endforeach()
        add_dependencies(${_target} ${_target}Codegen)
        set_target_properties(${_target}Codegen PROPERTIES FOLDER o2libs/Codegen)

        set_property(GLOBAL APPEND PROPERTY O2LIBS_CODEGEN_TARGETS ${_target}Codegen)
    endif()

    set_property(GLOBAL APPEND PROPERTY O2LIBS_TARGETS ${_target})
    set_property(GLOBAL APPEND PROPERTY O2LIBS_CACHES "${_cache}")

    file(GLOB_RECURSE _tests CONFIGURE_DEPENDS "${_root}/Tests/*.cpp" "${_root}/Tests/*.h")
    set_property(GLOBAL APPEND PROPERTY O2LIBS_TEST_SOURCES ${_tests})
endfunction()

# Resolves what is enabled and creates everything. Called once, by the root CMakeLists.txt
function(o2libs_finalize)
    set(O2LIBS_ROOT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    get_filename_component(O2LIBS_ROOT "${O2LIBS_ROOT}" ABSOLUTE)

    get_property(_modules GLOBAL PROPERTY O2LIBS_MODULES)

    foreach(_name ${_modules})
        get_property(_option GLOBAL PROPERTY O2LIBS_MODULE_${_name}_OPTION)
        if(${_option})
            _o2libs_enable(${_name})
        endif()
    endforeach()

    # The umbrella: what a project links. Empty when no module is on, so the line in the
    # project's CMakeLists.txt does not have to be conditional
    add_library(o2libs INTERFACE)
    target_include_directories(o2libs INTERFACE "${O2LIBS_ROOT}/Sources")

    set(_enabled "")
    foreach(_name ${_modules})
        get_property(_on GLOBAL PROPERTY O2LIBS_MODULE_${_name}_ENABLED)
        if(_on)
            _o2libs_add_module(${_name})
            target_link_libraries(o2libs INTERFACE o2libs${_name})
            list(APPEND _enabled ${_name})
        endif()
    endforeach()

    if(_enabled)
        string(REPLACE ";" ", " _list "${_enabled}")
        message(STATUS "o2libs modules: ${_list}")
    else()
        message(STATUS "o2libs modules: none")
    endif()

    # For the project's own codegen: "-parent_projects \"<o2 cache> ${O2LIBS_CODEGEN_CACHES}\""
    get_property(_caches GLOBAL PROPERTY O2LIBS_CACHES)
    string(REPLACE ";" " " _caches "${_caches}")
    set(O2LIBS_CODEGEN_CACHES "${_caches}" PARENT_SCOPE)
    set(O2LIBS_ENABLED_MODULES "${_enabled}" PARENT_SCOPE)

    set_property(GLOBAL PROPERTY O2LIBS_ROOT_DIR "${O2LIBS_ROOT}")
endfunction()

# o2libs_add_tests()
# One gtest executable, o2libsTests, for all the enabled modules. The project calls it from its own
# tests section, after set_target_output_directories() and BuildAssets exist
function(o2libs_add_tests)
    get_property(ROOT GLOBAL PROPERTY O2LIBS_ROOT_DIR)
    get_property(_tests GLOBAL PROPERTY O2LIBS_TEST_SOURCES)
    if(NOT _tests OR NOT O2_TESTS OR EMSCRIPTEN OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        return()
    endif()
    if(NOT TARGET o2TestsSupport OR NOT TARGET GTest::gtest)
        return()
    endif()

    add_executable(o2libsTests "${ROOT}/Tests/TestsMain.cpp" ${_tests})
    target_link_libraries(o2libsTests PRIVATE GTest::gtest o2TestsSupport o2libs)
    target_compile_definitions(o2libsTests PRIVATE ${O2_COMPILE_DEFINITIONS_EXPORT} O2LIBS_ROOT_DIR="${ROOT}")
    if(MSVC)
        target_compile_options(o2libsTests PRIVATE ${O2_MSVC_MP_FLAG} "/Zc:__cplusplus" "/bigobj")
    endif()
    set_target_properties(o2libsTests PROPERTIES FOLDER o2libs)

    # The project decides where binaries go (tests need the built assets next to them)
    if(COMMAND set_target_output_directories)
        set_target_output_directories(o2libsTests)
    endif()
    if(TARGET BuildAssets)
        add_dependencies(o2libsTests BuildAssets)
    endif()
endfunction()

# o2libs_link(<game library target> [<its codegen target>])
# Links the enabled modules into the project's library and orders the codegen passes
function(o2libs_link TARGET)
    target_link_libraries(${TARGET} PUBLIC o2libs)

    if(ARGC GREATER 1 AND TARGET ${ARGV1})
        get_property(_codegens GLOBAL PROPERTY O2LIBS_CODEGEN_TARGETS)
        foreach(_codegen ${_codegens})
            add_dependencies(${ARGV1} ${_codegen})
        endforeach()
    endif()
endfunction()
