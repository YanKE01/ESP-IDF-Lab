idf_component_register(SRCS "usb_host_os_detect.c"
                       INCLUDE_DIRS ".")

# Wire our tusb_config.h and descriptor callbacks into the tinyusb component,
# the same way the win_usb example does.
idf_build_get_property(build_components BUILD_COMPONENTS)
if(tinyusb IN_LIST build_components)
    set(tinyusb_name tinyusb)            # Local component
else()
    set(tinyusb_name espressif__tinyusb) # Managed component
endif()

idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${PROJECT_DIR}/main")
target_sources(${tusb_lib} PUBLIC "${PROJECT_DIR}/main/usb_descriptors.c")

cmake_policy(SET CMP0079 NEW)
target_link_libraries(${tusb_lib} PRIVATE ${COMPONENT_LIB})
