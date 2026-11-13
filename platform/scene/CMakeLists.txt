cmake_minimum_required(VERSION 3.16)
project(emberfall_scene LANGUAGES C CXX)
include(CTest)

if(NOT TARGET nnn-script-core)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../core" "${CMAKE_CURRENT_BINARY_DIR}/nnn-core")
endif()
if(NOT TARGET nnn-dwq-core)
    message(FATAL_ERROR "emberfall-scene requires nnn-dwq-core from the same core")
endif()
if(NOT TARGET emberfall-render)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../render" "${CMAKE_CURRENT_BINARY_DIR}/nnn-render")
endif()
find_package(Threads REQUIRED)
add_library(emberfall-scene STATIC src/scene.cpp src/game_assets.cpp src/game_session.cpp src/dialogue_features.cpp
    src/pc_save.cpp src/exe_analysis.cpp src/save_layout.cpp src/story_compositor.cpp)
target_include_directories(emberfall-scene PUBLIC include)
target_link_libraries(emberfall-scene PUBLIC nnn-script-core nnn-game-audio nnn-dwq-core nnn-png-core nnn-archive-core emberfall-render Threads::Threads)
target_compile_features(emberfall-scene PUBLIC cxx_std_17)
if(NOT ANDROID)
    add_executable(nnn-scene-player src/player.cpp src/audio_export.cpp)
    target_link_libraries(nnn-scene-player PRIVATE emberfall-scene png_static)
endif()
set_target_properties(emberfall-scene PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(emberfall-scene PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()
if(BUILD_TESTING)
    add_executable(emberfall-scene-tests tests/scene_tests.cpp)
    target_link_libraries(emberfall-scene-tests PRIVATE emberfall-scene)
    target_compile_definitions(emberfall-scene-tests PRIVATE
        EMBERFALL_SCENE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    set_target_properties(emberfall-scene-tests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME emberfall-scene-behavior COMMAND emberfall-scene-tests)
    set_tests_properties(emberfall-scene-behavior PROPERTIES TIMEOUT 60)
    if(NOT ANDROID)
        find_package(Threads REQUIRED)
        add_executable(emberfall-surface-binding-tests ../android/tests/SurfaceBindingTest.cpp)
        target_compile_features(emberfall-surface-binding-tests PRIVATE cxx_std_17)
        target_link_libraries(emberfall-surface-binding-tests PRIVATE Threads::Threads)
        add_test(NAME emberfall-surface-binding COMMAND emberfall-surface-binding-tests)
        set_tests_properties(emberfall-surface-binding PROPERTIES TIMEOUT 15)

        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(
                NAME emberfall-config-audit
                COMMAND "${Python3_EXECUTABLE}" -m unittest discover
                    -s "${CMAKE_CURRENT_SOURCE_DIR}/tools" -p "test_*.py"
            )
            set_tests_properties(emberfall-config-audit PROPERTIES TIMEOUT 30)

            set(_emberfall_jdk_hints)
            if(DEFINED ENV{JAVA_HOME} AND NOT "$ENV{JAVA_HOME}" STREQUAL "")
                list(APPEND _emberfall_jdk_hints "$ENV{JAVA_HOME}")
            endif()
            list(APPEND _emberfall_jdk_hints "/home/rooboko/.cache/systemnnn-m/tyn-build/jdk-17.0.21+9")
            unset(EMBERFALL_JAVAC CACHE)
            unset(EMBERFALL_JAVA CACHE)
            find_program(EMBERFALL_JAVAC NAMES javac HINTS ${_emberfall_jdk_hints} PATH_SUFFIXES bin NO_DEFAULT_PATH)
            find_program(EMBERFALL_JAVA NAMES java HINTS ${_emberfall_jdk_hints} PATH_SUFFIXES bin NO_DEFAULT_PATH)
            if(NOT EMBERFALL_JAVAC OR NOT EMBERFALL_JAVA)
                find_program(EMBERFALL_JAVAC NAMES javac)
                find_program(EMBERFALL_JAVA NAMES java)
            endif()

            set(_emberfall_android_jar_candidates)
            if(DEFINED ENV{ANDROID_SDK_ROOT} AND NOT "$ENV{ANDROID_SDK_ROOT}" STREQUAL "")
                file(GLOB _emberfall_sdk_jars "$ENV{ANDROID_SDK_ROOT}/platforms/*/android.jar")
                list(SORT _emberfall_sdk_jars ORDER DESCENDING)
                list(APPEND _emberfall_android_jar_candidates ${_emberfall_sdk_jars})
            endif()
            if(DEFINED ENV{ANDROID_HOME} AND NOT "$ENV{ANDROID_HOME}" STREQUAL "")
                file(GLOB _emberfall_home_jars "$ENV{ANDROID_HOME}/platforms/*/android.jar")
                list(SORT _emberfall_home_jars ORDER DESCENDING)
                list(APPEND _emberfall_android_jar_candidates ${_emberfall_home_jars})
            endif()
            list(APPEND _emberfall_android_jar_candidates
                "/home/rooboko/.cache/systemnnn-m/tyn-build/android-sdk/platforms/android-36/android.jar")
            set(EMBERFALL_ANDROID_JAR "")
            foreach(_jar IN LISTS _emberfall_android_jar_candidates)
                if(EXISTS "${_jar}")
                    set(EMBERFALL_ANDROID_JAR "${_jar}")
                    break()
                endif()
            endforeach()

            if(EMBERFALL_JAVAC AND EMBERFALL_JAVA AND EMBERFALL_ANDROID_JAR)
                get_filename_component(_emberfall_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
                add_test(
                    NAME emberfall-android-jvm
                    COMMAND "${Python3_EXECUTABLE}" -c [=[
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

javac, java, android_jar, repo_arg, classes_arg = sys.argv[1:6]
repo = Path(repo_arg)
classes = Path(classes_arg)
shutil.rmtree(classes, ignore_errors=True)
classes.mkdir(parents=True, exist_ok=True)

sjis = repo / "build/tyn-upstream-aligned/engine/src/main/java/com/tyranor/runtime/systemnnn/SjisTunnelDecoder.java"
if not sjis.is_file():
    sjis = repo / "platform/android/app/java/com/tyranor/runtime/systemnnn/SjisTunnelDecoder.java"
if not sjis.is_file():
    raise SystemExit(f"Missing SjisTunnelDecoder.java under {repo}")

sources = [
    sjis,
    *sorted((repo / "platform/android/player/java").glob("*.java")),
    *sorted((repo / "platform/android/tests").glob("*.java")),
]
subprocess.run(
    [javac, "-encoding", "UTF-8", "-cp", android_jar, "-d", str(classes), *(str(p) for p in sources)],
    check=True,
)

cp = f"{classes}{os.pathsep}{android_jar}"
desktop = Path(os.environ.get("SYSTEMNNN_DESKTOP", "/mnt/c/Users/a7188/Desktop"))
mng = desktop / "鏖殺ノ乙女"
dea_parent = desktop / "魔法少女消耗战线 死终Ω神盾"
dea_zh = dea_parent / "DeadEndAegis [zh]"
dea_zht = dea_parent / "DeadEndAegis [zht]"
gokuin = desktop / "(18禁ゲーム)[Black Cyc] 獄淫の尖塔"
ref_indices = repo / "research/emberfall-platform/text-tunnel/reference-indices.txt"


def run_suite(main_class, *args):
    proc = subprocess.run(
        [java, "-ea", "-cp", cp, main_class, *(str(a) for a in args)],
        capture_output=True,
        text=True,
    )
    if proc.stdout:
        sys.stdout.write(proc.stdout)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)
    return proc.stdout


run_suite("com.tyranor.runtime.systemnnn.PcSaveInteropTest")
run_suite("com.tyranor.runtime.systemnnn.DialogueLayoutTest", *([dea_zh] if dea_zh.is_dir() else []))
run_suite(
    "com.tyranor.runtime.systemnnn.DialogueMarkupTest",
    *([dea_zh, gokuin] if dea_zh.is_dir() and gokuin.is_dir() else []),
)
run_suite("com.tyranor.runtime.systemnnn.GalleryCatalogTest")
cfg_args = (
    [mng, dea_zh, dea_zht]
    if mng.is_dir() and dea_zh.is_dir() and dea_zht.is_dir()
    else ([mng] if mng.is_dir() else [])
)
run_suite("com.tyranor.runtime.systemnnn.GameConfigTest", *cfg_args)
save_args = [g for g in (dea_zh, dea_zht, gokuin) if g.is_dir()]
run_suite("com.tyranor.runtime.systemnnn.GameSaveTest", *save_args)
run_suite("com.tyranor.runtime.systemnnn.GameTextDecoderTest", *cfg_args)

story_roots = [g for g in (mng, dea_zh, dea_zht) if g.is_dir()]
env_root = os.environ.get("SYSTEMNNN_GAME_ROOT")
if env_root and Path(env_root).is_dir() and Path(env_root) not in story_roots:
    story_roots.append(Path(env_root))
if not story_roots:
    raise SystemExit("GameStoryControlsTest requires at least one installed game directory (0 checks)")
story_out = run_suite("com.tyranor.runtime.systemnnn.GameStoryControlsTest", *story_roots)
match = re.search(r"\bPASS\s+(\d+)\b", story_out)
if not match or int(match.group(1)) <= 0:
    raise SystemExit(f"GameStoryControlsTest executed 0 checks: {story_out!r}")

if mng.is_dir() and (mng / "sav/sysfile3.org").is_file():
    run_suite("com.tyranor.runtime.systemnnn.PcSystemSaveTest", mng, mng / "sav/sysfile3.org", 8192)

if ref_indices.is_file() and dea_zh.is_dir() and dea_zht.is_dir():
    run_suite("TextCodecTest", ref_indices, dea_parent)
]=]
                        "${EMBERFALL_JAVAC}"
                        "${EMBERFALL_JAVA}"
                        "${EMBERFALL_ANDROID_JAR}"
                        "${_emberfall_repo_root}"
                        "${CMAKE_CURRENT_BINARY_DIR}/jvm-test-classes"
                )
                set_tests_properties(emberfall-android-jvm PROPERTIES TIMEOUT 120)
            endif()
        endif()
    endif()
endif()
