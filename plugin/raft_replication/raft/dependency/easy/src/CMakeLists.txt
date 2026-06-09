cmake_minimum_required(VERSION 2.8)

SET (HEADER_FILE
  util/easy_hash.h
  util/easy_pool.h
  util/easy_buf.h
  util/easy_inet.h
  util/easy_array.h
  util/easy_time.h
  util/easy_string.h
  io/easy_client.h
  io/ev.h
  io/easy_socket.h
  io/easy_summary.h
  io/easy_baseth_pool.h
  io/easy_request.h
  io/easy_ssl.h
  io/easy_log.h
  io/easy_io_struct.h
  io/easy_io.h
  io/easy_message.h
  io/easy_connection.h
  packet/easy_simple_handler.h
  packet/easy_tbnet.h
  include/easy_define.h
  include/easy_list.h
  include/easy_atomic.h
  )

SET (PRJ_SRC
  util/easy_string.c
  util/easy_pool.c
  util/easy_time.c
  util/easy_hash.c
  util/easy_inet.c
  util/easy_array.c
  util/easy_buf.c
  io/easy_io.c
  io/easy_socket.c
  io/easy_request.c
  io/easy_baseth_pool.c
  io/easy_summary.c
  io/easy_ssl.c
  io/easy_message.c
  io/easy_client.c
  io/easy_log.c
  io/easy_connection.c
  io/ev.c
  )

INCLUDE_DIRECTORIES (${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)

IF(OPENSSL_VERSION_MAJOR VERSION_EQUAL 3)
  SET_SOURCE_FILES_PROPERTIES(
    io/easy_ssl.c
    PROPERTIES COMPILE_FLAGS "-Wno-error=deprecated-declarations"
)
ENDIF()

ADD_LIBRARY (myeasy STATIC ${PRJ_SRC})

IF (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
TARGET_COMPILE_OPTIONS (myeasy PRIVATE
  -D_GNU_SOURCE -D__STDC_LIMIT_MACROS
  $<$<COMPILE_LANGUAGE:C>:-g -Wall -Werror -Wno-extra -Wno-error=vla -Wno-error=jump-misses-init -Wno-write-strings -Wno-error=undef -Wno-error=implicit-fallthrough= -Wno-error=sign-compare -Wno-error=unused-parameter -Wno-error=discarded-qualifiers -Wno-error=unused-but-set-variable -fPIC -fno-strict-aliasing>
)
ELSE()
TARGET_COMPILE_OPTIONS (myeasy PRIVATE
  -D_GNU_SOURCE -D__STDC_LIMIT_MACROS -DEASY_SPIN_USE_SYS
  $<$<COMPILE_LANGUAGE:C>:-g -Wall -Werror -Wno-extra -Wno-error=vla -Wno-error=jump-misses-init -Wno-write-strings -Wno-error=undef -Wno-error=implicit-fallthrough= -Wno-error=sign-compare -Wno-error=unused-parameter -Wno-error=discarded-qualifiers -Wno-error=unused-but-set-variable -fPIC -fno-strict-aliasing>
)
ENDIF()

IF(WITH_INSTALL)
  INSTALL(TARGETS myeasy DESTINATION lib)
  INSTALL(FILES ${HEADER_FILE} DESTINATION include/easy)
ENDIF()
