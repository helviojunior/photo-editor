# Interface gráfica: Qt Quick (QML), como o clivcut. Os controllers C++
# (AppController = singleton App no QML) expõem o núcleo; o QML só desenha.

# Ícones de plataforma (.ico no exe, .icns no bundle) vêm do photoeditor_install
# (src/cmake/Packaging.cmake), desenhados a partir do logo.

qt_add_executable(photoeditor
    main.cpp
    AppController.h AppController.cpp
    EditorController.h EditorController.cpp
    MergeController.h MergeController.cpp
    InstagramController.h InstagramController.cpp
    PhotoListModel.h PhotoListModel.cpp
    ImageProviders.h ImageProviders.cpp
    SelectionChain.h
    Language.h Language.cpp
    Logging.h Logging.cpp
)
set_target_properties(photoeditor PROPERTIES OUTPUT_NAME PhotoEditor)

file(GLOB PHOTOEDITOR_QML RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    qml/*.qml qml/components/*.qml qml/pages/*.qml qml/editor/*.qml qml/dialogs/*.qml
    qml/dialogs/*.js qml/editor/*.js)
file(GLOB PHOTOEDITOR_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS icons/*.svg)
file(GLOB PHOTOEDITOR_ASSETS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    assets/*.png assets/logo/*.png fonts/*.ttf)
set_source_files_properties(qml/Theme.qml qml/Dialogs.qml qml/Messages.qml qml/Keymap.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Pastas qml/components, qml/pages... com qmldir próprio.
qt_policy(SET QTP0004 NEW)
qt_add_qml_module(photoeditor
    URI PhotoEditor
    VERSION 1.0
    # O executável também se chama PhotoEditor: o módulo vai para outra pasta.
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/PhotoEditor
    QML_FILES ${PHOTOEDITOR_QML}
    RESOURCES ${PHOTOEDITOR_ICONS} ${PHOTOEDITOR_ASSETS}
)

target_compile_definitions(photoeditor PRIVATE
    PHOTOEDITOR_VERSION="${PHOTOEDITOR_VERSION_LABEL}"
    PHOTOEDITOR_BRAND_NAME="${PHOTOEDITOR_BRAND_NAME}")
set_target_properties(photoeditor PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    # Nunca troque: é por ele que o macOS reconhece a versão nova como o mesmo programa.
    MACOSX_BUNDLE_GUI_IDENTIFIER br.com.photoeditor.desktop
    MACOSX_BUNDLE_BUNDLE_NAME PhotoEditor
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
target_link_libraries(photoeditor PRIVATE
    photoeditor_core
    Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::Concurrent)

# Traduções: o código está em inglês; photoeditor_en.ts guarda só os plurais.
# Atualize os .ts com: cmake --build build --target update_translations
qt_add_translations(TARGETS photoeditor
    SOURCE_TARGETS photoeditor photoeditor_core photoeditor_imaging
    TS_FILES ${CMAKE_SOURCE_DIR}/i18n/photoeditor_pt_BR.ts
    PLURALS_TS_FILE ${CMAKE_SOURCE_DIR}/i18n/photoeditor_en.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none)

if(EXISTS ${CMAKE_SOURCE_DIR}/src/cmake/Packaging.cmake)
    include(${CMAKE_SOURCE_DIR}/src/cmake/Packaging.cmake)
    if(COMMAND photoeditor_install)
        photoeditor_install(photoeditor)
    endif()
endif()
