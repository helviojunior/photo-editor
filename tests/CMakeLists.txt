# Testes (QtTest): um executável por área. Os de imaging comparam com
# referências geradas pela versão em Python (branch legacy) em tests/data/.
function(photoeditor_test name)
    add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE ${ARGN} Qt6::Test)
    target_compile_definitions(tst_${name} PRIVATE PHOTOEDITOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
                                                   PHOTOEDITOR_MODELS_DIR="${CMAKE_SOURCE_DIR}/.cache/models")
    add_test(NAME ${name} COMMAND tst_${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

photoeditor_test(develop photoeditor_imaging)
photoeditor_test(exif photoeditor_imaging)
photoeditor_test(auto photoeditor_imaging)
photoeditor_test(merge photoeditor_imaging)
photoeditor_test(segment photoeditor_imaging)

if(TARGET photoeditor_core)
    photoeditor_test(core photoeditor_core)
endif()
