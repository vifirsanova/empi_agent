cmake_minimum_required(VERSION 3.15)
project(EMPI CXX)

# Требуем C++17 для std::filesystem
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Опционально: находим Python (для информационных целей)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    message(STATUS "Python found: ${Python3_EXECUTABLE}")
else()
    message(WARNING "Python not found - TextAnalyzer will not work at runtime")
endif()

# Основная библиотека агентов
add_library(empi_agents
    src/agents/TextAnalyzer.cpp
    src/core/UniversalAgent.cpp
    # src/core/MessageProtocol.cpp  # если есть, раскомментировать
)

# Подключаем зависимости
target_include_directories(empi_agents
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Используем find_package для nlohmann/json если он установлен,
# или FetchContent если нужна автоматическая загрузка
find_package(nlohmann_json 3.2.0 QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(empi_agents PRIVATE nlohmann_json::nlohmann_json)
    message(STATUS "Using system nlohmann/json")
else()
    # Альтернатива: скачать nlohmann/json автоматически
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

# Для std::filesystem на разных компиляторах
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    # GCC < 9 требует линковки с stdc++fs
    target_link_libraries(empi_agents PRIVATE stdc++fs)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    # Clang < 9 также может требовать
    target_link_libraries(empi_agents PRIVATE c++fs)
endif()

# Копируем Python интеграции в бинарную директорию при сборке
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/integrations)
add_custom_command(
    TARGET empi_agents POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/integrations/
        ${CMAKE_CURRENT_BINARY_DIR}/integrations/
    COMMENT "Copying Python integrations to binary directory"
)

# Установочная конфигурация (опционально)
install(TARGETS empi_agents
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY src/
    DESTINATION include/empi
    FILES_MATCHING PATTERN "*.hpp"
)

# Тесты (если есть)
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_text_analyzer tests/test_text_analyzer.cpp)
    target_link_libraries(test_text_analyzer empi_agents)
    add_test(NAME TextAnalyzerTest COMMAND test_text_analyzer)
endif()
