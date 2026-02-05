cmake_minimum_required(VERSION 3.15)
project(EMPI CXX)

# ==================== ОСНОВНЫЕ НАСТРОЙКИ ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== LLAMA INTEGRATION ====================
option(EMPI_BUILD_LLAMA_TOOLS "Build llama-based dialog recorder" ON)

if(EMPI_BUILD_LLAMA_TOOLS)
    # Отключаем всё лишнее в форке
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE) 
    set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
    
    # Добавляем форк как подпроект
    add_subdirectory(llama-dynamic-context)
    
    message(STATUS "Llama integration enabled")
else()
    message(STATUS "Llama integration disabled")
endif()

# ==================== PYTHON CONFIG ====================
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    message(STATUS "Python found: ${Python3_EXECUTABLE}")
else()
    message(WARNING "Python not found - TextAnalyzer will not work at runtime")
endif()

# ==================== EMPI CORE LIBRARY ====================
add_library(empi_agents
    src/agents/TextAnalyzer.cpp
    src/core/UniversalAgent.cpp
)

target_include_directories(empi_agents
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

# ==================== JSON DEPENDENCY ====================
find_package(nlohmann_json 3.2.0 QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(empi_agents PRIVATE nlohmann_json::nlohmann_json)
    message(STATUS "Using system nlohmann/json")
else()
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
    target_link_libraries(empi_agents PRIVATE nlohmann_json)
    message(STATUS "Using downloaded nlohmann/json")
endif()

# ==================== COMPILER-SPECIFIC SETTINGS ====================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(empi_agents PRIVATE stdc++fs)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(empi_agents PRIVATE c++fs)
endif()

# ==================== PYTHON INTEGRATIONS COPY ====================
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/integrations)
add_custom_command(
    TARGET empi_agents POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/integrations/
        ${CMAKE_CURRENT_BINARY_DIR}/integrations/
    COMMENT "Copying Python integrations to binary directory"
)

# ==================== DIALOG RECORDER (ЕДИНСТВЕННЫЙ ИНСТРУМЕНТ) ====================
if(EMPI_BUILD_LLAMA_TOOLS AND TARGET llama)
    # Проверяем существование файла
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/dialog_recorder.cpp)
        add_executable(empi_dialog_recorder
            tools/dialog_recorder.cpp
        )
        target_link_libraries(empi_dialog_recorder PRIVATE llama)
        target_include_directories(empi_dialog_recorder PRIVATE
            llama-dynamic-context/include
        )
        message(STATUS "Building empi_dialog_recorder")
    else()
        # Создаем заглушку если файла нет
        message(STATUS "Creating dialog_recorder.cpp stub")
        file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/tools/dialog_recorder.cpp
            "// Stub for dialog_recorder - replace with actual implementation\n"
            "#include <iostream>\n"
            "int main() {\n"
            "    std::cout << \"Dialog recorder stub. Create tools/dialog_recorder.cpp with actual code.\\n\";\n"
            "    return 0;\n"
            "}\n"
        )
        add_executable(empi_dialog_recorder
            tools/dialog_recorder.cpp
        )
        target_link_libraries(empi_dialog_recorder PRIVATE llama)
        target_include_directories(empi_dialog_recorder PRIVATE
            llama-dynamic-context/include
        )
    endif()
elseif(EMPI_BUILD_LLAMA_TOOLS)
    message(WARNING "Llama integration requested but llama target not found")
endif()

# ==================== TESTS ====================
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_text_analyzer tests/test_text_analyzer.cpp)
    target_link_libraries(test_text_analyzer empi_agents)
    add_test(NAME TextAnalyzerTest COMMAND test_text_analyzer)
endif()

# ==================== INSTALLATION ====================
install(TARGETS empi_agents
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY src/
    DESTINATION include/empi
    FILES_MATCHING PATTERN "*.hpp"
)
