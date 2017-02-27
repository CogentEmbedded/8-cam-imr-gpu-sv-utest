###############################################################################
# CMakeLists.txt
#
# Copyright (c) 2016 Cogent Embedded Inc. ALL RIGHTS RESERVED.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
##############################################################################

cmake_minimum_required (VERSION 3.0)

project("smartcamera-sv" C)

# search for additional CMake modules
SET(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

set(INSTALL_BIN_DIR bin CACHE PATH "Installation directory for executables")
set(INSTALL_LIB_DIR lib CACHE PATH "Installation directory for libraries")
set(INSTALL_INC_DIR include CACHE PATH "Installation directory for headers")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
find_package(Cairo REQUIRED)
find_package(Pango REQUIRED)
find_package(EGL REQUIRED)
find_package(GLIB REQUIRED COMPONENTS gobject gmodule gthread gio)
find_package(GStreamer REQUIRED COMPONENTS
    gstreamer-allocators
    gstreamer-app
    gstreamer-video
)
find_package(OpenGLES2 REQUIRED)
find_package(Wayland REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Spnav QUIET)

#pkg_check_modules (MMNGRBUF REQUIRED mmngrbuf-user-module)
#pkg_check_modules (MMNGR REQUIRED mmngr-user-module)
#pkg_check_modules (SPNAV REQUIRED libspnav)

pkg_check_modules (WAYLAND REQUIRED wayland-client)
pkg_check_modules (MEDIACTRL REQUIRED libmediactl-v4l2)
pkg_check_modules (PNG REQUIRED libpng)

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)

include_directories(
  ${GLIB_INCLUDE_DIRS}
  ${CAIRO_INCLUDE_DIRS}
  ${PANGO_INCLUDE_DIRS}
  ${Z_INCLUDE_DIRS}
  ${OPENGLES2_INCLUDE_DIRS}
  ${EGL_INCLUDE_DIRS}
  ${GSTREAMER_INCLUDE_DIRS}
  ${GSTREAMER-APP_INCLUDE_DIRS}
  ${GSTREAMER-RTP_INCLUDE_DIRS}
  ${GSTREAMER-PBUTILS_INCLUDE_DIRS}
  ${GSTREAMER-VIDEO_INCLUDE_DIRS}
  ${GSTREAMER-ALLOCATOR_INCLUDE_DIRS}
)

# ...common libraries
set(COMMON_LIBRARIES
	"pthread"
	"dl"
	"z"
	"m"
)

set(GLIB_LIBRARIES
	"gobject-2.0"
	"glib-2.0"
	"gmodule-2.0"
	"gthread-2.0"
)

set(GSTREAMER_ALL_LIBRARIES
	${GSTREAMER_LIBRARIES}
	${GSTREAMER-APP_LIBRARIES}
        ${GSTREAMER-RTP_LIBRARIES}
	${GSTREAMER-PNUTILS_LIBRARIES}
	${GSTREAMER-VIDEO_LIBRARIES}
	${GSTREAMER-ALLOCATOR_LIBRARIES}
)

set(VSPM_LIBRARIES "vspm")
set(MMNGR_LIBRARIES "mmngr" "mmngrbuf")
set(SPNAV_LIBRARIES "spnav")

# ...specify include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ...add sources
file(GLOB APP_C_SRC
  "utest/utest-common.c"
  "utest/utest-display.c"
  "utest/utest-vsink.c"
  "utest/utest-vin.c"
  "utest/utest-imr.c"
  "utest/utest-mesh.c"
  "utest/utest-meta.c"
  "utest/utest-imr-sv.c"
  "utest/utest-imr-gui.c"
  "utest/utest-png.c"
  "utest/utest-bmp.c"
  "utest/utest-car.c"
  "utest/utest-compositor.c"
  "utest/utest-sc.c"
  "utest/utest-config.c"
  "utest/utest-main.c"
  "utest/utest-mesh.c"
)

add_executable(sc ${APP_C_SRC})
target_link_libraries(sc
  ${COMMON_LIBRARIES}
  ${GLIB_LIBRARIES}
  ${PANGO_LIBRARIES}
  "pangocairo-1.0"
  ${GSTREAMER_LIBRARIES}
  ${GSTREAMER_ALLOCATORS_LIBRARIES}
  ${GSTREAMER_APP_LIBRARIES}
  ${GSTREAMER_BASE_LIBRARIES}
  ${GSTREAMER_VIDEO_LIBRARIES}
  ${EGL_LIBRARIES}
  ${WAYLAND_LIBRARIES}
  ${CAIRO_LIBRARIES}
  ${MMNGR_LIBRARIES}
  ${MMNGRBUF_LIBRARIES}
  ${VSPM_LIBRARIES}
  ${PNG_LIBRARIES}
  ${SPNAV_LIBRARIES}
  ${OPENGLES2_LIBRARIES}
  ${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/libwvobjparse.a
  "sv"
  "drivermonitor"
  "mmngr"
  "mmngrbuf"
  "m"
)
set_target_properties(sc PROPERTIES SKIP_BUILD_RPATH ON)

install(TARGETS sc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

message(STATUS "Installation directory: ${CMAKE_INSTALL_BINDIR}")
