idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." "../../jarvis_s3")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
