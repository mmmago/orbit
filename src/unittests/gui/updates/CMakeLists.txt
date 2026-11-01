# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT

set(updates_dir "${CMAKE_SOURCE_DIR}/src/lib/gui/panel/Updates")
add_executable(ReleaseUpdateTests
  ReleaseUpdateTests.swift
  ${updates_dir}/GitHubRelease.swift
  ${updates_dir}/ReleaseAsset.swift
  ${updates_dir}/ReleaseVersion.swift
  ${updates_dir}/UpdateError.swift
  ${updates_dir}/UpdateInstaller.swift
)
set_target_properties(ReleaseUpdateTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_options(ReleaseUpdateTests PRIVATE "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-swift-version 6>")
target_link_libraries(ReleaseUpdateTests PRIVATE "-framework Security")
add_test(NAME ReleaseUpdateTests COMMAND ReleaseUpdateTests)
set_property(GLOBAL APPEND PROPERTY ${CMAKE_PROJECT_NAME}_tests ReleaseUpdateTests)

add_test(NAME InstallUpdateTests COMMAND /bin/bash
  ${CMAKE_CURRENT_SOURCE_DIR}/InstallUpdateTests.sh
  ${CMAKE_SOURCE_DIR}/src/apps/res/install-update.sh)
