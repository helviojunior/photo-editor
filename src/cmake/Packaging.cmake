# Instalação do app = o conteúdo do pacote. O `cmake --install` monta a árvore
# que vira o .tar.gz (Linux), o .app do .dmg (macOS) e a pasta do .msi
# (Windows); os builders em Docker (packaging/<plataforma>/build.sh) só
# acrescentam o que o CMake não sabe fazer num cross-compile.
#
# Uso (src/app/CMakeLists.txt):
#
#   include(${CMAKE_SOURCE_DIR}/src/cmake/Packaging.cmake)
#   photoeditor_install(photoeditor)
#
# Layout instalado (o mesmo que core/Paths.cpp procura):
#
#   Linux    bin/PhotoEditor              lib/ (Qt, ONNX Runtime; rpath $ORIGIN/../lib)
#            share/photoeditor/models/    share/photoeditor/instagram/
#            share/applications/photoeditor.desktop + ícone hicolor
#   Windows  PhotoEditor.exe e DLLs       models/   instagram/
#   macOS    PhotoEditor.app/Contents/{MacOS,Frameworks,PlugIns}
#            PhotoEditor.app/Contents/Resources/{models,instagram}
#
# O helper do Instagram fica em <recursos>/instagram/photoe_instagram.py e o
# Python dele em <recursos>/instagram/python/ (python.exe no Windows,
# bin/python3 no resto).

# Qt (bibliotecas, plugins, QML) copiado pelo próprio Qt na instalação. Num
# cross-compile não dá (o macdeployqt/windeployqt não rodam no Linux): lá o
# packaging/<plataforma>/bundle.py faz esse papel.
if(CMAKE_CROSSCOMPILING)
    set(_photoeditor_deploy_default OFF)
else()
    set(_photoeditor_deploy_default ON)
endif()
option(PHOTOEDITOR_DEPLOY "Copiar Qt e dependências para a instalação (qt deploy)" ${_photoeditor_deploy_default})

# Entradas preparadas pelo packaging/build.py antes do configure.
set(PHOTOEDITOR_ART_DIR "${CMAKE_SOURCE_DIR}/.cache/art" CACHE PATH
    "Ícones gerados do logo (packaging/build.py art): PhotoEditor.icns/.ico/.png")
set(PHOTOEDITOR_MODEL_DIR "${CMAKE_SOURCE_DIR}/.cache/models/sam2.1-hiera-tiny" CACHE PATH
    "Modelo SAM 2.1 (packaging/build.py model); vazio = pacote sem o modelo")
set(PHOTOEDITOR_INSTAGRAM_RUNTIME "" CACHE PATH
    "Runtime do helper do Instagram (packaging/build.py instagram-runtime --out <dir>), com <dir>/python")

set(PHOTOEDITOR_BUNDLE_ID "br.com.photoeditor.desktop") # nunca trocar (regra 18)

function(photoeditor_install target)
    get_target_property(_name ${target} OUTPUT_NAME)
    if(NOT _name)
        set(_name ${target})
    endif()

    if(APPLE)
        set(_contents "${_name}.app/Contents")
        set(_res "${_contents}/Resources")
        set(_libdir "${_contents}/Frameworks")
        set(_doc "${_res}")
    elseif(WIN32)
        set(_res ".")
        set(_libdir ".")
        set(_doc ".")
    else()
        set(_res "${CMAKE_INSTALL_DATADIR}/photoeditor")
        set(_libdir "${CMAKE_INSTALL_LIBDIR}")
        set(_doc "${CMAKE_INSTALL_DATADIR}/doc/photoeditor")
    endif()

    # --- ícone embutido no executável -----------------------------------
    # (Se o alvo já traz um .rc com ícone, ele vale: dois IDI_ICON1 não ligam.)
    get_target_property(_sources ${target} SOURCES)
    list(FILTER _sources INCLUDE REGEX "\\.rc$")
    if(WIN32 AND NOT _sources AND EXISTS "${PHOTOEDITOR_ART_DIR}/PhotoEditor.ico")
        set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_icon.rc")
        file(WRITE "${_rc}" "IDI_ICON1 ICON \"${PHOTOEDITOR_ART_DIR}/PhotoEditor.ico\"\n")
        target_sources(${target} PRIVATE "${_rc}")
    elseif(APPLE AND EXISTS "${PHOTOEDITOR_ART_DIR}/PhotoEditor.icns")
        set(_icns "${PHOTOEDITOR_ART_DIR}/PhotoEditor.icns")
        set_source_files_properties("${_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(${target} PRIVATE "${_icns}")
        set_target_properties(${target} PROPERTIES MACOSX_BUNDLE_ICON_FILE PhotoEditor.icns)
    endif()

    set_target_properties(${target} PROPERTIES
        WIN32_EXECUTABLE ON
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_GUI_IDENTIFIER ${PHOTOEDITOR_BUNDLE_ID}
        MACOSX_BUNDLE_BUNDLE_NAME ${_name}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    if(APPLE)
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
    elseif(UNIX)
        # Pacote relocável: as .so ficam em ../lib, em qualquer pasta.
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()

    if(WIN32)
        set(_bindir ".")
    else()
        set(_bindir "${CMAKE_INSTALL_BINDIR}")
    endif()
    install(TARGETS ${target}
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${_bindir})

    # --- ONNX Runtime (biblioteca compartilhada pronta) --------------------
    if(PHOTOEDITOR_WITH_ONNX AND PHOTOEDITOR_ORT_RUNTIME_FILES)
        foreach(_file IN LISTS PHOTOEDITOR_ORT_RUNTIME_FILES)
            get_filename_component(_fname "${_file}" NAME)
            if(UNIX AND NOT APPLE)
                # Só o SONAME (libonnxruntime.so.1), com o conteúdo do arquivo
                # real: os outros nomes são links que ninguém carrega. Com o
                # deploy do Qt ligado, ele mesmo já a copia (dependência do app).
                if(_fname STREQUAL "libonnxruntime.so.1" AND NOT PHOTOEDITOR_DEPLOY)
                    file(REAL_PATH "${_file}" _real)
                    install(FILES "${_real}" DESTINATION ${_libdir} RENAME ${_fname})
                endif()
            elseif(APPLE)
                # O install name é o arquivo versionado; os links não servem.
                if(NOT IS_SYMLINK "${_file}")
                    install(FILES "${_file}" DESTINATION ${_libdir})
                endif()
            else()
                install(FILES "${_file}" DESTINATION ${_libdir})
            endif()
        endforeach()
    endif()

    # --- modelo SAM e helper do Instagram ---------------------------------
    if(PHOTOEDITOR_MODEL_DIR)
        if(EXISTS "${PHOTOEDITOR_MODEL_DIR}")
            get_filename_component(_model "${PHOTOEDITOR_MODEL_DIR}" NAME)
            install(DIRECTORY "${PHOTOEDITOR_MODEL_DIR}/" DESTINATION "${_res}/models/${_model}"
                    FILES_MATCHING PATTERN "*.onnx" PATTERN "*.onnx_data")
        else()
            message(WARNING "Modelo SAM ausente em ${PHOTOEDITOR_MODEL_DIR}: o pacote sai sem as camadas "
                            "(rode `packaging/build.py model`).")
        endif()
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/src/instagram/photoe_instagram.py")
        install(FILES "${CMAKE_SOURCE_DIR}/src/instagram/photoe_instagram.py" DESTINATION "${_res}/instagram")
    endif()
    if(PHOTOEDITOR_INSTAGRAM_RUNTIME)
        if(NOT EXISTS "${PHOTOEDITOR_INSTAGRAM_RUNTIME}/python")
            message(FATAL_ERROR "PHOTOEDITOR_INSTAGRAM_RUNTIME=${PHOTOEDITOR_INSTAGRAM_RUNTIME} sem python/ "
                                "(rode `packaging/build.py instagram-runtime`).")
        endif()
        install(DIRECTORY "${PHOTOEDITOR_INSTAGRAM_RUNTIME}/python" DESTINATION "${_res}/instagram"
                USE_SOURCE_PERMISSIONS)
    endif()

    install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION "${_doc}")

    # --- integração com o desktop do Linux --------------------------------
    if(UNIX AND NOT APPLE)
        install(FILES "${CMAKE_SOURCE_DIR}/packaging/linux/photoeditor.desktop"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
        if(EXISTS "${PHOTOEDITOR_ART_DIR}/PhotoEditor.png")
            install(FILES "${PHOTOEDITOR_ART_DIR}/PhotoEditor.png"
                    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps" RENAME photoeditor.png)
        endif()
    endif()

    # --- Qt: bibliotecas, plugins e módulos QML ----------------------------
    # Linux: lib/, plugins/, qml/ e bin/qt.conf (deploy genérico do Qt 6.5+,
    # sem as bibliotecas do sistema). Windows: windeployqt. macOS: macdeployqt.
    if(PHOTOEDITOR_DEPLOY)
        # O script do Qt procura o executável em <prefixo>/${CMAKE_INSTALL_BINDIR}:
        # no Windows ele fica na raiz da instalação (ao lado das DLLs).
        if(WIN32)
            set(CMAKE_INSTALL_BINDIR ".")
        endif()
        qt_generate_deploy_qml_app_script(
            TARGET ${target}
            OUTPUT_SCRIPT _deploy
            NO_UNSUPPORTED_PLATFORM_ERROR
            DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM)
        install(SCRIPT "${_deploy}")
    endif()
endfunction()
