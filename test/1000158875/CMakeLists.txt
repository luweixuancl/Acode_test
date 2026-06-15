cmake_minimum_required(VERSION 3.14)
project(GeomagXGBoost)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(xgboost REQUIRED)

add_executable(geomag_recon
    main.cpp
    geomag_reconstructor.cpp
    geomag_reconstructor.h
)

target_link_libraries(geomag_recon xgboost::xgboost)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(geomag_recon OpenMP::OpenMP_CXX)
endif()
