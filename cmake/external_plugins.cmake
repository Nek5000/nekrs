set(FOUND_PLUGINS "")
set(UDF_RPATH "")
set(UDF_LIBS "")
set(UDF_INCLUDES "")

function(add_external_plugins)

file(GLOB plugins LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/plugins/*)
foreach(DIR ${plugins})
  get_filename_component(PLUGIN_NAME ${DIR} NAME)
  IF(IS_DIRECTORY ${DIR})
      message(NOTICE "-- Found plugin: ${PLUGIN_NAME}")
      list(APPEND FOUND_PLUGINS ${PLUGIN_NAME})
      add_subdirectory(${DIR})
  ELSE()
      CONTINUE()
  ENDIF()

  list(APPEND UDF_RPATH "${CMAKE_INSTALL_PREFIX}/3rd_party/${PLUGIN_NAME}/lib")
  list(APPEND UDF_LIBS "${CMAKE_INSTALL_PREFIX}/3rd_party/${PLUGIN_NAME}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}${PLUGIN_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
  list(APPEND UDF_INCLUDES "${CMAKE_INSTALL_PREFIX}/3rd_party/${PLUGIN_NAME}/include")
endforeach()

endfunction()
