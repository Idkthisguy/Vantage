file(GLOB_RECURSE ALL_SRCS 
    "${COMPONENT_DIR}/*.c" 
    "${COMPONENT_DIR}/*.cpp"
)

set(FILTERED_SRCS "wm.c" "")
foreach(SRC_PATH ${ALL_SRCS})
    if(NOT SRC_PATH MATCHES "(/managed_components/|/components/|/build/|/examples/|/doc/)")
        list(APPEND FILTERED_SRCS "${SRC_PATH}")
    endif()
endforeach()

file(GLOB_RECURSE ALL_FILES "${COMPONENT_DIR}/*")

set(DYNAMIC_INC_DIRS ".")
foreach(FILE_PATH ${ALL_FILES})
    if(NOT FILE_PATH MATCHES "(/managed_components/|/components/|/build/|/examples/|/doc/)")
        get_filename_component(DIR_PATH "${FILE_PATH}" DIRECTORY)
        list(APPEND DYNAMIC_INC_DIRS "${DIR_PATH}")
    endif()
endforeach()

list(REMOVE_DUPLICATES DYNAMIC_INC_DIRS)

idf_component_register(
    SRCS 
        ${FILTERED_SRCS}
    INCLUDE_DIRS 
        ${DYNAMIC_INC_DIRS}
    REQUIRES 
        LovyanGFX 
        lvgl 
        fatfs 
        sdmmc 
        driver 
        vfs 
        esp_http_client 
        app_update 
        esp_wifi 
        nvs_flash
        mbedtls
        cjson
        tinyusb
        elf_loader
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=cpp>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-cpp>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=type-limits>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-type-limits>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=format>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=format-truncation>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-format-truncation>
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    -Dtdefl_compress=vantage_tdefl_compress
    -Dtdefl_compress_buffer=vantage_tdefl_compress_buffer
    -Dtdefl_init=vantage_tdefl_init
    -Dtdefl_get_prev_return_status=vantage_tdefl_get_prev_return_status
    -Dtdefl_get_adler32=vantage_tdefl_get_adler32
    -Dtdefl_compress_mem_to_output=vantage_tdefl_compress_mem_to_output
    -Dtdefl_compress_mem_to_heap=vantage_tdefl_compress_mem_to_heap
    -Dtdefl_compress_mem_to_mem=vantage_tdefl_compress_mem_to_mem
    -Dtdefl_write_image_to_png_file_in_memory_ex=vantage_tdefl_write_image_to_png_file_in_memory_ex
    -Dtdefl_write_image_to_png_file_in_memory=vantage_tdefl_write_image_to_png_file_in_memory
)