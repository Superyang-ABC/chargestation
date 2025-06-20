file(GLOB EASYLOGGER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/easylogger/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/demo/os/linux/easylogger/port/*.c
)

add_library(easylogger STATIC
    ${EASYLOGGER_SRC}
    easylogger/plugins/file/elog_file.c

)
target_include_directories(easylogger PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/demo/os/linux/easylogger/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/easylogger/inc 
    ${CMAKE_CURRENT_SOURCE_DIR}/easylogger/plugins/file
    
    
)