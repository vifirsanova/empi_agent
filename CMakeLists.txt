cmake_minimum_required(VERSION 3.14)
project(empi_agent VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

if(EXISTS "${CMAKE_SOURCE_DIR}/llama-dynamic-context/CMakeLists.txt")
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
    add_subdirectory(llama-dynamic-context llama_build)
    set(LLAMA_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/llama-dynamic-context/include")
    set(LLAMA_LIBRARY llama)
    message(STATUS "Llama integration enabled")
else()
    message(WARNING "llama-dynamic-context not found — run: git submodule update --init")
    set(LLAMA_INCLUDE_DIR "")
    set(LLAMA_LIBRARY "")
endif()

add_library(empi_core STATIC
    src/core/UniversalAgent.cpp
    src/core/LLMClient.cpp
    src/agents/TextAnalyzer.cpp
    src/agents/FeedbackAgent.cpp
    src/agents/InterfaceGenerator.cpp
)

target_include_directories(empi_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${LLAMA_INCLUDE_DIR}
)

target_link_libraries(empi_core PUBLIC
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(LLAMA_LIBRARY)
    target_link_libraries(empi_core PUBLIC ${LLAMA_LIBRARY})
endif()

add_executable(orchestrate_agents
    src/orchestrate/orchestrate_agents.cpp
)
target_link_libraries(orchestrate_agents PRIVATE empi_core)

option(BUILD_GUI "Build Qt GUI application" OFF)
if(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets)
    find_package(toml11 REQUIRED)

    add_executable(empi_gui
        gui/main.cpp
        gui/MainWindow.cpp
    )
    target_link_libraries(empi_gui PRIVATE
        empi_core
        Qt6::Widgets
        Qt6::WebEngineWidgets
        toml11::toml11
    )
    target_include_directories(empi_gui PRIVATE ${CMAKE_SOURCE_DIR}/src)
endif()

add_executable(test_llm_client tests/test_llm_client.cpp)
target_link_libraries(test_llm_client PRIVATE empi_core)

option(BUILD_TESTS "Build all tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_default tests/test_default.cpp)
    target_link_libraries(test_default PRIVATE empi_core)
    add_test(NAME DefaultTest COMMAND test_default)

    add_executable(test_orchestration tests/test_orchestration.cpp)
    target_link_libraries(test_orchestration PRIVATE empi_core)
    add_test(NAME OrchestrationTest COMMAND test_orchestration)
endif()

add_custom_command(
    TARGET empi_core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/integrations
        ${CMAKE_BINARY_DIR}/integrations
    COMMENT "Copying integrations/ to build directory"
)

add_custom_command(
    TARGET empi_core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/config
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/agent_config.toml
        ${CMAKE_BINARY_DIR}/config/agent_config.toml
    COMMENT "Copying config to build directory"
)

add_executable(test_interface_generator tests/test_interface_generator.cpp)
target_link_libraries(test_interface_generator PRIVATE empi_core)

add_executable(test_pipeline tests/test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE empi_core)
