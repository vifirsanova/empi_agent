cmake_minimum_required(VERSION 3.14)
project(empi_agent VERSION 1.0.0 LANGUAGES CXX)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS "Build all tests" OFF)
option(BUILD_GUI "Build Qt GUI application" OFF)
option(BUILD_HTTP "Build HTTP server" ON)

find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Find Poppler
find_path(POPPLER_INCLUDE_DIR
    NAMES poppler-qt6.h poppler/qt6/poppler-qt6.h
    PATHS /usr/include /usr/local/include /opt/homebrew/include
)

find_library(POPPLER_LIBRARY
    NAMES poppler-qt6 poppler-qt5 poppler
    PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
)

if(POPPLER_INCLUDE_DIR AND POPPLER_LIBRARY)
    set(POPPLER_FOUND TRUE)
    set(POPPLER_INCLUDE_DIRS ${POPPLER_INCLUDE_DIR})
    set(POPPLER_LIBRARIES ${POPPLER_LIBRARY})
    message(STATUS "Found Poppler: ${POPPLER_LIBRARY}")
else()
    set(POPPLER_FOUND FALSE)
    message(WARNING "Poppler not found. PDF support will be disabled.")
endif()

# Qt components
if(BUILD_HTTP)
    find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Network)
elseif(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Concurrent WebEngineWidgets WebChannel Widgets)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core Concurrent)
endif()

# Llama integration
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

# ============================================================================
# CORE LIBRARY
# ============================================================================

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
    ${POPPLER_INCLUDE_DIRS}
)

target_link_libraries(empi_core PUBLIC
    nlohmann_json::nlohmann_json
    Threads::Threads
    Qt6::Core          
    Qt6::Concurrent 
)

if(POPPLER_FOUND)
    target_link_libraries(empi_core PUBLIC ${POPPLER_LIBRARIES})
endif()

if(LLAMA_LIBRARY)
    target_link_libraries(empi_core PUBLIC ${LLAMA_LIBRARY})
endif()

# Copy integrations and config
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

# ============================================================================
# CLI
# ============================================================================

add_executable(orchestrate_agents
    src/orchestrate/orchestrate_agents.cpp
)
target_link_libraries(orchestrate_agents PRIVATE empi_core)

# ============================================================================
# TESTS
# ============================================================================

if(BUILD_TESTS)
    enable_testing()
    
    add_executable(test_llm_client tests/test_llm_client.cpp)
    target_link_libraries(test_llm_client PRIVATE empi_core)
    add_test(NAME TestLLMClient COMMAND test_llm_client)

    add_executable(test_interface_generator tests/test_interface_generator.cpp)
    target_link_libraries(test_interface_generator PRIVATE empi_core)
    add_test(NAME TestInterfaceGenerator COMMAND test_interface_generator)

    add_executable(test_pipeline tests/test_pipeline.cpp)
    target_link_libraries(test_pipeline PRIVATE empi_core)
    add_test(NAME TestPipeline COMMAND test_pipeline)
    
    add_executable(test_default tests/test_default.cpp)
    target_link_libraries(test_default PRIVATE empi_core)
    add_test(NAME DefaultTest COMMAND test_default)

    add_executable(test_orchestration tests/test_orchestration.cpp)
    target_link_libraries(test_orchestration PRIVATE empi_core)
    add_test(NAME OrchestrationTest COMMAND test_orchestration)
endif()

# ============================================================================
# HTTP SERVER
# ============================================================================

if(BUILD_HTTP)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    
    add_executable(empi_http
        gui/main.cpp
        gui/DocumentParser.cpp
    )
    
    target_link_libraries(empi_http PRIVATE 
        empi_core 
        Qt6::Core
        Qt6::Concurrent
        Qt6::Network
        OpenSSL::Crypto
    )
    
    if(POPPLER_FOUND)
        target_link_libraries(empi_http PRIVATE ${POPPLER_LIBRARIES})
        target_include_directories(empi_http PRIVATE ${POPPLER_INCLUDE_DIRS})
    endif()
    
    target_include_directories(empi_http PRIVATE 
        ${CMAKE_SOURCE_DIR}/src
    )
    
    # === КОПИРОВАНИЕ WEB ФРОНТЕНДА (гарантированное) ===
    # Копируем при каждой конфигурации CMake
    file(COPY ${CMAKE_SOURCE_DIR}/gui/web DESTINATION ${CMAKE_BINARY_DIR}/gui)
    
    # Создаем директорию для CSS
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/gui/web/css)
    
    # Копируем CSS файл
    file(COPY ${CMAKE_SOURCE_DIR}/gui/web/css/style.css DESTINATION ${CMAKE_BINARY_DIR}/gui/web/css)
    
    # Copy web frontend (post-build - дублируем для надежности)
    add_custom_command(
        TARGET empi_http POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/gui/web
            ${CMAKE_BINARY_DIR}/gui/web
        COMMENT "Copying web frontend to build directory"
    )
    
    # Copy CSS (post-build)
    add_custom_command(
        TARGET empi_http POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gui/web/css
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/gui/web/css/style.css
            ${CMAKE_BINARY_DIR}/gui/web/css/style.css
        COMMENT "Copying CSS files"
    )
    
    # Installation
    install(TARGETS empi_http
        RUNTIME DESTINATION bin
    )
    
    install(DIRECTORY ${CMAKE_BINARY_DIR}/gui/web
        DESTINATION share/empi_agent
    )
    
    install(DIRECTORY ${CMAKE_BINARY_DIR}/config
        DESTINATION share/empi_agent
    )
endif()

# ============================================================================
# GUI (optional)
# ============================================================================

if(BUILD_GUI)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    
    add_executable(empi_gui
        gui/main_gui.cpp
        gui/DocumentParser.cpp
    )
    
    target_link_libraries(empi_gui PRIVATE 
        empi_core 
        Qt6::WebEngineWidgets 
        Qt6::WebChannel
        Qt6::Widgets
        Qt6::Core
    )
    
    if(POPPLER_FOUND)
        target_link_libraries(empi_gui PRIVATE ${POPPLER_LIBRARIES})
        target_include_directories(empi_gui PRIVATE ${POPPLER_INCLUDE_DIRS})
    endif()
    
    target_include_directories(empi_gui PRIVATE 
        ${CMAKE_SOURCE_DIR}/src
    )
    
    # Copy web frontend for GUI
    add_custom_command(
        TARGET empi_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/gui/web
            ${CMAKE_BINARY_DIR}/gui/web
        COMMENT "Copying web frontend to build directory"
    )
    
    if(APPLE)
        set_target_properties(empi_gui PROPERTIES
            MACOSX_BUNDLE TRUE
        )
    endif()
    
    if(WIN32)
        set_target_properties(empi_gui PROPERTIES
            WIN32_EXECUTABLE TRUE
        )
    endif()
    
    install(TARGETS empi_gui
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION .
    )
    
    install(DIRECTORY ${CMAKE_BINARY_DIR}/gui/web
        DESTINATION share/empi_agent
    )
    
    install(DIRECTORY ${CMAKE_BINARY_DIR}/config
        DESTINATION share/empi_agent
    )
endif()

# ============================================================================
# INSTALL
# ============================================================================

install(TARGETS empi_core 
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(TARGETS orchestrate_agents
    RUNTIME DESTINATION bin
)
