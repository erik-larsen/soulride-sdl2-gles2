cmake_minimum_required(VERSION 3.16)
project(soulride CXX C)

# Prefer Homebrew dylibs over any stale SDL2.framework in /Library/Frameworks.
set(CMAKE_FIND_FRAMEWORK LAST)

# 2003-era codebase; build as gnu++98 (CMake spells it "98").
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(EMSCRIPTEN)
  # SDL2 / SDL2_mixer come from the Emscripten ports system; the USE
  # flags are needed at compile time for the include paths too.
  add_compile_options(-sUSE_SDL=2 -sUSE_SDL_MIXER=2 -fexceptions)
else()
  find_package(SDL2 REQUIRED CONFIG)
  find_package(SDL2_mixer REQUIRED CONFIG)
endif()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ------------------------------------------------------------------ lua 3.x
set(LUA_CORE_SOURCES
  lapi.c lauxlib.c lbuffer.c lbuiltin.c ldo.c lfunc.c lgc.c llex.c
  lmem.c lobject.c lparser.c lstate.c lstring.c ltable.c ltm.c
  lundump.c lvm.c lzio.c)
list(TRANSFORM LUA_CORE_SOURCES PREPEND ${SRC}/lua/src/)

set(LUA_LIB_SOURCES linit.c ldblib.c liolib.c lmathlib.c lstrlib.c)
list(TRANSFORM LUA_LIB_SOURCES PREPEND ${SRC}/lua/src/lib/)

add_library(lua STATIC ${LUA_CORE_SOURCES} ${LUA_LIB_SOURCES})
target_include_directories(lua PUBLIC ${SRC}/lua/include PRIVATE ${SRC}/lua/src)

# ------------------------------------------------------------------ gamegui
set(GAMEGUI_SOURCES
  gamegui.cpp gg_actor.cpp gg_audio.cpp gg_event.cpp gg_file.cpp
  gg_font_impl.cpp gg_log.cpp gg_movie_impl.cpp gg_player_impl.cpp
  gg_psdread.cpp gg_resource.cpp gg_string.cpp gg_script.cpp)
list(TRANSFORM GAMEGUI_SOURCES PREPEND ${SRC}/gamegui/)

add_library(gamegui STATIC ${GAMEGUI_SOURCES})
target_include_directories(gamegui PUBLIC ${SRC}/gamegui PRIVATE ${SRC})
target_compile_definitions(gamegui PRIVATE LINUX=1)

# ------------------------------------------------------------------ soulride
set(GAME_SOURCES
  attractmode.cpp avi_tools.cpp boarder.cpp boarderctl.cpp camera.cpp
  clip.cpp collide.cpp config.cpp console.cpp finish.cpp game.cpp
  gameloop.cpp geometry.cpp gstamp.cpp highscore.cpp imageoverlay.cpp
  input.cpp lift.cpp main.cpp mdynamic.cpp mesh.cpp model.cpp
  multiplayer.cpp music.cpp ogl.cpp orientedstatic.cpp overlay.cpp
  particle.cpp path.cpp perftest.cpp player.cpp polygonregion.cpp
  psdread.cpp recording.cpp sbox.cpp screenshot.cpp scylinder.cpp
  staticlod.cpp stickman.cpp surface.cpp surfaceshader.cpp
  terrainmesh.cpp terrainmodel.cpp text.cpp timer.cpp tree.cpp ui.cpp
  uihelidrop.cpp uimainmenu.cpp uioptions.cpp uiplayback.cpp
  uiplayer.cpp usercamera.cpp utility.cpp view.cpp weather.cpp
  linuxmain.cpp oglrender.cpp linuxsound.cpp srgl.cpp)
list(TRANSFORM GAME_SOURCES PREPEND ${SRC}/)

add_executable(soulride ${GAME_SOURCES})
target_include_directories(soulride PRIVATE ${SRC})
target_compile_definitions(soulride PRIVATE LINUX=1)

if(EMSCRIPTEN)
  target_link_libraries(soulride PRIVATE lua gamegui)
  target_link_options(soulride PRIVATE
    -sUSE_SDL=2
    -sUSE_SDL_MIXER=2
    -fexceptions
    -sALLOW_MEMORY_GROWTH=1
    -sMAX_WEBGL_VERSION=1
    -sSTACK_SIZE=1048576
    -lidbfs.js
    -sEXPORTED_RUNTIME_METHODS=FS
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/data@/data"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/Recordings@/Recordings"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/config.txt@/config.txt"
    "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/web/prejs.js"
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
  # Relink when the web wrapper files change.
  set_target_properties(soulride PROPERTIES LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/web/prejs.js;${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
  # The build directory is directly publishable (e.g. GitHub Pages).
  add_custom_command(TARGET soulride POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      $<TARGET_FILE_DIR:soulride>/soulride.html
      $<TARGET_FILE_DIR:soulride>/index.html)
  set_target_properties(soulride PROPERTIES SUFFIX ".html")
elseif(APPLE)
  target_compile_definitions(soulride PRIVATE GL_SILENCE_DEPRECATION=1)
  find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
  target_link_libraries(soulride PRIVATE ${OPENGL_FRAMEWORK})
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(soulride PRIVATE OpenGL::GL)
endif()

if(NOT EMSCRIPTEN)
  # SDL2_mixer's CMake config exports SDL2_mixer::SDL2_mixer.
  target_link_libraries(soulride PRIVATE
    lua gamegui
    SDL2::SDL2
    SDL2_mixer::SDL2_mixer)
endif()
