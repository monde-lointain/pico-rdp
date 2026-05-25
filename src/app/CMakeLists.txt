# Presenter / app subsystem: your game's state machine + rendering.
add_library(app STATIC
    app.cc
)
add_library(picosystem_template::app ALIAS app)
target_include_directories(app PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(app
    PUBLIC  picosystem_template::game picosystem_template::gfx
    PRIVATE project_warnings
)
orthodoxy_enforce(app)
