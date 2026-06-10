qt_add_executable(pass WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
)

target_link_libraries(pass PRIVATE passcore Qt6::Widgets)
