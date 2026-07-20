# **********************************************************************
# * Copyright (C) 2018-2025 MX Authors
# *
# * Authors: Adrian
# *          MX Linux <http://mxlinux.org>
# *
# * This file is part of mx-cleanup.
# *
# * mx-cleanup is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# *
# * mx-cleanup is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with mx-cleanup.  If not, see <http://www.gnu.org/licenses/>.
# **********************************************************************/

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VERSION_FROM_CHANGELOG "")
set(DPKG_RESULT 1)

if(DEFINED PROJECT_VERSION_OVERRIDE AND NOT "${PROJECT_VERSION_OVERRIDE}" STREQUAL "")
    set(PROJECT_VERSION_FROM_CHANGELOG "${PROJECT_VERSION_OVERRIDE}")
    message(STATUS "Using version override: ${PROJECT_VERSION_FROM_CHANGELOG}")
else()
    find_program(DPKG_PARSECHANGELOG dpkg-parsechangelog)
    if(DPKG_PARSECHANGELOG)
        execute_process(
            COMMAND ${DPKG_PARSECHANGELOG} -SVersion
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE PROJECT_VERSION_FROM_CHANGELOG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE DPKG_RESULT
            ERROR_QUIET
        )
    endif()

    if(DPKG_RESULT EQUAL 0 AND NOT "${PROJECT_VERSION_FROM_CHANGELOG}" STREQUAL "")
        message(STATUS "Using version from debian/changelog: ${PROJECT_VERSION_FROM_CHANGELOG}")
    elseif(EXISTS ${CMAKE_SOURCE_DIR}/debian/changelog)
        file(READ ${CMAKE_SOURCE_DIR}/debian/changelog PROJECT_CHANGELOG_CONTENT)
        string(REGEX MATCH "^[^\\(]*\\(([^)]+)\\)" _match "${PROJECT_CHANGELOG_CONTENT}")
        if(NOT _match)
            message(FATAL_ERROR "Could not parse version from debian/changelog")
        endif()
        set(PROJECT_VERSION_FROM_CHANGELOG "${CMAKE_MATCH_1}")
        message(STATUS "Using version parsed from debian/changelog: ${PROJECT_VERSION_FROM_CHANGELOG}")
    elseif(EXISTS ${CMAKE_SOURCE_DIR}/PKGBUILD)
        file(READ ${CMAKE_SOURCE_DIR}/PKGBUILD PKGBUILD_CONTENT)
        string(REGEX MATCH "\n?pkgver=([^\n\r]+)" _match "${PKGBUILD_CONTENT}")
        if(NOT _match)
            message(FATAL_ERROR "Could not parse version from PKGBUILD")
        endif()
        set(PROJECT_VERSION_FROM_CHANGELOG "${CMAKE_MATCH_1}")
        string(STRIP "${PROJECT_VERSION_FROM_CHANGELOG}" PROJECT_VERSION_FROM_CHANGELOG)
        string(REGEX REPLACE "^\"|\"$" "" PROJECT_VERSION_FROM_CHANGELOG "${PROJECT_VERSION_FROM_CHANGELOG}")
        string(REGEX REPLACE "^'|'$" "" PROJECT_VERSION_FROM_CHANGELOG "${PROJECT_VERSION_FROM_CHANGELOG}")
        string(REGEX MATCH "^\\$\\{PKGVER:-([^}]+)\\}$" _pkgver_match "${PROJECT_VERSION_FROM_CHANGELOG}")
        if(_pkgver_match)
            set(PROJECT_VERSION_FROM_CHANGELOG "${CMAKE_MATCH_1}")
        endif()
        message(STATUS "Using version parsed from PKGBUILD: ${PROJECT_VERSION_FROM_CHANGELOG}")
    else()
        message(FATAL_ERROR "Could not determine version: dpkg-parsechangelog not available and no debian/changelog or PKGBUILD")
    endif()
endif()

project(mx-cleanup
    VERSION ${PROJECT_VERSION_FROM_CHANGELOG}
    DESCRIPTION "MX Cleanup - GUI for system cleanup and maintenance"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compile commands export for IDEs and tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimize for Ninja builds
if(CMAKE_GENERATOR STREQUAL "Ninja")
    # Enable colored output for Ninja
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fdiagnostics-color=always)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcolor-diagnostics)
    endif()
endif()

# Option to use clang for testing builds
option(USE_CLANG "Use clang compiler" OFF)
if(USE_CLANG)
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
    set(CMAKE_CXX_COMPILER_ID "Clang")
    message(STATUS "Using clang compiler")
endif()

# Option to build tests
option(BUILD_TESTS "Build tests" OFF)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    LinguistTools
)

# Enable automatic MOC, UIC, and RCC processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Absolute directory where the privileged helper binaries are installed.
set(HELPER_INSTALL_DIR "/usr/libexec/mx-cleanup" CACHE PATH
    "Absolute install directory for the privileged helper binaries")
if(NOT IS_ABSOLUTE "${HELPER_INSTALL_DIR}")
    message(FATAL_ERROR "HELPER_INSTALL_DIR must be an absolute path, got: ${HELPER_INSTALL_DIR}")
endif()
set(HELPER_PATH "${HELPER_INSTALL_DIR}/helper")
set(HELPER_TERMINAL_PATH "${HELPER_INSTALL_DIR}/helper-terminal-keep-open")
configure_file(
    scripts/org.mxlinux.pkexec.mx-cleanup-helper.policy.in
    ${CMAKE_BINARY_DIR}/org.mxlinux.pkexec.mx-cleanup-helper.policy
    @ONLY
)

# Define source files
set(SOURCES
    main.cpp
    mainwindow.cpp
    about.cpp
    packagemanager.cpp
)

set(HELPER_SOURCES
    helper.cpp
    helperlib.cpp
    packagemanager.cpp
)

set(HEADERS
    mainwindow.h
    about.h
    common.h
    usernameutils.h
    packagemanager.h
    helperlib.h
)

set(UI_FILES
    mainwindow.ui
)

set(RESOURCE_FILES
    images.qrc
)

# Get all translation files
file(GLOB TRANSLATION_FILES "translations/*.ts")

# Create the executable
add_executable(mx-cleanup
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${RESOURCE_FILES}
)

add_executable(helper
    ${HELPER_SOURCES}
)

# Link Qt6 libraries
target_link_libraries(mx-cleanup
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_link_libraries(helper
    Qt6::Core
)

# Set compiler flags
target_compile_options(mx-cleanup PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
    -Werror
)

target_compile_options(helper PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
    -Werror
)

# Add compiler-specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(mx-cleanup PRIVATE -Werror=return-stack-address)
    target_compile_options(helper PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(mx-cleanup PRIVATE -Werror=return-local-addr)
    target_compile_options(helper PRIVATE -Werror=return-local-addr)
endif()

# Set compile definitions
target_compile_definitions(mx-cleanup PRIVATE
    HELPER_PATH="${HELPER_PATH}"
    HELPER_TERMINAL_PATH="${HELPER_TERMINAL_PATH}"
    QT_DEPRECATED_WARNINGS
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    VERSION="${PROJECT_VERSION}"
)

target_compile_definitions(helper PRIVATE
    QT_DEPRECATED_WARNINGS
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    VERSION="${PROJECT_VERSION}"
)

# Release-specific optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(mx-cleanup PRIVATE NDEBUG)
    target_compile_options(mx-cleanup PRIVATE -O3)

    # Add LTO - different flags for different compilers
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
        target_compile_options(mx-cleanup PRIVATE -flto=thin)
        target_link_options(mx-cleanup PRIVATE -flto=thin)
    else()
        target_compile_options(mx-cleanup PRIVATE -flto=auto)
        target_link_options(mx-cleanup PRIVATE -flto=auto)
    endif()
endif()

# Handle translations
qt6_add_translations(mx-cleanup
    TS_FILES ${TRANSLATION_FILES}
    LRELEASE_OPTIONS -compress -nounfinished -removeidentical -silent
    QM_FILES_OUTPUT_VARIABLE qm_files
)



# Set target properties
set_target_properties(mx-cleanup PROPERTIES
    OUTPUT_NAME "mx-cleanup"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

set_target_properties(helper PROPERTIES
    OUTPUT_NAME "helper"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)



# Install target (required by Debian build system)
# Other files are handled by debian/install
install(TARGETS mx-cleanup
    RUNTIME DESTINATION bin
)

install(TARGETS helper
    RUNTIME DESTINATION ${HELPER_INSTALL_DIR}
)

# Tests
if(BUILD_TESTS)
    enable_testing()

    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(test_mx-cleanup
        tests/test_utils.cpp
        mainwindow.cpp
        about.cpp
        packagemanager.cpp
        helperlib.cpp
    )

    target_link_libraries(test_mx-cleanup
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
    )

    target_compile_definitions(test_mx-cleanup PRIVATE
        HELPER_PATH="${HELPER_PATH}"
        HELPER_TERMINAL_PATH="${HELPER_TERMINAL_PATH}"
    )

    add_test(NAME test_utils COMMAND test_mx-cleanup)
endif()
