idf_build_get_property(component_targets __COMPONENT_TARGETS)
if("___idf_espressif__esp-dl" IN_LIST component_targets)
    idf_component_get_property(espdl_dir espressif__esp-dl COMPONENT_DIR)
elseif("___idf_esp-dl" IN_LIST component_targets)
    idf_component_get_property(espdl_dir esp-dl COMPONENT_DIR)
endif()
set(cmake_dir ${espdl_dir}/fbs_loader/cmake)
include(${cmake_dir}/utilities.cmake)

idf_component_register(SRC_DIRS "." INCLUDE_DIRS "." REQUIRES esp-dl)

if(NOT IDF_TARGET STREQUAL "esp32p4" AND NOT IDF_TARGET STREQUAL "esp32s31")
    message(FATAL_ERROR "The bundled PP-OCRv6 models support ESP32-P4 and ESP32-S31.")
endif()

set(models_dir ${PROJECT_DIR}/main/models)
set(models ${models_dir}/pp_ocr_v6_det_s8.espdl
           ${models_dir}/pp_ocr_v6_rec_s8.espdl)
set(packed_model ${BUILD_DIR}/espdl_models/pp_ocr_v6.espdl)
set(pack_model_exe ${espdl_dir}/fbs_loader/pack_espdl_models.py)

file(MAKE_DIRECTORY ${BUILD_DIR}/espdl_models)
add_custom_command(
    OUTPUT ${packed_model}
    COMMENT "Pack local PP-OCRv6 DET_S8 and REC_S8 models..."
    COMMAND "${PYTHON}" ${pack_model_exe} --model_path ${models} --out_file ${packed_model}
    DEPENDS ${models} ${pack_model_exe}
    VERBATIM)

target_add_aligned_binary_data(${COMPONENT_LIB} ${packed_model} BINARY)
