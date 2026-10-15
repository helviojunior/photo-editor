# Núcleo do editor: projeto (pasta + catálogo SQLite), ações sobre as fotos,
# histórico, merges, caches derivados, exportação e projetos recentes. Sem
# interface — o app (QML) e os testes usam esta biblioteca.
add_library(photoeditor_core STATIC
    Paths.h Paths.cpp
    FileTime.h FileTime.cpp
    Database.h Database.cpp
    Models.h
    Project.h Project.cpp
    Merges.h Merges.cpp
    Derivatives.h Derivatives.cpp
    Exporter.h Exporter.cpp
    RecentProjects.h RecentProjects.cpp
)
target_include_directories(photoeditor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(photoeditor_core PUBLIC photoeditor_imaging Qt6::Core Qt6::Sql Qt6::Concurrent)

# Passos do esquema do catálogo, embutidos no binário (:/photoeditor/schema/NNNN.sql).
file(GLOB PHOTOEDITOR_SCHEMA RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS schema/*.sql)
qt_add_resources(photoeditor_core "photoeditor_schema"
    PREFIX "/photoeditor"
    FILES ${PHOTOEDITOR_SCHEMA})
