cmake_minimum_required(VERSION 3.16)
project(ModbusIPE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH "/usr/lib/aarch64-linux-gnu/cmake")

find_package(Drogon REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MODBUS REQUIRED libmodbus)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(CJSON REQUIRED libcjson)

add_executable(modbusipe ModbusIPE-CPP.cpp)

target_include_directories(modbusipe PRIVATE
        ${MODBUS_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
        ${CJSON_INCLUDE_DIRS}
)

target_link_libraries(modbusipe PRIVATE
        Drogon::Drogon
        nlohmann_json::nlohmann_json
        ${MODBUS_LIBRARIES}
        ${CURL_LIBRARIES}
        ${CJSON_LIBRARIES}
        pthread
)
