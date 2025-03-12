# We want to support the cmake version of debian stable
cmake_minimum_required(VERSION 3.25)

project(opkg
    VERSION 0.8.0 # https://semver.org/spec/v0.1.0.html
    DESCRIPTION "Opkg package manager"
    HOMEPAGE_URL "https://git.yoctoproject.org/opkg/"
    LANGUAGES C
)
# Hack to support "+git" suffix which cmake doesn't support normally.
# See https://gitlab.kitware.com/cmake/cmake/-/issues/16716#note_519812
set(PROJECT_VERSION "${PROJECT_VERSION}+git")
set(VERSION ${PROJECT_VERSION})

option(STATIC_LIBOPKG "Statically link libopkg into opkg" OFF)
# The current libopkg API is deprecated so only build it if requested
option(WITH_LIBOPKG_API "Enable deprecated libopkg API" OFF)
option(WITH_XZ "Enable xz compressor support" ON)
option(WITH_BZIP2 "Enable bzip2 compressor support" OFF)
option(WITH_LZ4 "Enable lz4 compressor support" OFF)
option(WITH_ZSTD "Enable zstd compressor support" OFF)
option(WITH_CURL "Enable downloading with curl" ON)
include(CMakeDependentOption)
cmake_dependent_option(WITH_SSLCURL "Enable certificate authentication with curl" ON "WITH_CURL" OFF)
option(WITH_SHA256 "Enable sha256sum check" OFF)
option(USE_ACL "Enable ACL support" ON)
option(USE_XATTR "Enable xattr support" ON)
option(USE_SOLVER_LIBSOLV "Enable libsolv solver support if true. Enable the internal solver if false." ON)
if(USE_SOLVER_LIBSOLV)
    set(USE_SOLVER_INTERNAL OFF CACHE BOOL "Disable internal solver")
    if(WITH_LIBOPKG_API)
        # do not allow opkg to be built with both old API and external solver support
        message(FATAL_ERROR "External solver support not compatible with deprecated libopkg API!")
    endif()
else()
    message(WARNING "The opkg internal sat-solver is marked for DEPRECATION in a future release. Consider using the libsolv solver instead.")
    set(USE_SOLVER_INTERNAL ON CACHE BOOL "Enable internal solver")
endif()
option(WITH_GPGME "Enable signature checking with gpgme" ON)


set(DATADIR "/share" CACHE STRING "datadir")
set(SYSCONFDIR "/etc" CACHE STRING "sysconfdir")
set(VARDIR "/var" CACHE STRING "vardir")
# This was ported from autoconf (it used to be "host_cpu"), but it should be target_cpu: https://bugzilla.yoctoproject.org/show_bug.cgi?id=15784
set(HOST_CPU_STR ${CMAKE_HOST_SYSTEM_PROCESSOR} CACHE STRING "Host CPU architecture")

configure_file(config.h.in config.h)
configure_file(libopkg.pc.in libopkg.pc @ONLY)
include(GNUInstallDirs)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/libopkg.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

install(PROGRAMS intercept/depmod intercept/ldconfig intercept/update-modules DESTINATION ${CMAKE_INSTALL_PREFIX}${DATADIR}/opkg/intercept)

add_subdirectory(libopkg)
add_subdirectory(man)
add_subdirectory(src)
add_subdirectory(utils)

# make is intentionally hard-coded here. Don't use CMAKE_MAKE_PROGRAM, as tests/Makefile is not a generated Makefile (and thus doesn't use the cmake generators like ninja etc.)
add_custom_target(run-tests OPKG_PATH=${CMAKE_CURRENT_BINARY_DIR}/src/opkg make -C ${CMAKE_CURRENT_SOURCE_DIR}/tests DATADIR=${DATADIR} SYSCONFDIR=${SYSCONFDIR} VARDIR=${VARDIR})
add_custom_target(check ${CMAKE_MAKE_PROGRAM} run-tests)

# Call with cd build && cmake .. && cmake --install . --component=ptest
install(DIRECTORY tests COMPONENT ptest EXCLUDE_FROM_ALL DESTINATION "$ENV{PTEST_PATH}")
add_custom_target(install-ptest COMMAND ${CMAKE_COMMAND} --install ${CMAKE_CURRENT_BINARY_DIR} --component=ptest)
