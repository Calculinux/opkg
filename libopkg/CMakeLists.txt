if(STATIC_LIBOPKG)
    set(LIBRARY_TYPE STATIC)
else()
    set(LIBRARY_TYPE SHARED)
endif()

set(OPTIONAL_FILES
    $<$<BOOL:${WITH_CURL}>:opkg_download_curl.c>
    $<$<NOT:$<BOOL:${WITH_CURL}>>:opkg_download_wget.c>
    $<$<BOOL:${WITH_GPGME}>:opkg_gpg.c opkg_gpg.h>
    $<$<BOOL:${WITH_LIBOPKG_API}>:opkg.c opkg.h>
    $<$<BOOL:${WITH_SHA256}>:sha256.c sha256.h>
    $<$<BOOL:${USE_SOLVER_INTERNAL}>:solvers/internal/opkg_action.c solvers/internal/opkg_upgrade_internal.c solvers/internal/opkg_solver_internal.c solvers/internal/opkg_install_internal.c solvers/internal/pkg_depends_internal.c solvers/internal/opkg_upgrade_internal.h solvers/internal/opkg_solver_internal.h solvers/internal/opkg_install_internal.h solvers/internal/pkg_depends_internal.h>
    $<$<BOOL:${USE_SOLVER_LIBSOLV}>:solvers/libsolv/opkg_solver_libsolv.c>
)

add_library(libopkg
    ${LIBRARY_TYPE}
    ${OPTIONAL_FILES}
    cksum_list.h
    conffile.h
    conffile_list.h
    file_list.h
    file_util.h
    hash_table.h
    list.h
    md5.h
    nv_pair.h
    nv_pair_list.h
    opkg_archive.h
    opkg_cmd.h
    opkg_conf.h
    opkg_configure.h
    opkg_download.h
    opkg_install.h
    opkg_message.h
    opkg_remove.h
    opkg_solver.h
    opkg_utils.h
    opkg_verify.h
    parse_util.h
    pkg.h
    pkg_depends.h
    pkg_dest.h
    pkg_dest_list.h
    pkg_extract.h
    pkg_hash.h
    pkg_parse.h
    pkg_src.h
    pkg_src_list.h
    pkg_vec.h
    release.h
    release_parse.h
    sprintf_alloc.h
    str_list.h
    string_util.h
    void_list.h
    xfuncs.h
    xregex.h
    xsystem.h
    cksum_list.c
    conffile.c
    conffile_list.c
    file_list.c
    file_util.c
    hash_table.c
    md5.c
    nv_pair.c
    nv_pair_list.c
    opkg_archive.c
    opkg_cmd.c
    opkg_conf.c
    opkg_configure.c
    opkg_download.c
    opkg_install.c
    opkg_message.c
    opkg_remove.c
    opkg_utils.c
    opkg_verify.c
    parse_util.c
    pkg.c
    pkg_depends.c
    pkg_dest.c
    pkg_dest_list.c
    pkg_extract.c
    pkg_hash.c
    pkg_parse.c
    pkg_src.c
    pkg_src_list.c
    pkg_vec.c
    release.c
    release_parse.c
    sprintf_alloc.c
    str_list.c
    string_util.c
    void_list.c
    xfuncs.c
    xregex.c
    xsystem.c
)

# avoid double "lib" prefix
set_target_properties(libopkg PROPERTIES PREFIX "")
set_target_properties(libopkg PROPERTIES VERSION 1.0.0 SOVERSION 1)

find_package(PkgConfig REQUIRED)
if(STATIC_LIBOPKG)
    # needed for e.g. FindLibArchive.cmake (used by "find_package(LibArchive...)") which does not yet
    # have an argument to use static libs
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    list(APPEND PKG_CONFIG_EXECUTABLE "--static")
endif()

# Always require libarchive
find_package(LibArchive REQUIRED)
target_link_libraries(libopkg ${LibArchive_LIBRARIES})

if(WITH_XZ)
    find_package(LibLZMA REQUIRED)
    target_link_libraries(libopkg ${LIBLZMA_LIBRARIES})
endif()

if(WITH_BZIP2)
    find_package(BZip2 REQUIRED)
    target_link_libraries(libopkg ${BZIP2_LIBRARIES})
endif()

if(WITH_LZ4)
    pkg_check_modules(lz4 REQUIRED IMPORTED_TARGET liblz4)
    target_link_libraries(libopkg PkgConfig::lz4)
endif()

if(WITH_ZSTD)
    pkg_check_modules(zstd REQUIRED IMPORTED_TARGET libzstd)
    target_link_libraries(libopkg PkgConfig::zstd)
endif()

if(WITH_CURL)
    # Specifying CURL_USE_STATIC_LIBS when calling find_package(CURL...) is not enough,
    # and dependencies of libcurl.a (libssh.a, etc.) are not provided in CURL_LIBRARIES.
    # Thus it is necessary to find those dependencies manually using pkg-config.

    # Do not use "IMPORTED_TARGET", because libraries are missing from PkgConfig::curl_pkgconfig
    pkg_check_modules(curl_pkgconfig REQUIRED libcurl)
    target_link_libraries(libopkg ${curl_pkgconfig_LIBRARIES})
endif()

if(WITH_SSLCURL)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(libopkg OpenSSL::Crypto OpenSSL::SSL)
endif()

if(USE_ACL)
    pkg_check_modules(acl REQUIRED IMPORTED_TARGET libacl)
    target_link_libraries(libopkg PkgConfig::acl)
endif()

if(USE_SOLVER_LIBSOLV)
    # There is no FindLibsolv.cmake, use libsolv.pc (pkg-config) instead
    pkg_check_modules(LIBSOLV REQUIRED libsolv)
    target_link_libraries(libopkg ${LIBSOLV_LIBRARIES})
endif()

if(WITH_GPGME)
    pkg_check_modules(gpgme REQUIRED IMPORTED_TARGET gpgme)
    target_link_libraries(libopkg PkgConfig::gpgme)
endif()

target_include_directories(libopkg PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/..)
target_include_directories(libopkg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (NOT STATIC_LIBOPKG)
    install(TARGETS libopkg)
endif()
