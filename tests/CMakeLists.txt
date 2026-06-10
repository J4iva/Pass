# Los tests enlazan passcore SIN Qt6::Widgets: sirven también como test de humo
# de que el core se mantiene libre de dependencias de UI.
function(pass_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE passcore Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

pass_add_test(tst_smoke)
pass_add_test(tst_migrations)
pass_add_test(tst_repositories)
pass_add_test(tst_strategycatalog)
pass_add_test(tst_timerservice)
pass_add_test(tst_stats)
pass_add_test(tst_events)
