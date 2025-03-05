install(PROGRAMS opkg-check-config TYPE BIN)

if(WITH_GPGME)
    install(PROGRAMS opkg-key TYPE BIN)
endif()
