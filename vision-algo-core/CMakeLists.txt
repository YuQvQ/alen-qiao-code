cmake_minimum_required(VERSION 3.20)
project(vision_algo CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 默认代码页不是 UTF-8，源文件含中文注释时会误识别为编译指令
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_definitions(VISION_ALGO_EXPORTS)

find_package(OpenCV REQUIRED)

# 海康 MVS SDK（若安装）—— 用于相机枚举与真实采集
set(MVS_ROOT "C:/Program Files (x86)/MVS" CACHE PATH "Hikvision MVS SDK root")
set(MVS_INCLUDE "${MVS_ROOT}/Development/Includes")
set(MVS_LIB_DIR "${MVS_ROOT}/Development/Libraries/win64")
set(MVS_HAS_SDK OFF)
if(EXISTS "${MVS_INCLUDE}/MvCameraControl.h" AND EXISTS "${MVS_LIB_DIR}/MvCameraControl.lib")
    set(MVS_HAS_SDK ON)
endif()

add_library(vision_algo SHARED
    src/algo_registry.cpp
    src/template_match.cpp
    src/image_source.cpp
    src/camera_source.cpp
    src/bin_threshold.cpp
    src/blob_analysis.cpp
    src/circle_find.cpp
    src/line_find.cpp
    src/text_output.cpp
    src/calibration_transform.cpp
    src/coordsys_convert.cpp
    src/position_tracking.cpp
    src/index_selector.cpp
    src/if_condition.cpp
    src/edge_finder.cpp
    src/line_fit.cpp
    src/circle_fit.cpp
    src/distance_measure.cpp
    src/angle_measure.cpp
    src/line_intersect.cpp
    src/line_angle.cpp
    src/rectangle_detect.cpp
    src/roi_crop.cpp
    src/morphology.cpp
    src/pixel_stats.cpp
    src/image_resize.cpp
    src/corner_detect.cpp
    src/image_display.cpp
    src/grayscale.cpp
    src/gaussian_blur.cpp
    src/median_blur.cpp
    src/hist_equalize.cpp
    src/invert.cpp
    src/canny_edge.cpp
    src/sobel_edge.cpp
    src/adaptive_threshold.cpp
    src/convert_color.cpp
    src/image_flip.cpp
    src/image_rotate.cpp
    src/find_contours.cpp
    src/convex_hull.cpp
    src/area_measure.cpp
    src/math_op.cpp
    src/math_func.cpp
    src/counter.cpp
    src/accumulator.cpp
    src/feature_match.cpp
    src/shape_match.cpp
    src/qrcode_detect.cpp
    src/color_detect.cpp
    src/dnn_classify.cpp
    src/dnn_detect.cpp
    src/dnn_detect_ort.cpp
    src/hik_camera.cpp
    src/bilateral_filter.cpp
    src/threshold.cpp
    src/min_max_loc.cpp
    src/distance_transform.cpp
    src/connected_components.cpp
    src/image_math.cpp
    src/for_loop.cpp
    src/ortho_router.cpp
    src/export.cpp
)

target_include_directories(vision_algo
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(MVS_HAS_SDK)
    target_include_directories(vision_algo PRIVATE ${MVS_INCLUDE})
    target_compile_definitions(vision_algo PRIVATE VZ_HAS_MVS_SDK=1)
    target_link_directories(vision_algo PRIVATE ${MVS_LIB_DIR})
    target_link_libraries(vision_algo PRIVATE MvCameraControl)
    # 延迟加载：不使用相机时无需 MvCameraControl.dll，DLL 可脱离海康运行时分发
    target_link_options(vision_algo PRIVATE "/DELAYLOAD:MvCameraControl.dll")
    target_link_libraries(vision_algo PRIVATE delayimp)
    message(STATUS "MVS SDK found: ${MVS_INCLUDE}")
else()
    message(STATUS "MVS SDK NOT found; camera enumeration/real grab disabled")
endif()

target_link_libraries(vision_algo PRIVATE ${OpenCV_LIBS})

# Python embed + onnxruntime（绕过 OpenCV 4.13 YOLO end-to-end bug）
set(PYTHON_ROOT "C:/Users/qiaoy/AppData/Local/Programs/Python/Python314" CACHE PATH "Python root")
target_include_directories(vision_algo PRIVATE "${PYTHON_ROOT}/include")
# numpy C headers (numpy 2.x: _core/include, older: core/include)
execute_process(COMMAND py -c "import numpy; print(numpy.get_include())" OUTPUT_VARIABLE NUMPY_INCLUDE OUTPUT_STRIP_TRAILING_WHITESPACE)
target_include_directories(vision_algo PRIVATE "${NUMPY_INCLUDE}")
target_link_libraries(vision_algo PRIVATE "${PYTHON_ROOT}/libs/python314.lib")
target_compile_definitions(vision_algo PRIVATE USE_PYTHON_ORT NPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION)

set_target_properties(vision_algo PROPERTIES
    OUTPUT_NAME vision_algo
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
    POSITION_INDEPENDENT_CODE ON
)

include(GNUInstallDirs)
install(TARGETS vision_algo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

option(VISION_ALGO_BUILD_TESTS "Build tests" OFF)
if(VISION_ALGO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "vision_algo configuration complete.")

# Standalone DnnDetect test
add_executable(standalone_dnn_test
    ${CMAKE_CURRENT_SOURCE_DIR}/standalone_dnn_test.cpp
)
target_include_directories(standalone_dnn_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(standalone_dnn_test PRIVATE vision_algo ${OpenCV_LIBS})
set_target_properties(standalone_dnn_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Raw OpenCV forward dump (no DLL)
add_executable(raw_opencv_test
    ${CMAKE_CURRENT_SOURCE_DIR}/raw_opencv_test.cpp
)
target_link_libraries(raw_opencv_test PRIVATE ${OpenCV_LIBS})
set_target_properties(raw_opencv_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Backend enumeration test
add_executable(backends_test
    ${CMAKE_CURRENT_SOURCE_DIR}/backends_test.cpp
)
target_link_libraries(backends_test PRIVATE ${OpenCV_LIBS})
set_target_properties(backends_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Python embed test (onnxruntime via Python)
set(PYTHON_ROOT "C:/Users/qiaoy/AppData/Local/Programs/Python/Python314" CACHE PATH "Python root")
add_executable(ort_embed_test
    ${CMAKE_CURRENT_SOURCE_DIR}/ort_embed_test.cpp
)
target_include_directories(ort_embed_test PRIVATE "${PYTHON_ROOT}/include")
target_link_libraries(ort_embed_test PRIVATE "${PYTHON_ROOT}/libs/python314.lib")
set_target_properties(ort_embed_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Standalone test for LineFind on sample images
add_executable(test_linefind_standalone
    D:/traework/TRAE_Result/6ab5d4cb64aae2f9939373bd/test_linefind_vz.cpp
)
target_include_directories(test_linefind_standalone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    C:/tools/opencv/build/include
)
target_link_libraries(test_linefind_standalone PRIVATE vision_algo ${OpenCV_LIBS})
set_target_properties(test_linefind_standalone PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)