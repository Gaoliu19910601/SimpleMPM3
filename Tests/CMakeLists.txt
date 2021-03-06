#===================================
#
# Tests Project
#
#===================================
add_executable(
    Tests 
    # precompiled header
    Tests_pcp.h Tests_pcp.cpp
    # entry
    Tests_main.cpp
    # header
    test_utilities.h
    test_material_models.h
    # unit tests
    test_stack_and_link_list.cpp
    test_model_container.cpp
    )

target_include_directories(
    Tests PUBLIC
    ${COMMONUTILS_INCLUDE_DIR}
    ${FILEUTILS_INCLUDE_DIR}
    ${MEMORYUTILS_INCLUDE_DIR}
    ${MATERIALMODELS_INCLUDE_DIR}
    ${GEOMETRY_INCLUDE_DIR}
    ${SIMULATIONS_INCLUDE_DIR}
    ${RESULTVIEWERQT_INCLUDE_DIR}
    )

#link_directories(${GLFW_LIBRARIES_DIR})
target_link_libraries(
    Tests PUBLIC
    MemoryUtils
    MaterialModels
    Geometry
    #Simulations
    )

if (WIN32)
    # supress warning for certain functions in visual studio
    target_compile_definitions(Tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    # use precompiled header
    target_compile_options(Tests PRIVATE -YcTests_pcp.h)
endif()
