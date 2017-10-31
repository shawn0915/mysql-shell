# Copyright (c) 2015, 2017, Oracle and/or its affiliates. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

# Find the MySQL Client Libraries and related development files
#
#   MYSQL_FOUND           - TRUE if MySQL was found
#   MYSQL_INCLUDE_DIRS    - path which contains mysql.h
#   MYSQL_LIBRARIES       - libraries provided by the MySQL installation
#   MYSQL_VERSION         - version of the MySQL Client Libraries



# ----------------------------------------------------------------------
#
# Macro that runs "mysql_config ${_opt}" and return the line after
# trimming away ending space/newline.
#
# _mysql_conf(
#   _var    - output variable name, will contain a ';' separated list
#   _opt    - the flag to give to mysql_config
#
# ----------------------------------------------------------------------

if(NOT MYSQL_CONFIG_EXECUTABLE)
  set(MYSQL_CONFIG_EXECUTABLE ${MYSQL_BUILD_DIR}/scripts/mysql_config)
endif()

macro(_mysql_conf _var _opt)
  execute_process(
    COMMAND ${MYSQL_CONFIG_EXECUTABLE} ${_opt}
    OUTPUT_VARIABLE ${_var}
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endmacro()

####
####

set(MYSQL_SOURCE_DIR "../mysql" CACHE PATH "Path to MySQL 8.0 source directory")
set(MYSQL_BUILD_DIR "${MYSQL_SOURCE_DIR}/bld" CACHE PATH "Path to MySQL 8.0 build directory")

if(MYSQL_DIR)
  set(MYSQLX_INCLUDES "${MYSQL_DIR}/include")

  set(MYSQLX_LIBRARY_PATHS
    "${MYSQL_DIR}/lib"
  )
  set(MYSQL_LIBRARY_PATHS
    "${MYSQL_DIR}/lib"
  )
else()
  set(MYSQLX_INCLUDES
    "${MYSQL_BUILD_DIR}/include"
    "${MYSQL_BUILD_DIR}/rapid/plugin/x/generated"
    "${MYSQL_SOURCE_DIR}/include"
    "${MYSQL_SOURCE_DIR}/rapid/plugin/x/client"
    "${MYSQL_SOURCE_DIR}/libbinlogevents/export"
  )

  set(MYSQLX_LIBRARY_PATHS
    "${MYSQL_BUILD_DIR}/rapid/plugin/x/client"
    "${MYSQL_BUILD_DIR}/rapid/plugin/x/protocol"
  )
  set(MYSQL_LIBRARY_PATHS
    "${MYSQL_BUILD_DIR}/libmysql"
    "${MYSQL_BUILD_DIR}/libmysql/${CMAKE_BUILD_TYPE}"
  )
endif()

if(NOT WIN32)
  find_library(MYSQLX_CLIENT_LIB NAMES libmysqlxclient.a
                 PATHS ${MYSQLX_LIBRARY_PATHS} PATH_SUFFIXES mysql
                 NO_DEFAULT_PATH)
  find_library(MYSQLX_PROTO_LIB NAMES libmysqlxmessages.a
                 PATHS ${MYSQLX_LIBRARY_PATHS} PATH_SUFFIXES mysql
                 NO_DEFAULT_PATH)
  find_library(MYSQL_CLIENT_LIB NAMES libmysqlclient.a
                 PATHS ${MYSQL_LIBRARY_PATHS} PATH_SUFFIXES mysql
                 NO_DEFAULT_PATH)
  # Find the mysql_config --libs command and replace the -L.. -lmysqlclient
  # portion with the actual location we detected, but leave the rest
  # which contains other dependencies
  _mysql_conf(_mysql_config_output "--libs")
  message(STATUS "config --libs ${_mysql_config_output}")
  message(STATUS "${MYSQL_CLIENT_LIB}")
  string(REGEX REPLACE ".* -lmysqlclient *(.*)" "\\1" _mysql_config_output "${_mysql_config_output}")
  string(REGEX REPLACE "-l\([^ ]+\)" "\\1" _mysql_config_output "${_mysql_config_output}")
  separate_arguments(_mysql_config_output)
  set(MYSQL_CLIENT_LIB ${MYSQL_CLIENT_LIB} ${_mysql_config_output})
else()
  if(MYSQL_DIR)
    find_library(MYSQLX_CLIENT_LIB NAMES mysqlxclient.lib
                   PATHS ${MYSQLX_LIBRARY_PATHS}
                   NO_DEFAULT_PATH)

    find_library(MYSQLX_PROTO_LIB NAMES mysqlxmessages.lib
                   PATHS ${MYSQLX_LIBRARY_PATHS}
                   NO_DEFAULT_PATH)

    find_library(MYSQL_CLIENT_LIB NAMES mysqlclient.lib
                   PATHS ${MYSQL_LIBRARY_PATHS}
                   NO_DEFAULT_PATH)
  else()
    if(NOT CMAKE_BUILD_TYPE)
      set(CMAKE_BUILD_TYPE RelWithDebInfo)
    endif()
    find_library(MYSQLX_CLIENT_LIB NAMES mysqlxclient.lib
                   PATHS ${MYSQLX_LIBRARY_PATHS} PATH_SUFFIXES ${CMAKE_BUILD_TYPE}
                   NO_DEFAULT_PATH)

    find_library(MYSQLX_PROTO_LIB NAMES mysqlxmessages.lib
                   PATHS ${MYSQLX_LIBRARY_PATHS} PATH_SUFFIXES ${CMAKE_BUILD_TYPE}
                   NO_DEFAULT_PATH)

    find_library(MYSQL_CLIENT_LIB NAMES mysqlclient.lib
                   PATHS ${MYSQL_LIBRARY_PATHS} PATH_SUFFIXES ${CMAKE_BUILD_TYPE}
                   NO_DEFAULT_PATH)
  endif()
endif()

if(MYSQLX_INCLUDES AND MYSQLX_CLIENT_LIB AND MYSQL_CLIENT_LIB)
  set(MYSQLX_FOUND TRUE)
  set(MYSQLX_INCLUDE_DIRS ${MYSQLX_INCLUDES})
  set(MYSQLX_LIBRARIES ${MYSQLX_CLIENT_LIB} ${MYSQLX_PROTO_LIB} ${MYSQL_CLIENT_LIB})
else()
  set(MYSQLX_FOUND FALSE)
endif()

if(MYSQLX_FOUND)
  message(STATUS "Found MySQL client Libraries")
  message(STATUS "  MYSQLX_LIBRARIES   : ${MYSQLX_LIBRARIES}")
  message(STATUS "  MYSQLX_INCLUDE_DIRS: ${MYSQLX_INCLUDE_DIRS}")
else()
  message(STATUS "MYSQLX_CLIENT_LIB: ${MYSQLX_CLIENT_LIB}")
  message(STATUS "MYSQL_PROTO_LIB: ${MYSQLX_PROTO_LIB}")
  message(STATUS "MYSQL_CLIENT_LIB: ${MYSQL_CLIENT_LIB}")
  message(FATAL_ERROR "Could not find MySQL client (libmysqlclient) and X protocol client (libmysqlxclient) libraries. Please set MYSQL_SOURCE_DIR and MYSQL_BUILD_DIR")
endif()

mark_as_advanced(MYSQLX_LIBRARY MYSQLX_INCLUDE_DIRS)