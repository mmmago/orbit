# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
# Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

# macOS helper that shows the native drag for files dragged in from another
# computer. It runs as its own process so deskflow-core never owns a window:
# once a process has windows, macOS requires keyboard layout APIs to be called
# on the main thread, and deskflow-core calls them from its event thread.

set(target ${CMAKE_PROJECT_NAME}-drag-helper)

add_executable(${target} main.mm ${PROJECT_SOURCE_DIR}/src/lib/platform/OSXQuitHandler.m)
target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src/lib)
set_source_files_properties(main.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

find_library(COCOA_LIBRARY Cocoa)
find_library(UTI_LIBRARY UniformTypeIdentifiers)
target_link_libraries(${target} ${COCOA_LIBRARY} ${UTI_LIBRARY})

if(BUILD_OSX_BUNDLE)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_BUNDLE_CONTENT_DIR:${CMAKE_PROJECT_PROPER_NAME}>/MacOS
    XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
  )
  if (APPLE_CODESIGN_DEV)
    configure_mac_codesign(${target})
  endif()
else()
  install(TARGETS ${target} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
