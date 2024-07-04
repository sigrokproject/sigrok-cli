cmake_minimum_required(VERSION 3.1)

project(sigrok-cli
    VERSION 0.8.0
    HOMEPAGE_URL "http://www.sigrok.org")

set(PROJECT_BUG_URL "sigrok-devel@lists.sourceforge.net")
set(PROJECT_PACKAGE_STRING "${PROJECT_NAME} ${PROJECT_VERSION}")

execute_process(
        COMMAND git log -1 --format=%h
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        )


# Source files
set(SR_SOURCES
    main.c
    show.c
    device.c
    session.c
    input.c
    output.c
    decode.c
    sigrok-cli.h
    parsers.c
    anykey.c
    options.c
)

configure_file(cmake/config.h.in config.h)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(SIGROK_CLI REQUIRED
    glib-2.0>=2.32.0
    #libsigrok>=0.5.0
)

list(APPEND CMAKE_PREFIX_PATH "/home/martin/GIT/SigrokInstall/lib/libsigrok")
#set(SIGROK_CLI_INCLUDE_DIRS ${SIGROK_CLI_INCLUDE_DIRS} "/home/martin/GIT/SigrokInstall/include" "/home/martin/GIT/SigrokInstall/include/libsigrok" "/home/martin/GIT/SigrokInstall/include/libsigrokcxx")

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")  # Enable C99 standard

# Include directories
include_directories(${SIGROK_CLI_INCLUDE_DIRS})

# Build executable
add_executable(sigrok-cli ${SR_SOURCES})
target_include_directories(sigrok-cli PRIVATE ${CMAKE_BINARY_DIR}) # config.h

# Link libraries
target_link_libraries(sigrok-cli ${SIGROK_CLI_LIBRARIES})
target_include_directories(sigrok-cli PRIVATE "/home/martin/GIT/SigrokInstall/include")

# libsigrokdecode
option(LOCAL_LIBSIGROKDECODE "Use local libsigrokdecode" false)
if (${LOCAL_LIBSIGROKDECODE})
    Include(FetchContent)
    FetchContent_Declare(libsigrokdecode
        # GIT_REPOSITORY https://github.com/sigrokproject/libsigrokdecode.git
        GIT_REPOSITORY https://github.com/Murmele/libsigrokdecode.git # TODO: remove as soon as libsigrokdecode supports cmake!
        GIT_TAG cmake # GIT_TAG master TODO: change when merged
    )
    FetchContent_MakeAvailable(libsigrokdecode) # name of the target in the CMakeLists.txt
    target_include_directories(sigrok-cli PRIVATE ${libsigrokdecode_SOURCE_DIR})
    target_link_libraries(sigrok-cli sigrokdecode)
    target_compile_definitions(sigrok-cli PRIVATE "-DHAVE_SRD")
else()
    pkg_check_modules(LIBSIGROKDECODE
        libsigrokdecode>=0.5.0
    )
    if (LIBSIGROKDECODE_FOUND)
        target_link_libraries(sigrok-cli ${LIBSIGROKDECODE_LIBRARIES})
        target_compile_definitions(sigrok-cli PRIVATE "-DHAVE_SRD")
        target_include_directories(sigrok-cli PRIVATE ${LIBSIGROKDECODE_INCLUDE_DIRS})
    endif()
endif()

link_directories(BEFORE "/home/martin/GIT/SigrokInstall/lib")
target_link_libraries(sigrok-cli "/home/martin/GIT/SigrokInstall/lib/libsigrok.so")

# Install executable
install(TARGETS sigrok-cli DESTINATION bin)

# Install desktop file and icon
install(FILES contrib/org.sigrok.sigrok-cli.desktop DESTINATION share/applications)
install(FILES contrib/sigrok-cli.svg DESTINATION share/icons/hicolor/scalable/apps)

# Print configuration summary
message("\n--- sigrok-cli configuration summary ---")
message(" - Package version................. ${SC_PACKAGE_VERSION}")
message(" - Prefix.......................... ${CMAKE_INSTALL_PREFIX}")
message(" - Building on..................... ${CMAKE_BUILD_TYPE}")
message(" - Building for.................... ${CMAKE_SYSTEM_PROCESSOR}")

# Compiler information
message("\nCompile configuration:")
message(" - C compiler...................... ${CMAKE_C_COMPILER}")
message(" - C compiler flags................ ${CMAKE_C_FLAGS}")

# Detected libraries
message("\nDetected libraries (required):")
message(" - glib-2.0 >= 2.32.0.............. ${SIGROK_CLI_VERSION}")
message(" - libsigrok >= 0.5.0.............. ${SIGROK_CLI_VERSION}")

if (LIBSIGROKDECODE_FOUND)
    message("\nDetected libraries (optional):")
    message(" - libsigrokdecode >= 0.5.0....... ${LIBSIGROKDECODE_VERSION}")
endif()

