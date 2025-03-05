add_executable(opkg
  opkg.c
)

target_link_libraries(opkg PUBLIC libopkg)
install(TARGETS opkg)
# Necessary for default install path /usr/local/bin and /usr/local/lib
set_target_properties(opkg
    PROPERTIES
        INSTALL_RPATH "\$ORIGIN/../lib"
)
