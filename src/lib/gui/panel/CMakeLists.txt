# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT

# The menu bar panel, in SwiftUI. Objective-C++ (gui/OrbitPanel.mm) uses it
# through the generated OrbitPanel-Swift.h header.

file(GLOB_RECURSE panel_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.swift")
set(panel_header_dir "${CMAKE_CURRENT_BINARY_DIR}/include")

add_library(orbit_panel STATIC ${panel_sources})
# no Qt code generation here: this target is Swift only
set_target_properties(orbit_panel PROPERTIES
  Swift_MODULE_NAME OrbitPanel
  AUTOMOC OFF
  AUTOUIC OFF
  AUTORCC OFF
)
target_compile_options(orbit_panel PRIVATE
  "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-swift-version 6>"
  "$<$<COMPILE_LANGUAGE:Swift>:-emit-objc-header>"
  "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-emit-objc-header-path ${panel_header_dir}/OrbitPanel-Swift.h>"
)
target_include_directories(orbit_panel INTERFACE "${panel_header_dir}")
target_link_libraries(orbit_panel INTERFACE "-framework AppKit" "-framework SwiftUI" "-framework Security")

# C++ targets link this with clang: tell the linker where the Swift runtime
# is (it ships with macOS; the objects ask for it with autolink entries)
execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE sdk_path OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND xcrun --find swiftc OUTPUT_VARIABLE swiftc_path OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(swift_toolchain "${swiftc_path}" DIRECTORY)
target_link_directories(orbit_panel INTERFACE
  "${sdk_path}/usr/lib/swift"
  "${swift_toolchain}/../lib/swift/macosx"
)

# Objective-C++ that includes the generated header must wait for the whole
# Swift build, not only for its first steps
add_custom_target(orbit_panel_header DEPENDS orbit_panel)
