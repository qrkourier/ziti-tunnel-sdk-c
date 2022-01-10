message("CMAKE_CURRENT_SOURCE_DIR:'${CMAKE_CURRENT_SOURCE_DIR}' \
	CMAKE_CURRENT_BINARY_DIR:'${CMAKE_CURRENT_BINARY_DIR}' \
	CMAKE_INSTALL_PREFIX:'${CMAKE_INSTALL_PREFIX}' \
	CMAKE_SYSTEM_NAME:'${CMAKE_SYSTEM_NAME}' \ 
        CMAKE_SYSTEM_PROCESSOR:'${CMAKE_SYSTEM_PROCESSOR}'")

#cpack_add_component(${PROJECT_NAME})
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_CURRENT_BINARY_DIR};${PROJECT_NAME};${PROJECT_NAME};/")

# -OR -
#get_cmake_property(CPACK_COMPONENTS_ALL COMPONENTS)
#list(REMOVE_ITEM CPACK_COMPONENTS_ALL "Unspecified")
#set(CPACK_COMPONENTS_GROUPING "IGNORE")
#set(CPACK_RPM_COMPONENT_INSTALL "ON")

#set(CPACK_GENERATOR "RPM")
set(CPACK_GENERATOR "DEB")

set(CPACK_PROJECT_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/package/CPackGenConfig.cmake)

set(CPACK_PACKAGE_CONTACT "support@netfoundry.io")
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_RELEASE 1)
set(CPACK_PACKAGE_VENDOR "NetFoundry")
set(CPACK_PACKAGE_VERSION ${ver})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}")

set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/ziti")
set(CPACK_BIN_DIR "${CPACK_PACKAGING_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
set(CPACK_ETC_DIR "${CPACK_PACKAGING_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}")
set(CPACK_SHARE_DIR "${CPACK_PACKAGING_INSTALL_PREFIX}/share")
set(ZITI_IDENTITY_FILE "${CPACK_ETC_DIR}/id.json")
set(ZITI_JWT_FILE "${CPACK_ETC_DIR}/id.jwt")

set(INSTALL_IN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/package")
set(INSTALL_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/package")

set(SYSTEMD_SERVICE_NAME "${CPACK_PACKAGE_NAME}")
set(SYSTEMD_UNIT_FILE_NAME "${SYSTEMD_SERVICE_NAME}.service")

configure_file("${INSTALL_IN_DIR}/${SYSTEMD_UNIT_FILE_NAME}.in" 
	       "${INSTALL_OUT_DIR}/${SYSTEMD_UNIT_FILE_NAME}"
	       @ONLY)

set(RPM_POST_INSTALL_IN "${INSTALL_IN_DIR}/post.sh.in")
set(RPM_PRE_UNINSTALL_IN "${INSTALL_IN_DIR}/preun.sh.in")
set(RPM_POST_UNINSTALL_IN "${INSTALL_IN_DIR}/postun.sh.in")

set(CPACK_RPM_POST_INSTALL "${INSTALL_OUT_DIR}/post.sh")
set(CPACK_RPM_PRE_UNINSTALL "${INSTALL_OUT_DIR}/preun.sh")
set(CPACK_RPM_POST_UNINSTALL "${INSTALL_OUT_DIR}/postun.sh")

configure_file("${RPM_POST_INSTALL_IN}" "${CPACK_RPM_POST_INSTALL}" @ONLY)
configure_file("${RPM_PRE_UNINSTALL_IN}" "${CPACK_RPM_PRE_UNINSTALL}" @ONLY)
configure_file("${RPM_POST_UNINSTALL_IN}" "${CPACK_RPM_POST_UNINSTALL}" @ONLY) 

set(DEB_POST_INSTALL_IN "${INSTALL_IN_DIR}/postinst.in")
set(DEB_PRE_UNINSTALL_IN "${INSTALL_IN_DIR}/prerm.in")
set(DEB_POST_UNINSTALL_IN "${INSTALL_IN_DIR}/postrm.in")

set(CPACK_DEB_POST_INSTALL "${INSTALL_OUT_DIR}/postinst")
set(CPACK_DEB_PRE_UNINSTALL "${INSTALL_OUT_DIR}/prerm")
set(CPACK_DEB_POST_UNINSTALL "${INSTALL_OUT_DIR}/postrm")

configure_file("${DEB_POST_INSTALL_IN}" "${CPACK_DEB_POST_INSTALL}" @ONLY)
configure_file("${DEB_PRE_UNINSTALL_IN}" "${CPACK_DEB_PRE_UNINSTALL}" @ONLY)
configure_file("${DEB_POST_UNINSTALL_IN}" "${CPACK_DEB_POST_UNINSTALL}" @ONLY)

install(FILES "${INSTALL_OUT_DIR}/${SYSTEMD_UNIT_FILE_NAME}"
	DESTINATION "${CPACK_SHARE_DIR}"
	COMPONENT ${PROJECT_NAME})

install(DIRECTORY DESTINATION "${CPACK_ETC_DIR}" COMPONENT ${PROJECT_NAME})
