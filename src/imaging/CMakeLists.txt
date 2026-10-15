# Processamento de imagem: revelação, crop, Auto, Merge, seleção (SAM),
# leitura/gravação de JPEG e EXIF. Sem banco e sem interface — só OpenCV e o
# QtCore (strings, JSON, hash).
add_library(photoeditor_imaging STATIC
    Develop.h Develop.cpp
    Crop.h Crop.cpp
    SettingsHash.h SettingsHash.cpp
    Exif.h Exif.cpp
    ImageIO.h ImageIO.cpp
    Publish.h Publish.cpp
    Auto.h Auto.cpp
    Merge.h Merge.cpp
    Segment.h Segment.cpp
    Mask.h Mask.cpp
)
target_include_directories(photoeditor_imaging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(photoeditor_imaging PUBLIC Qt6::Core ${OpenCV_LIBS})
target_compile_definitions(photoeditor_imaging PRIVATE
    PHOTOEDITOR_BRAND_NAME="${PHOTOEDITOR_BRAND_NAME}"
    PHOTOEDITOR_BRAND_URL="${PHOTOEDITOR_BRAND_URL}"
    PHOTOEDITOR_VERSION="${PHOTOEDITOR_VERSION_LABEL}")
if(PHOTOEDITOR_WITH_ONNX)
    target_link_libraries(photoeditor_imaging PUBLIC photoeditor_onnxruntime)
    target_compile_definitions(photoeditor_imaging PUBLIC PHOTOEDITOR_HAVE_ONNX)
endif()
