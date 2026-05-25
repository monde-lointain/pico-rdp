# Your game logic lives here. Ships only the seedable PRNG; add your own modules.
add_library(game STATIC
    rng.cc
)
add_library(picosystem_template::game ALIAS game)
target_include_directories(game PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(game PRIVATE project_warnings)
orthodoxy_enforce(game)
