idf_component_register(SRCS "main.c" "app_usb/hid_keyboard.c" INCLUDE_DIRS "." "app_usb/include")

# Determine whether TinyUSB comes from the component registry or a local component.
idf_build_get_property(build_components BUILD_COMPONENTS)
if(tinyusb IN_LIST build_components)
    set(tinyusb_name tinyusb)
else()
    set(tinyusb_name espressif__tinyusb)
endif()

idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${COMPONENT_DIR}/tusb")
target_sources(${tusb_lib} PRIVATE "${COMPONENT_DIR}/tusb/usb_descriptors.c")

cmake_policy(SET CMP0079 NEW)
target_link_libraries(${tusb_lib} PRIVATE ${COMPONENT_LIB})
