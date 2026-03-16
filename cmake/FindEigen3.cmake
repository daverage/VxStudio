set(Eigen3_FOUND TRUE)
set(Eigen3_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../external/eigen3")
set(Eigen3_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(Eigen3_VERSION 3.4.0)
set(Eigen3_DEFINITIONS "")
set(Eigen3_LIBRARIES "")

add_library(Eigen3::Eigen INTERFACE IMPORTED)
set_target_properties(Eigen3::Eigen PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${Eigen3_INCLUDE_DIRS}")
