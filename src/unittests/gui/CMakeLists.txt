# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT
# Orbit modifications: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

add_subdirectory(config)
add_subdirectory(core)

if(APPLE)
  add_subdirectory(updates)
endif()

create_test(
  NAME LoggerTests
  DEPENDS gui
  SOURCE LoggerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME ArrangementViewTests
  DEPENDS gui
  SOURCE ArrangementViewTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
