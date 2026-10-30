cmake_minimum_required(VERSION 3.22)

# kody-kart is a kodeOS app on the kode SDK, in the shape of kodediy/kode-app-template
# (like kodegotchi, 2026-10-19). The block below is the template's, verbatim: the chip is spelled out
# because it has to be known before kbsp's sdkconfig.board has been fetched, and a
# clean checkout configures twice so the component manager can fetch the SDK first.
set(IDF_TARGET esp32p4)

set(KBSP_BOARD
    "${CMAKE_CURRENT_LIST_DIR}/managed_components/kodediy__kbsp/sdkconfig.board")

if(NOT EXISTS "${KBSP_BOARD}" AND NOT KBSP_FETCH_PASS)
    message(STATUS "Fetching the SDK and the board settings (first build only)")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}"
                -S "${CMAKE_CURRENT_LIST_DIR}"
                -B "${CMAKE_BINARY_DIR}/kbsp_fetch"
                -DKBSP_FETCH_PASS=ON
                "-DSDKCONFIG=${CMAKE_BINARY_DIR}/kbsp_fetch/sdkconfig"
        RESULT_VARIABLE KBSP_FETCH_RC)
    file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/kbsp_fetch")
    if(NOT EXISTS "${KBSP_BOARD}")
        message(FATAL_ERROR
            "The SDK could not be fetched (cmake ended with ${KBSP_FETCH_RC}). "
            "Read the first error above. A refused git fetch means that your "
            "GitHub account does not list your SSH key.")
    endif()
endif()

if(EXISTS "${KBSP_BOARD}")
    set(SDKCONFIG_DEFAULTS
        "${KBSP_BOARD};${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
else()
    # The throwaway configure, which runs without the board settings.
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
endif()

# What the launcher prints under the name on the pill.
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "01.00.00")
endif()


include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The project name is the app's identity on the Dot, and the image is build/<name>.bin.
project(kody_kart)
