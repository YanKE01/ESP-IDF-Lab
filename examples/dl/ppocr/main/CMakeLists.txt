idf_component_register(SRCS "ppocr.cpp"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES pp_ocr_v6 esp_timer
                       EMBED_FILES "pp_ocr_v6.jpg")

if(NOT CONFIG_PP_OCR_V6_MODEL_IN_FLASH_RODATA)
    message(FATAL_ERROR "This example embeds the models in the application. Select models: pp_ocr_v6 -> model location -> flash_rodata.")
endif()
