# Top level directory has the include files


ADD_EXECUTABLE( bpml BPMaximalMatching.cpp )
ADD_EXECUTABLE( bpmm BPMaximumMatching.cpp )
ADD_EXECUTABLE( awpm ApproxWeightPerfectMatching.cpp )


TARGET_LINK_LIBRARIES( bpml CombBLAS)
TARGET_LINK_LIBRARIES( bpmm CombBLAS )
TARGET_LINK_LIBRARIES( awpm CombBLAS )

ADD_TEST(NAME BPML_Test COMMAND ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 4 $<TARGET_FILE:bpml> er 12 8  )
ADD_TEST(NAME BPMM_Test COMMAND ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 4 $<TARGET_FILE:bpmm> er 12 8  )


# Set CMake Install directories for bipartitematchings
set(bipartitematchings_INSTALL_LIB_DIR lib CACHE PATH "Installation path for bipartitematchings library files")
set(bipartitematchings_INSTALL_BIN_DIR bin CACHE PATH "Installation path for bipartitematchings binary files")
set(bipartitematchings_INSTALL_INCLUDE_DIR include/bipartitematchings/ CACHE PATH "Installation path for header files")

if(WIN32 AND NOT CYGWIN)
  set(DEF_INSTALL_CMAKE_DIR CMake)
else()
  set(DEF_INSTALL_CMAKE_DIR lib/CMake/bipartitematchings)
endif()

set(bipartitematchings_INSTALL_CMAKE_DIR ${DEF_INSTALL_CMAKE_DIR} CACHE PATH "Installation path for bipartitematchings CMake files")



add_library(bipartitematchings INTERFACE)
target_include_directories(bipartitematchings INTERFACE 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/bipartitematchings>

)


# install(
#   TARGETS bipartitematchings 
#   EXPORT bipartitematchingsTargets
#   RUNTIME DESTINATION ${bipartitematchings_INSTALL_BIN_DIR}
#   LIBRARY DESTINATION ${bipartitematchings_INSTALL_LIB_DIR}
#   ARCHIVE DESTINATION ${bipartitematchings_INSTALL_LIB_DIR}
# )

install(
  FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/ApproxWeightPerfectMatching.h
  ${CMAKE_CURRENT_SOURCE_DIR}/BPMaximalMatching.h
  ${CMAKE_CURRENT_SOURCE_DIR}/BPMaximumMatching.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Utility.h
  ${CMAKE_CURRENT_SOURCE_DIR}/MatchingDefs.h
  DESTINATION ${bipartitematchings_INSTALL_INCLUDE_DIR}
)




# # Create/Install bipartitematchingsConfig.cmake and related files.
# #
# # The below section is modified from KitWare CMake wiki on "How to create a ProjectConfig.cmake file".
# # See: https://gitlab.kitware.com/cmake/community/wikis/doc/tutorials/How-to-create-a-ProjectConfig.cmake-file
# export(TARGETS bipartitematchings FILE ${bipartitematchings_BINARY_DIR}/bipartitematchingsConfig.cmake)
# export(PACKAGE bipartitematchings)
# # Add bipartitematchingsConfigVersion.cmake
# configure_file(bipartitematchingsConfigVersion.cmake.in
#   "${bipartitematchings_BINARY_DIR}/bipartitematchingsConfigVersion.cmake" @ONLY)
# # Add bipartitematchingsConfig.cmake for build tree and for install tree.
# # -- Note: CONFIG_INCLUDE_DIRS is the location of the include directories.
# # -- Note: bipartitematchings_CMAKE_DIR is set to the path of bipartitematchingsConfig.cmake when it is called.
# set(CONFIG_INCLUDE_DIRS "${bipartitematchings_SOURCE_DIR}/include"
#                         "${bipartitematchings_SOURCE_DIR}/bipartitematchings")
# configure_file(bipartitematchingsConfig.cmake.in
#   "${bipartitematchings_BINARY_DIR}/bipartitematchingsConfig.cmake" @ONLY)
# file(RELATIVE_PATH RELATIVE_INCLUDE_DIR "/${bipartitematchings_INSTALL_CMAKE_DIR}"
#   "/${bipartitematchings_INSTALL_INCLUDE_DIR}")
# set(CONFIG_INCLUDE_DIRS "\${bipartitematchings_CMAKE_DIR}/${RELATIVE_INCLUDE_DIR}")
# configure_file(bipartitematchingsConfig.cmake.in
#   "${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/bipartitematchingsConfig.cmake" @ONLY)
# # Install the bipartitematchingsConfig.cmake and bipartitematchingsConfigVersion.cmake
# install(FILES
#   "${bipartitematchings_BINARY_DIR}/bipartitematchingsConfigVersion.cmake"
#   "${bipartitematchings_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/bipartitematchingsConfig.cmake"
#   DESTINATION "${bipartitematchings_INSTALL_CMAKE_DIR}" COMPONENT dev)
# # Install the export set for use with the install-tree
# install(EXPORT bipartitematchingsTargets DESTINATION
#   "${bipartitematchings_INSTALL_CMAKE_DIR}" COMPONENT dev)
# # Configure and install pkg-config file.
# configure_file(bipartitematchings.pc.in
#   "${bipartitematchings_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/bipartitematchings.pc" @ONLY)
# install(FILES
#   "${bipartitematchings_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/bipartitematchings.pc"
#   DESTINATION "${bipartitematchings_INSTALL_LIB_DIR}/pkgconfig"
#   )
