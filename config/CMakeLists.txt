add_executable(price_table_test
    price_table.cpp
    price_table_test.cpp
)
target_include_directories(price_table_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../tools/nlohmann_json/single_include
)
target_link_libraries(price_table_test)