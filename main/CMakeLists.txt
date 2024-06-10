idf_component_register(
    SRCS "image_builder.c" "webserver.c" "wifi.c" "lodepng.c" "main.c" "printer.c"
    INCLUDE_DIRS "."
)

spiffs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)