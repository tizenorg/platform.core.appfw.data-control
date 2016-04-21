CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

PROJECT(data-control C)

SET(PREFIX ${CMAKE_INSTALL_PREFIX})

INCLUDE_DIRECTORIES (
	include
	)

SET(requires "dlog bundle glib-2.0 appsvc pkgmgr-info capi-base-common cynara-client sqlite3 openssl capi-appfw-application")
INCLUDE(FindPkgConfig)
pkg_check_modules(pkgs REQUIRED ${requires})

FOREACH(flag ${pkgs_CFLAGS})
    SET(EXTRA_CFLAGS "${EXTRA_CFLAGS} ${flag}")
ENDFOREACH(flag)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EXTRA_CFLAGS} -Wall -fPIC -Werror -fvisibility=hidden")
SET(CMAKE_C_FLAGS_DEBUG "-O0 -g")

ADD_LIBRARY (${PROJECT_NAME} SHARED
	src/data-control-noti.c
	src/data-control-map.c
	src/data-control-sql.c
	src/data-control-sql-cursor.c
	src/data-control-provider.c
	src/data-control-internal.c
	src/data_control_internal.c
	src/data_control_noti.c
	src/data_control_map.c
	src/data_control_sql.c
	src/data_control_sql_cursor.c
	src/data_control_provider.c
	)

TARGET_LINK_LIBRARIES(${PROJECT_NAME} ${pkgs_LDFLAGS})

SET_TARGET_PROPERTIES(${PROJECT_NAME}
	PROPERTIES
	VERSION ${FULLVER}
	SOVERSION ${MAJORVER}
	CLEAN_DIRECT_OUTPUT 1
	)

ADD_CUSTOM_COMMAND(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ln -s libdata-control.so.${MAJORVER} libcapi-data-control.so.1)

SET(VERSION ${FULLVER})
CONFIGURE_FILE(${PROJECT_NAME}.pc.in ${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.pc @ONLY)
CONFIGURE_FILE(${PROJECT_NAME}.conf.in ${PROJECT_NAME}.conf @ONLY)

INSTALL(FILES ${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.conf DESTINATION ${SYSCONF_INSTALL_DIR}/dbus-1/session.d/)
INSTALL(FILES ${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.pc DESTINATION ${LIB_INSTALL_DIR}/pkgconfig)
CONFIGURE_FILE(capi-${PROJECT_NAME}.pc.in ${CMAKE_SOURCE_DIR}/capi-${PROJECT_NAME}.pc @ONLY)
INSTALL(FILES ${CMAKE_SOURCE_DIR}/capi-${PROJECT_NAME}.pc DESTINATION ${LIB_INSTALL_DIR}/pkgconfig)

INSTALL(TARGETS ${PROJECT_NAME} DESTINATION ${LIB_INSTALL_DIR})
INSTALL(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include/appfw FILES_MATCHING PATTERN "*.h")
INSTALL(DIRECTORY ./ DESTINATION ${LIB_INSTALL_DIR} FILES_MATCHING PATTERN "*.so*")
