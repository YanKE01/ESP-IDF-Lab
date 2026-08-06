idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES cherryusb)

idf_component_get_property(cherryusb_lib cherryusb COMPONENT_LIB)

# Pull in CherryUSB's ESP-IDF FATFS glue. Without this, the weak empty
# usbh_msc_run() in usbh_msc.c satisfies the linker and /usb is never mounted.
if(CONFIG_CHERRYUSB_HOST_MSC)
    target_link_libraries(${cherryusb_lib} INTERFACE "-u msc_host_vfs_register")
endif()
