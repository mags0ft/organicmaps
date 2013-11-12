CMDRES = $$system(python ../tools/autobuild/shader_preprocessor.py $$PWD/shaders shader_index.txt shader_def)
message($$CMDRES)

SOURCES += \
    $$DRAPE_DIR/data_buffer.cpp \
    $$DRAPE_DIR/binding_info.cpp \
    $$DRAPE_DIR/batcher.cpp \
    $$DRAPE_DIR/attribute_provider.cpp \
    $$DRAPE_DIR/vertex_array_buffer.cpp \
    $$DRAPE_DIR/uniform_value.cpp \
    $$DRAPE_DIR/texture.cpp \
    $$DRAPE_DIR/shader_reference.cpp \
    $$DRAPE_DIR/index_buffer.cpp \
    $$DRAPE_DIR/gpu_program.cpp \
    $$DRAPE_DIR/gpu_program_manager.cpp \
    $$DRAPE_DIR/glconstants.cpp \
    $$DRAPE_DIR/glstate.cpp \
    $$DRAPE_DIR/glbuffer.cpp \
    $$DRAPE_DIR/utils/list_generator.cpp \
    $$DRAPE_DIR/shader_def.cpp \
    $$DRAPE_DIR/glextensions_list.cpp

HEADERS += \
    $$DRAPE_DIR/data_buffer.hpp \
    $$DRAPE_DIR/binding_info.hpp \
    $$DRAPE_DIR/batcher.hpp \
    $$DRAPE_DIR/attribute_provider.hpp \
    $$DRAPE_DIR/vertex_array_buffer.hpp \
    $$DRAPE_DIR/uniform_value.hpp \
    $$DRAPE_DIR/texture.hpp \
    $$DRAPE_DIR/shader_reference.hpp \
    $$DRAPE_DIR/pointers.hpp \
    $$DRAPE_DIR/index_buffer.hpp \
    $$DRAPE_DIR/gpu_program.hpp \
    $$DRAPE_DIR/gpu_program_manager.hpp \
    $$DRAPE_DIR/glstate.hpp \
    $$DRAPE_DIR/glIncludes.hpp \
    $$DRAPE_DIR/glconstants.hpp \
    $$DRAPE_DIR/glfunctions.hpp \
    $$DRAPE_DIR/glbuffer.hpp \
    $$DRAPE_DIR/utils/list_generator.hpp \
    $$DRAPE_DIR/shader_def.hpp \
    $$DRAPE_DIR/glextensions_list.hpp
