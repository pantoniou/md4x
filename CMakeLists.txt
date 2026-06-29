cmake_minimum_required(VERSION 3.16)

# --- Version (parsed from build.zig.zon) ---

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/build.zig.zon" _md4x_zon)
string(REGEX MATCH "\\.version[ \t]*=[ \t]*\"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"" _ "${_md4x_zon}")
set(MD4X_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(MD4X_VERSION_MINOR "${CMAKE_MATCH_2}")
set(MD4X_VERSION_PATCH "${CMAKE_MATCH_3}")

project(md4x
    VERSION ${MD4X_VERSION_MAJOR}.${MD4X_VERSION_MINOR}.${MD4X_VERSION_PATCH}
    LANGUAGES C)

# --- Dependencies ---

# YAML parser for AST/meta/html renderer frontmatter.
find_package(libfyaml CONFIG REQUIRED)

if(TARGET libfyaml::libfyaml)
    set(MD4X_FYAML libfyaml::libfyaml)
elseif(TARGET fyaml::fyaml)
    set(MD4X_FYAML fyaml::fyaml)
else()
    message(FATAL_ERROR "libfyaml found but no imported target (libfyaml::libfyaml) was exported")
endif()

# --- Common settings ---

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(MD4X_WARN_FLAGS -Wall -Wextra -Wshadow -Wdeclaration-after-statement)

set(MD4X_VERSION_DEFS
    MD_VERSION_MAJOR=${MD4X_VERSION_MAJOR}
    MD_VERSION_MINOR=${MD4X_VERSION_MINOR}
    MD_VERSION_RELEASE=${MD4X_VERSION_PATCH})

set(MD4X_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderers")

# Apply shared compile settings to an md4x target.
function(md4x_configure tgt)
    target_compile_definitions(${tgt} PRIVATE ${MD4X_VERSION_DEFS})
    target_compile_options(${tgt} PRIVATE ${MD4X_WARN_FLAGS})
    target_include_directories(${tgt} PUBLIC ${MD4X_INCLUDE_DIRS})
endfunction()

# --- Core parser library ---

add_library(md4x STATIC
    src/md4x.c
    src/entity.c)
md4x_configure(md4x)
target_compile_definitions(md4x PRIVATE MD4X_USE_UTF8)

# --- Renderer libraries (link against the parser) ---

# Renderers that parse YAML frontmatter need libfyaml.
add_library(md4x-html STATIC src/renderers/md4x-html.c)
md4x_configure(md4x-html)
target_link_libraries(md4x-html PUBLIC md4x ${MD4X_FYAML})

add_library(md4x-ast STATIC src/renderers/md4x-ast.c)
md4x_configure(md4x-ast)
target_link_libraries(md4x-ast PUBLIC md4x ${MD4X_FYAML})

add_library(md4x-meta STATIC src/renderers/md4x-meta.c)
md4x_configure(md4x-meta)
target_link_libraries(md4x-meta PUBLIC md4x ${MD4X_FYAML})

# Renderers without YAML dependency.
add_library(md4x-ansi STATIC src/renderers/md4x-ansi.c)
md4x_configure(md4x-ansi)
target_link_libraries(md4x-ansi PUBLIC md4x)

add_library(md4x-text STATIC src/renderers/md4x-text.c)
md4x_configure(md4x-text)
target_link_libraries(md4x-text PUBLIC md4x)

add_library(md4x-markdown STATIC src/renderers/md4x-markdown.c)
md4x_configure(md4x-markdown)
target_link_libraries(md4x-markdown PUBLIC md4x)

# Heal utility (standalone, no parser dependency).
add_library(md4x-heal STATIC src/renderers/md4x-heal.c)
md4x_configure(md4x-heal)

# --- CLI executable ---

add_executable(md4x-cli
    src/cli/md4x-cli.c
    src/cli/cmdline.c)
set_target_properties(md4x-cli PROPERTIES OUTPUT_NAME md4x)
md4x_configure(md4x-cli)
target_link_libraries(md4x-cli PRIVATE
    md4x-html
    md4x-ast
    md4x-ansi
    md4x-meta
    md4x-text
    md4x-markdown
    md4x-heal
    ${MD4X_FYAML})

# --- Tests (ctest) ---

option(MD4X_BUILD_TESTS "Build/register the md4x test suites with ctest" ON)

if(MD4X_BUILD_TESTS)
    enable_testing()

    find_program(PYTHON3_EXECUTABLE NAMES python3 python)

    if(PYTHON3_EXECUTABLE)
        set(MD4X_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test")

        # One ctest entry per spec/regression/coverage suite (test/*.txt).
        file(GLOB MD4X_TEST_SUITES RELATIVE "${MD4X_TEST_DIR}" "${MD4X_TEST_DIR}/*.txt")
        list(SORT MD4X_TEST_SUITES)

        foreach(suite ${MD4X_TEST_SUITES})
            add_test(
                NAME ${suite}
                WORKING_DIRECTORY "${MD4X_TEST_DIR}"
                COMMAND ${PYTHON3_EXECUTABLE} run-testsuite.py
                        -s "${suite}" -p "$<TARGET_FILE:md4x-cli>")
        endforeach()

        # Pathological / DoS-resistance stress tests.
        add_test(
            NAME pathological
            WORKING_DIRECTORY "${MD4X_TEST_DIR}"
            COMMAND ${PYTHON3_EXECUTABLE} pathological-tests.py
                    -p "$<TARGET_FILE:md4x-cli>")
    else()
        message(WARNING "python3 not found; md4x test suites will not be registered with ctest")
    endif()
endif()

# --- Install ---

install(TARGETS
    md4x md4x-html md4x-ast md4x-ansi md4x-meta md4x-text md4x-markdown md4x-heal
    ARCHIVE DESTINATION lib)
install(TARGETS md4x-cli RUNTIME DESTINATION bin)
install(FILES
    src/md4x.h
    src/renderers/md4x-html.h
    src/renderers/md4x-ast.h
    src/renderers/md4x-ansi.h
    src/renderers/md4x-meta.h
    src/renderers/md4x-text.h
    src/renderers/md4x-markdown.h
    src/renderers/md4x-heal.h
    DESTINATION include)
