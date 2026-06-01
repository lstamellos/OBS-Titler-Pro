cmake_minimum_required(VERSION 3.16)
project(obs-titles VERSION 1.0.0)

# ── C++ standard ────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Qt auto-tools ───────────────────────────────────────────────────
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Find OBS ────────────────────────────────────────────────────────
# Try to find standard CMake config package first (e.g. from CMAKE_PREFIX_PATH)
find_package(libobs QUIET)
find_package(obs-frontend-api QUIET)

if(NOT libobs_FOUND OR NOT obs-frontend-api_FOUND)
  if(WIN32)
    # Search for OBS dependencies package
    file(GLOB OBS_CANDIDATES "C:/Users/menac/Desktop/obs-build-dependencies/plugin-deps-*")
    list(SORT OBS_CANDIDATES ORDER DESCENDING)
    list(APPEND OBS_CANDIDATES "C:/Program Files/obs-studio")
    
    foreach(candidate ${OBS_CANDIDATES})
      if(EXISTS "${candidate}/lib/obs.lib" OR EXISTS "${candidate}/lib/obs/obs.lib")
        set(OBS_SDK_DIR "${candidate}" CACHE PATH "Path to OBS Studio SDK" FORCE)
        break()
      endif()
    endforeach()

    if(OBS_SDK_DIR)
      message(STATUS "Found OBS SDK at: ${OBS_SDK_DIR}")
      list(APPEND CMAKE_PREFIX_PATH "${OBS_SDK_DIR}")
      find_library(OBS_LIB NAMES obs HINTS "${OBS_SDK_DIR}/lib" "${OBS_SDK_DIR}/lib/obs")
      find_library(OBS_FRONTEND_API_LIB NAMES obs-frontend-api HINTS "${OBS_SDK_DIR}/lib" "${OBS_SDK_DIR}/lib/obs")
      find_path(OBS_INCLUDE_DIR NAMES obs.h HINTS "${OBS_SDK_DIR}/include" "${OBS_SDK_DIR}/include/obs")

      if(OBS_LIB AND OBS_FRONTEND_API_LIB AND OBS_INCLUDE_DIR)
        # Find DLLs to specify IMPORTED_LOCATION and prevent CMP0111 warnings
        find_file(OBS_DLL NAMES obs.dll HINTS "${OBS_SDK_DIR}/bin/64bit" "${OBS_SDK_DIR}/bin")
        find_file(OBS_FRONTEND_API_DLL NAMES obs-frontend-api.dll HINTS "${OBS_SDK_DIR}/bin/64bit" "${OBS_SDK_DIR}/bin")

        if(NOT TARGET OBS::libobs)
          add_library(OBS::libobs SHARED IMPORTED GLOBAL)
          set_target_properties(OBS::libobs PROPERTIES
            IMPORTED_IMPLIB "${OBS_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${OBS_INCLUDE_DIR}"
          )
          if(OBS_DLL)
            set_target_properties(OBS::libobs PROPERTIES IMPORTED_LOCATION "${OBS_DLL}")
          endif()
        endif()
        if(NOT TARGET OBS::obs-frontend-api)
          add_library(OBS::obs-frontend-api SHARED IMPORTED GLOBAL)
          set_target_properties(OBS::obs-frontend-api PROPERTIES
            IMPORTED_IMPLIB "${OBS_FRONTEND_API_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${OBS_INCLUDE_DIR}"
          )
          if(OBS_FRONTEND_API_DLL)
            set_target_properties(OBS::obs-frontend-api PROPERTIES IMPORTED_LOCATION "${OBS_FRONTEND_API_DLL}")
          endif()
        endif()
        if(EXISTS "${OBS_SDK_DIR}/cmake/Qt6")
          set(Qt6_DIR "${OBS_SDK_DIR}/cmake/Qt6" CACHE PATH "Qt6 cmake dir from OBS" FORCE)
        endif()
        set(libobs_FOUND TRUE)
        set(obs-frontend-api_FOUND TRUE)
      endif()
    endif()
  endif()
endif()

if(NOT libobs_FOUND OR NOT obs-frontend-api_FOUND)
  # Fallback to finding them normally if we didn't resolve them manually
  find_package(libobs REQUIRED)
  find_package(obs-frontend-api REQUIRED)
endif()

# ── Find Qt ─────────────────────────────────────────────────────────
find_package(Qt6 COMPONENTS Core Widgets QUIET)
if(NOT Qt6_FOUND)
  find_package(Qt5 5.15 COMPONENTS Core Widgets REQUIRED)
  set(QT_VERSION_MAJOR 5)
else()
  set(QT_VERSION_MAJOR 6)
endif()

set(QT_LIBS Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets)

# ── Find Cairo + Pango ──────────────────────────────────────────────
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(CAIRO    QUIET cairo)
  pkg_check_modules(PANGO    QUIET pango)
  pkg_check_modules(PANGOCAIRO QUIET pangocairo)
endif()

if(NOT CAIRO_FOUND OR NOT PANGO_FOUND OR NOT PANGOCAIRO_FOUND)
  message(STATUS "PkgConfig not found or missing components. Falling back to native search...")
  
  find_path(CAIRO_INCLUDE_DIR NAMES cairo.h PATH_SUFFIXES cairo)
  find_library(CAIRO_LIBRARY NAMES cairo)
  
  find_path(PANGO_INCLUDE_DIR NAMES pango/pango.h PATH_SUFFIXES pango-1.0)
  find_library(PANGO_LIBRARY NAMES pango-1.0)
  
  find_path(PANGOCAIRO_INCLUDE_DIR NAMES pango/pangocairo.h PATH_SUFFIXES pango-1.0)
  find_library(PANGOCAIRO_LIBRARY NAMES pangocairo-1.0)
  
  find_path(GLIB_INCLUDE_DIR NAMES glib.h PATH_SUFFIXES glib-2.0)
  if(GLIB_INCLUDE_DIR)
    get_filename_component(GLIB_PARENT_DIR "${GLIB_INCLUDE_DIR}" DIRECTORY)
    get_filename_component(GLIB_GRANDPARENT_DIR "${GLIB_PARENT_DIR}" DIRECTORY)
    find_path(GLIB_CONFIG_INCLUDE_DIR NAMES glibconfig.h
      HINTS "${GLIB_PARENT_DIR}/lib" "${GLIB_GRANDPARENT_DIR}/lib"
      PATH_SUFFIXES glib-2.0/include
    )
  else()
    find_path(GLIB_CONFIG_INCLUDE_DIR NAMES glibconfig.h PATH_SUFFIXES glib-2.0/include)
  endif()
  find_library(GLIB_LIBRARY NAMES glib-2.0)
  find_library(GOBJECT_LIBRARY NAMES gobject-2.0)
  find_path(HARFBUZZ_INCLUDE_DIR NAMES hb.h PATH_SUFFIXES harfbuzz)
  find_library(HARFBUZZ_LIBRARY NAMES harfbuzz)
  
  if(CAIRO_INCLUDE_DIR AND CAIRO_LIBRARY AND PANGO_INCLUDE_DIR AND PANGO_LIBRARY AND PANGOCAIRO_LIBRARY)
    set(CAIRO_FOUND TRUE)
    get_filename_component(CAIRO_PARENT_DIR "${CAIRO_INCLUDE_DIR}" DIRECTORY)
    set(CAIRO_INCLUDE_DIRS ${CAIRO_INCLUDE_DIR} ${CAIRO_PARENT_DIR})
    set(CAIRO_LIBRARIES ${CAIRO_LIBRARY})
    
    set(PANGO_FOUND TRUE)
    get_filename_component(PANGO_PARENT_DIR "${PANGO_INCLUDE_DIR}" DIRECTORY)
    set(PANGO_INCLUDE_DIRS ${PANGO_INCLUDE_DIR} ${PANGO_PARENT_DIR} ${GLIB_INCLUDE_DIR} ${GLIB_CONFIG_INCLUDE_DIR})
    if(HARFBUZZ_INCLUDE_DIR)
      list(APPEND PANGO_INCLUDE_DIRS ${HARFBUZZ_INCLUDE_DIR})
    endif()
    set(PANGO_LIBRARIES ${PANGO_LIBRARY} ${GLIB_LIBRARY} ${GOBJECT_LIBRARY})
    if(HARFBUZZ_LIBRARY)
      list(APPEND PANGO_LIBRARIES ${HARFBUZZ_LIBRARY})
    endif()
    
    set(PANGOCAIRO_FOUND TRUE)
    get_filename_component(PANGOCAIRO_PARENT_DIR "${PANGOCAIRO_INCLUDE_DIR}" DIRECTORY)
    set(PANGOCAIRO_INCLUDE_DIRS ${PANGOCAIRO_INCLUDE_DIR} ${PANGOCAIRO_PARENT_DIR})
    set(PANGOCAIRO_LIBRARIES ${PANGOCAIRO_LIBRARY})
    
    message(STATUS "Native Cairo found: ${CAIRO_LIBRARY}")
    message(STATUS "Native Pango found: ${PANGO_LIBRARY}")
    message(STATUS "Native PangoCairo found: ${PANGOCAIRO_LIBRARY}")
  else()
    message(FATAL_ERROR "Could not find Cairo, Pango, or PangoCairo. Please install dependencies.")
  endif()
endif()

# ── nlohmann/json ────────────────────────────────────────────────────
# Either install via package manager:  apt install nlohmann-json3-dev
# or rely on FetchContent:
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

# ── Sources ─────────────────────────────────────────────────────────
set(PLUGIN_SOURCES
    src/plugin-main.cpp
    src/title-data.cpp
    src/title-source.cpp
    src/title-dock.cpp
    src/title-editor.cpp
)

set(PLUGIN_HEADERS
    src/plugin-main.h
    src/title-data.h
    src/title-source.h
    src/title-dock.h
    src/title-editor.h
)

# ── Target ──────────────────────────────────────────────────────────
add_library(obs-titles MODULE
    ${PLUGIN_SOURCES}
    ${PLUGIN_HEADERS}
)

target_include_directories(obs-titles PRIVATE
    src
    ${CAIRO_INCLUDE_DIRS}
    ${PANGO_INCLUDE_DIRS}
    ${PANGOCAIRO_INCLUDE_DIRS}
)

target_link_libraries(obs-titles PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    ${QT_LIBS}
    ${CAIRO_LIBRARIES}
    ${PANGO_LIBRARIES}
    ${PANGOCAIRO_LIBRARIES}
    nlohmann_json::nlohmann_json
)

if(CAIRO_CFLAGS_OTHER)
  target_compile_options(obs-titles PRIVATE ${CAIRO_CFLAGS_OTHER})
endif()
if(PANGO_CFLAGS_OTHER)
  target_compile_options(obs-titles PRIVATE ${PANGO_CFLAGS_OTHER})
endif()
if(PANGOCAIRO_CFLAGS_OTHER)
  target_compile_options(obs-titles PRIVATE ${PANGOCAIRO_CFLAGS_OTHER})
endif()

# ── Locale / data files ──────────────────────────────────────────────
target_compile_definitions(obs-titles PRIVATE
    PLUGIN_NAME="obs-titles"
    PLUGIN_VERSION="${PROJECT_VERSION}"
)

# ── OBS module helper (sets correct output name & install path) ──────
# This macro is provided by the OBS cmake helpers:
#   set_target_obs_bundle(obs-titles)
# If building standalone:
set_target_properties(obs-titles PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/obs-plugins"
)

# ── Install ──────────────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    install(TARGETS obs-titles
            LIBRARY DESTINATION lib/obs-plugins)
elseif(APPLE)
    install(TARGETS obs-titles
            LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/obs-plugins)
elseif(WIN32)
    install(TARGETS obs-titles
            DESTINATION obs-plugins/64bit)
endif()

# ── Locale files ────────────────────────────────────────────────────
install(DIRECTORY data/locale
        DESTINATION data/obs-plugins/obs-titles)

# ── Print summary ────────────────────────────────────────────────────
message(STATUS "obs-titles: Qt${QT_VERSION_MAJOR}, Cairo ${CAIRO_VERSION}")
