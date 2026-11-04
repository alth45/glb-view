cmake_minimum_required(VERSION 3.16)

project(glb_viewer
  VERSION 2.0.0
  DESCRIPTION "OpenGL glTF/GLB PBR viewer with IBL"
  LANGUAGES C
)

# ---------- Global ----------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------- Options ----------
option(USE_SYSTEM_DEPS "Pakai paket sistem (glfw3, cglm) ketimbang FetchContent" OFF)
option(ENABLE_WARNINGS "Aktifkan warning compiler" ON)

# ---------- OpenGL ----------
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# ---------- GLFW ----------
if(USE_SYSTEM_DEPS)
  find_package(glfw3 3.3 REQUIRED)
  if(TARGET glfw)
    set(GLFW_TARGET glfw)
  elseif(TARGET glfw3::glfw)
    set(GLFW_TARGET glfw3::glfw)
  else()
    message(FATAL_ERROR "Target GLFW tidak ditemukan")
  endif()
else()
  set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
  include(FetchContent)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(glfw)
  set(GLFW_TARGET glfw)
endif()

# ---------- cglm ----------
if(USE_SYSTEM_DEPS)
  find_package(cglm REQUIRED)
else()
  set(CGLM_STATIC   ON  CACHE BOOL "" FORCE)
  set(CGLM_SHARED   OFF CACHE BOOL "" FORCE)
  set(CGLM_USE_TEST OFF CACHE BOOL "" FORCE)
  set(CGLM_INSTALL  OFF CACHE BOOL "" FORCE)
  include(FetchContent)
  FetchContent_Declare(cglm
    GIT_REPOSITORY https://github.com/recp/cglm.git
    GIT_TAG        v0.9.4
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(cglm)
endif()

# ---------- stb_image.h ----------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/stb_image.h")
  set(STB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")
else()
  include(FetchContent)
  FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(stb)
  set(STB_INCLUDE_DIR "${stb_SOURCE_DIR}")
endif()

# ---------- cgltf.h ----------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/cgltf.h")
  set(CGLTF_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")
else()
  include(FetchContent)
  FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(cgltf)
  set(CGLTF_INCLUDE_DIR "${cgltf_SOURCE_DIR}")
endif()

# ---------- Sources ----------
set(GL_VIEWER_SOURCES
  src/main.c
  src/app/app.c
  src/camera/camera.c
  src/core/log.c
  src/gl/gl_loader.c
  src/gfx/shader.c
  src/gfx/pbr_ibl.c
  src/model/gltf_model.c
  external/stb_impl.c
)

add_executable(glb_viewer ${GL_VIEWER_SOURCES})

target_include_directories(glb_viewer PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/external
  ${STB_INCLUDE_DIR}
  ${CGLTF_INCLUDE_DIR}
)

target_link_libraries(glb_viewer PRIVATE
  OpenGL::GL
  ${GLFW_TARGET}
  cglm
)

# ---------- Platform ----------
if(APPLE)
  target_link_libraries(glb_viewer PRIVATE
    "-framework OpenGL" "-framework Cocoa"
    "-framework IOKit"  "-framework CoreVideo"
  )
elseif(UNIX AND NOT APPLE)
  target_link_libraries(glb_viewer PRIVATE m)
endif()

# ---------- Warnings ----------
if(ENABLE_WARNINGS)
  if(MSVC)
    target_compile_options(glb_viewer PRIVATE /W3)
  else()
    target_compile_options(glb_viewer PRIVATE
      -Wall -Wextra -Wno-unused-parameter
    )
  endif()
endif()

# ---------- Copy assets ----------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
  add_custom_command(TARGET glb_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/assets"
      "$<TARGET_FILE_DIR:glb_viewer>/assets"
  )
endif()