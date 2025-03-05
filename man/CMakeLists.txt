set(man_pages_1 opkg.1)
set(man_pages_5 opkg.conf.5)

if(WITH_GPGME)
    set(man_pages_1 ${man_pages_1} opkg-key.1)
endif()

set(PACKAGE_NAME ${CMAKE_PROJECT_NAME})
set(PACKAGE_STRING "${PACKAGE_NAME} ${PROJECT_VERSION}")

set(SOURCE_DATE_EPOCH "$ENV{SOURCE_DATE_EPOCH}")
if (SOURCE_DATE_EPOCH STREQUAL "")
    execute_process(COMMAND "sh" "-c" "date +\"%B %Y\" | tr -d '\n'" OUTPUT_VARIABLE CLEAN_DATE)
else()
    execute_process(COMMAND  "sh" "-c" "date -d @${SOURCE_DATE_EPOCH}  +\"%B %Y\" | tr -d '\n'" OUTPUT_VARIABLE CLEAN_DATE)
endif()
message(DEBUG "CLEAN_DATE:" ${CLEAN_DATE})

foreach(man_page IN LISTS man_pages_1 man_pages_5)
    configure_file(${man_page}.in ${man_page})
endforeach()

# For CMAKE_INSTALL_MANDIR
include(GNUInstallDirs)
list(TRANSFORM man_pages_1 PREPEND ${CMAKE_CURRENT_BINARY_DIR}/)
list(TRANSFORM man_pages_5 PREPEND ${CMAKE_CURRENT_BINARY_DIR}/)
install(FILES ${man_pages_1} DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES ${man_pages_5} DESTINATION ${CMAKE_INSTALL_MANDIR}/man5)
