idf_component_register(SRC_DIRS "." "./lock"
                       PRIV_INCLUDE_DIRS "." "./lock" "${ESP_MATTER_PATH}/examples/common/utils")
# No REQUIRES on purpose: `main` then implicitly depends on every component, which is
# how it reaches esp_matter, connectedhomeip and the ultrawidelock reader stack.
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
