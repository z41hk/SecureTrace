cmake_minimum_required(VERSION 3.16)
project(SecureTrace VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(OpenSSL REQUIRED)

set(MYSQL_CONNECTOR_DIR "" CACHE PATH "MySQL Connector/C++ install root")

set(MYSQL_SEARCH_INCLUDE
    /usr/include
    /usr/local/include
    /usr/include/mysql-cppconn-8
    "C:/Program Files/MySQL/MySQL Connector C++ 9.3/include"
    "C:/Program Files/MySQL/MySQL Connector C++ 9.2/include"
    "C:/Program Files/MySQL/MySQL Connector C++ 9.1/include"
    "C:/Program Files/MySQL/MySQL Connector C++ 9.0/include"
    "C:/Program Files/MySQL/MySQL Connector C++ 8.4/include"
    "C:/Program Files/MySQL/MySQL Connector C++ 8.0/include"
    "C:/Program Files/MySQL/Connector C++ 8.0/include"
)
set(MYSQL_SEARCH_LIB
    /usr/lib
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib
    "C:/Program Files/MySQL/MySQL Connector C++ 9.3/lib64/vs14"
    "C:/Program Files/MySQL/MySQL Connector C++ 9.3/lib64"
    "C:/Program Files/MySQL/MySQL Connector C++ 9.2/lib64/vs14"
    "C:/Program Files/MySQL/MySQL Connector C++ 9.2/lib64"
    "C:/Program Files/MySQL/MySQL Connector C++ 8.4/lib64/vs14"
    "C:/Program Files/MySQL/MySQL Connector C++ 8.4/lib64"
    "C:/Program Files/MySQL/MySQL Connector C++ 8.0/lib64/vs14"
    "C:/Program Files/MySQL/MySQL Connector C++ 8.0/lib64"
    "C:/Program Files/MySQL/Connector C++ 8.0/lib64/vs14"
    "C:/Program Files/MySQL/Connector C++ 8.0/lib64"
)

if(MYSQL_CONNECTOR_DIR)
    list(PREPEND MYSQL_SEARCH_INCLUDE "${MYSQL_CONNECTOR_DIR}/include")
    list(PREPEND MYSQL_SEARCH_LIB
        "${MYSQL_CONNECTOR_DIR}/lib64/vs14"
        "${MYSQL_CONNECTOR_DIR}/lib64"
        "${MYSQL_CONNECTOR_DIR}/lib/vs14"
        "${MYSQL_CONNECTOR_DIR}/lib")
endif()

find_path(MYSQLCPPCONN_INCLUDE_DIR
    NAMES mysql_driver.h
    PATHS ${MYSQL_SEARCH_INCLUDE}
    PATH_SUFFIXES jdbc mysql-cppconn-8/jdbc mysql-cppconn/jdbc
)

find_library(MYSQLCPPCONN_LIBRARY
    NAMES mysqlcppconn mysqlcppconn-static mysqlcppconn8
    PATHS ${MYSQL_SEARCH_LIB}
)

if(NOT MYSQLCPPCONN_INCLUDE_DIR OR NOT MYSQLCPPCONN_LIBRARY)
    message(FATAL_ERROR
        "MySQL Connector/C++ (JDBC/classic) not found.\n"
        "Install the connector and pass -DMYSQL_CONNECTOR_DIR=\"<install root>\".")
endif()

message(STATUS "MySQL Connector include: ${MYSQLCPPCONN_INCLUDE_DIR}")
message(STATUS "MySQL Connector library: ${MYSQLCPPCONN_LIBRARY}")

add_executable(securetrace
    src/main.cpp
    src/Config.cpp
    src/Logger.cpp
    src/DatabaseConnection.cpp
    src/ConsoleUI.cpp
    src/AuditLog.cpp
    src/Users.cpp
    src/Cases.cpp
    src/Evidence.cpp
    src/Investigators.cpp
    src/CustodyTransfers.cpp
    src/Reports.cpp
    src/Analysis.cpp
    src/Backup.cpp
)

target_include_directories(securetrace PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MYSQLCPPCONN_INCLUDE_DIR}
)

target_link_libraries(securetrace PRIVATE
    ${MYSQLCPPCONN_LIBRARY}
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_executable(securetrace_tests
    tests/security_tests.cpp
    src/Config.cpp
    src/Logger.cpp
    src/DatabaseConnection.cpp
    src/ConsoleUI.cpp
    src/AuditLog.cpp
    src/Users.cpp
)

add_executable(securetrace_feature_tests
    tests/feature_tests.cpp
    src/Config.cpp
    src/Logger.cpp
    src/DatabaseConnection.cpp
    src/ConsoleUI.cpp
    src/AuditLog.cpp
    src/Users.cpp
)

target_include_directories(securetrace_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MYSQLCPPCONN_INCLUDE_DIR}
)

target_link_libraries(securetrace_tests PRIVATE
    ${MYSQLCPPCONN_LIBRARY}
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_include_directories(securetrace_feature_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MYSQLCPPCONN_INCLUDE_DIR}
)

target_link_libraries(securetrace_feature_tests PRIVATE
    ${MYSQLCPPCONN_LIBRARY}
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(securetrace PRIVATE ws2_32)
    target_link_libraries(securetrace_tests PRIVATE ws2_32)
    target_link_libraries(securetrace_feature_tests PRIVATE ws2_32)

    add_executable(securetrace_gui WIN32
        src/gui_main.cpp
        src/GuiApp.cpp
        src/Config.cpp
        src/Logger.cpp
        src/DatabaseConnection.cpp
        src/ConsoleUI.cpp
        src/AuditLog.cpp
        src/Users.cpp
        src/Backup.cpp
    )
    target_compile_definitions(securetrace_gui PRIVATE UNICODE _UNICODE)
    target_include_directories(securetrace_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${MYSQLCPPCONN_INCLUDE_DIR}
    )
    target_link_libraries(securetrace_gui PRIVATE
        ${MYSQLCPPCONN_LIBRARY}
        OpenSSL::SSL
        OpenSSL::Crypto
        ws2_32
        comctl32
        comdlg32
    )
    add_custom_command(TARGET securetrace_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:securetrace_gui>/config"
            "$<TARGET_FILE_DIR:securetrace_gui>/logs"
            "$<TARGET_FILE_DIR:securetrace_gui>/reports"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/config.example.ini"
            "$<TARGET_FILE_DIR:securetrace_gui>/config/config.example.ini"
    )
endif()

add_custom_command(TARGET securetrace POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:securetrace>/config"
        "$<TARGET_FILE_DIR:securetrace>/logs"
        "$<TARGET_FILE_DIR:securetrace>/reports"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/config.example.ini"
        "$<TARGET_FILE_DIR:securetrace>/config/config.example.ini"
)
