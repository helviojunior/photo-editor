cmake_minimum_required(VERSION 3.24)

# Versão: quem manda é o GitHub (regra 17). O packaging/build.py grava o VERSION da
# raiz (ignorado pelo git) a partir da última Release; o rótulo completo
# (1.2.3-dev+abc1234) vai para a interface e o número puro para o bundle/MSI.
set(PHOTOEDITOR_VERSION_LABEL "0.0.0-dev")
if(EXISTS ${CMAKE_SOURCE_DIR}/VERSION)
    file(STRINGS ${CMAKE_SOURCE_DIR}/VERSION PHOTOEDITOR_VERSION_LABEL LIMIT_COUNT 1)
endif()
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" PHOTOEDITOR_VERSION_NUMBER "${PHOTOEDITOR_VERSION_LABEL}")
if(NOT PHOTOEDITOR_VERSION_NUMBER)
    set(PHOTOEDITOR_VERSION_NUMBER 0.0.0)
endif()

project(photoeditor
    VERSION ${PHOTOEDITOR_VERSION_NUMBER}
    DESCRIPTION "Editor de fotos de eventos"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
# Windows: o executável fica na RAIZ da instalação, ao lado das DLLs. O deploy
# do Qt lê esta variável no nível do projeto (não adianta trocá-la dentro de
# uma função): tem de valer antes do GNUInstallDirs.
if(WIN32)
    set(CMAKE_INSTALL_BINDIR "." CACHE PATH "Executáveis (raiz da instalação no Windows)")
endif()
include(GNUInstallDirs)

option(PHOTOEDITOR_BUILD_TESTS "Compilar testes" ON)
option(PHOTOEDITOR_BUILD_APP "Compilar a interface gráfica" ON)
option(PHOTOEDITOR_WITH_ONNX "Seleção de objetos (SAM 2.1) com ONNX Runtime" ON)
set(PHOTOEDITOR_ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime já instalado (com include/ e lib/); vazio = baixar o pacote oficial")

# Identidade da marca (regra 11): nome, site e o que vai no EXIF Software.
set(PHOTOEDITOR_BRAND_NAME "PhotoEditor" CACHE STRING "Nome do produto")
set(PHOTOEDITOR_BRAND_URL "https://github.com/helviojunior/photo-editor" CACHE STRING "Site do produto (EXIF Software, Sobre)")

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Sql Concurrent)
if(PHOTOEDITOR_BUILD_APP)
    find_package(Qt6 6.7 REQUIRED COMPONENTS Quick QuickControls2 Svg LinguistTools)
endif()
qt_standard_project_setup(REQUIRES 6.7)

# OpenCV estático e mínimo (packaging/deps/opencv.sh); OpenCV_DIR ou
# CMAKE_PREFIX_PATH apontam para ele.
find_package(OpenCV 4.10 REQUIRED COMPONENTS core imgproc imgcodecs features2d calib3d objdetect)

if(PHOTOEDITOR_WITH_ONNX)
    include(src/cmake/OnnxRuntime.cmake)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
elseif(MSVC)
    add_compile_options(/W3 /utf-8 /permissive- /Zc:__cplusplus)
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif()

add_subdirectory(src/imaging)
if(EXISTS ${CMAKE_SOURCE_DIR}/src/core/CMakeLists.txt)
    add_subdirectory(src/core)
endif()
if(PHOTOEDITOR_BUILD_APP AND EXISTS ${CMAKE_SOURCE_DIR}/src/app/CMakeLists.txt)
    add_subdirectory(src/app)
endif()

if(PHOTOEDITOR_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
