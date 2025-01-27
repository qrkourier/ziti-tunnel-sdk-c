cmake_policy(SET CMP0057 NEW)

if(CPACK_GENERATOR MATCHES "RPM")
    set(CPACK_RPM_BUILDREQUIRES "cmake >= ${CMAKE_MINIMUM_REQUIRED_VERSION}, systemd-devel, gawk, gcc-c++ >= 4.9, json-c-devel, protobuf-c-devel, python3, openssl-devel, zlib-devel")
    if(CPACK_OS_RELEASE_NAME IN_LIST CPACK_RPM_DISTRIBUTIONS AND CPACK_OS_RELEASE_VERSION VERSION_GREATER "7")
        list(APPEND CPACK_RPM_BUILDREQUIRES "systemd-rpm-macros")
    endif()
    set(CPACK_RPM_PACKAGE_SOURCES OFF)
    set(CPACK_RPM_PACKAGE_REQUIRES "iproute, gawk, systemd, libatomic, json-c, protobuf-c, openssl-libs, zlib, polkit")
    set(CPACK_RPM_CHANGELOG_FILE "${CMAKE_CURRENT_LIST_DIR}/RPM_CHANGELOG")
    set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
    set(CPACK_RPM_PACKAGE_DESCRIPTION "The OpenZiti Edge Tunnel is a zero-trust tunneling software client.")
    set(CPACK_RPM_PACKAGE_GROUP "OpenZiti Client")
    set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
    set(CPACK_RPM_PACKAGE_RELEASE ${CPACK_PACKAGE_RELEASE})
    set(CPACK_RPM_PACKAGE_RELEASE_DIST "OFF")
    set(CPACK_RPM_PACKAGE_SUMMARY "OpenZiti Edge Tunneling Software Client")
    set(CPACK_RPM_PACKAGE_URL "https://openziti.io/")
    
    set(CPACK_RPM_PRE_INSTALL_SCRIPT_FILE "${CPACK_RPM_PRE_INSTALL}")
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CPACK_RPM_POST_INSTALL}")
    set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${CPACK_RPM_PRE_UNINSTALL}")
    set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CPACK_RPM_POST_UNINSTALL}")
    # systemd package on redhat 7 does not contain `systemd-sysusers`, so include shadow-utils
    # util-linux provides /usr/sbin/nologin.
    set(CPACK_RPM_PACKAGE_REQUIRES_PRE "systemd, shadow-utils, util-linux")
    set(CPACK_RPM_PACKAGE_REQUIRES_POST "systemd")
    set(CPACK_RPM_PACKAGE_REQUIRES_PREUN "systemd")
    set(CPACK_RPM_PACKAGE_REQUIRES_POSTUN "systemd")
    #[[
    set(CPACK_RPM_ziti-edge-tunnel_PACKAGE_NAME "${CPACK_RPM_PACKAGE_NAME}")
    set(CPACK_RPM_ziti-edge-tunnel_FILE_NAME "RPM-DEFAULT")
    ]]
    endif(CPACK_GENERATOR MATCHES "RPM")

if(CPACK_GENERATOR MATCHES "DEB")
    # note: libssl and libcrypto are 1.1.x or older on ubuntu <= 20. there we don't actually depend on libssl1.1 or libssl1.0.0.
    # when building on those distros/versions we compile the latest openssl libs and link statically, but it is clear how to
    # specify "libssl3 if it exists in the repos, or nothing" as a dependency.
    # systemd package on older distros does not contain `systemd-sysusers`, so include passwd for `useradd`, `groupadd`.
    # login provides `/usr/sbin/nologin`.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "debconf, iproute2, sed, systemd, libatomic1, libjson-c3 | libjson-c4 | libjson-c5 , libprotobuf-c1, libssl3 | libssl1.1 | libssl1.0.0, login, passwd, policykit-1, zlib1g")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CPACK_DEB_CONFFILES};${CPACK_DEB_PRE_INSTALL};${CPACK_DEB_POST_INSTALL};${CPACK_DEB_PRE_UNINSTALL};${CPACK_DEB_POST_UNINSTALL};${CPACK_DEB_TEMPLATES}")
endif(CPACK_GENERATOR MATCHES "DEB")
