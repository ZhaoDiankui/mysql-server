# Copyright (c) 2024, ApeCloud Inc Holding Limited.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is also distributed with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have included with MySQL.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

# cmake -DWITH_OBJSTORE=system|bundled
# bundled is the default

SET(OBJSTORE_INCLUDE_DIR "system default header")
SET(OBJSTORE_LIBRARY_PATH "system default lib path")
SET(OBJSTORE_LIBRARIES "aws-cpp-sdk-s3;aws-cpp-sdk-core")
SET(OBJSTORE_PLATFORM_DEPS "pthread;curl")

MACRO(SHOW_OBJSTORE_INFO)
  MESSAGE(STATUS "OBJSTORE_INCLUDE_DIR: ${OBJSTORE_INCLUDE_DIR}")
  MESSAGE(STATUS "OBJSTORE_LIBRARY_PATH: ${OBJSTORE_LIBRARY_PATH}")
  MESSAGE(STATUS "OBJSTORE_LIBRARIES: ${OBJSTORE_LIBRARIES}")
  MESSAGE(STATUS "OBJSTORE_PLATFORM_DEPS: ${OBJSTORE_PLATFORM_DEPS}")
ENDMACRO()

MACRO(PREPARE_BUNDLED_OJBSTORE)
  SET(OBJSTORE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/extra/aws-sdk-cpp")

  set(BUILD_ONLY "s3;core" CACHE STRING "AWS sdk components to build")
  set(ENABLE_TESTING OFF  CACHE BOOL "AWS sdk building unit and integration tests")
  set(AUTORUN_UNIT_TESTS OFF CACHE BOOL "AWS sdk auto run unittests")
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build aws sdk static library")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "AWS sdk build type")

  add_subdirectory(${PROJECT_SOURCE_DIR}/extra/aws-sdk-cpp)
  # compilation result installation is later phase, mkdir include path in advance avoid compile error.
  FILE(MAKE_DIRECTORY "${OBJSTORE_INSTALL_PREFIX}/include")
ENDMACRO()

MACRO(FIND_SYSTEM_OBJSTORE)
  FIND_PACKAGE(AWSSDK REQUIRED COMPONENTS "s3")
ENDMACRO()

MACRO (MYSQL_CHECK_OBJSTORE_S3)
  IF(NOT WITH_OBJSTOR)
    SET(WITH_OBJSTOR "bundled" CACHE STRING "By default use bundled ObjStore library")
  ENDIF()

  IF(WITH_OBJSTOR STREQUAL "bundled")
    MESSAGE(STATUS "WITH_OBJSTOR is bundled, download aws-sdk-cpp and compile it")
    PREPARE_BUNDLED_OJBSTORE()
    MESSAGE(STATUS "aws-sdk-cpp will be installed to ${OBJSTORE_INSTALL_PREFIX} in the compile phase")

    # Set the variables for the project
    SET(OBJSTORE_INCLUDE_DIR "${OBJSTORE_INSTALL_PREFIX}/include")
    SET(OBJSTORE_LIBRARY_PATH "${OBJSTORE_INSTALL_PREFIX}/lib64")
    SET(OBJSTORE_LIBRARY ${OBJSTORE_LIBRARIES})
    SET(OBJSTORE_PLATFORM_DEPS ${OBJSTORE_PLATFORM_DEPS})
    # Prepare include and ld path
    INCLUDE_DIRECTORIES(${OBJSTORE_INCLUDE_DIR})
    LINK_DIRECTORIES(${OBJSTORE_LIBRARY_PATH})
  ELSEIF(WITH_OBJSTOR STREQUAL "system")
    MESSAGE(STATUS "WITH_OBJSTOR is system, use system aws s3 lib")
    FIND_SYSTEM_OBJSTORE()
    SET(OBJSTORE_LIBRARY ${AWSSDK_LINK_LIBRARIES})
    SET(OBJSTORE_PLATFORM_DEPS ${OBJSTORE_PLATFORM_DEPS})
  ELSE()
    MESSAGE(FATAL_ERROR "WITH_OBJSTOR must be bundled or system")
  ENDIF()

  SHOW_OBJSTORE_INFO()
ENDMACRO()
