# This is the CMakeLists.txt for compiling listmanager_term_improved.cpp
# with QT Creator.
# The output executable is listmanager_term_improved_qtc and I moved it from
# the build directory to ~/c_stuff/listmanager

cmake_minimum_required(VERSION 3.15)

project(listmanager_cpp)

find_package(SQLite3 REQUIRED)
find_package(PythonLibs REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(Nuspell)

execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

add_definitions("-DGIT_BRANCH=${GIT_BRANCH}")

# these 3 lines were from thecherno for his example helloworld
#set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -std=c++17")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-sign-compare -Wno-stringop-truncation -std=c++2a")
#set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wsign-compare -std=c++2a")
set (source_dir "${PROJECT_SOURCE_DIR}/")

# the below obviously shouldn't be a GLOB since only one source file
#file (GLOB source_files "${source_dir}/listmanager_term_improved.*")

#add_subdirectory("${source_dir}/tests") #new
add_executable(${PROJECT_NAME} listmanager_term_improved.cpp)

# note that for what we're doing doesn't matter whether PUBLIC or PRIVATE (see CMAKE docs)
#target_include_directories(${PROJECT_NAME} PUBLIC "${source_dir}")
target_include_directories(${PROJECT_NAME} PUBLIC ${SQLITE3_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PUBLIC ${PYTHON_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PUBLIC ${PostgreSQL_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/ultralight_sdk/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/maddy/include)
# add_executable has to come before target_link_libraries

# case is important and tripped me up below related to SQLITE and POSTGRES
target_link_libraries(${PROJECT_NAME} ${SQLite3_LIBRARIES} ${PYTHON_LIBRARIES} ${PostgreSQL_LIBRARIES} Nuspell::nuspell ${PROJECT_SOURCE_DIR}/libiniparser.a ${PROJECT_SOURCE_DIR}/ultralight_sdk/bin/libUltralight.so ${PROJECT_SOURCE_DIR}/ultralight_sdk/bin/libUltralightCore.so)

# Note that there is a CMakeLists.txt in:  src, src/vendor/imgui, src/vendor/stb_image and src/tests
# src CMakeLists.txt: target_sources(thecherno PRIVATE Application.cpp IndexBuffer.cpp Renderer.cpp Shader.cpp Texture.cpp VertexArray.cpp VertexBuffer.cpp)
# stb_image CMakeLists.txt: target_sources(thecherno PRIVATE stb_image.cpp)
# imgui CMakeLists.txt target_sources(thecherno PRIVATE imgui.cpp imgui_draw.cpp imgui_impl_glfw_gl3.cpp)
# tests CMakeLists.txt target_sources(thecherno PRIVATE Text.cpp TestClearColor.cpp TestTexture2d.cpp)
