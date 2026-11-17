# 复用“程序设计”配置中的 MinGW 工具链；EasyX 接口需要按 C++ 编译。
add_executable(EasyXMaze WIN32 main.cpp)
target_include_directories(EasyXMaze PRIVATE "${PROJECT_SOURCE_DIR}/third_party/easyx/include")
target_compile_definitions(EasyXMaze PRIVATE UNICODE _UNICODE)
target_compile_options(EasyXMaze PRIVATE -Wall -Wextra -Wno-unknown-pragmas -finput-charset=UTF-8)
target_link_libraries(EasyXMaze PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/easyx/lib_mingw_msvcrt/x64/libeasyxw.a"
    gdi32 user32 msimg32 gdiplus imm32)
# 静态链接 MinGW 运行库，复制到桌面的 exe 可以直接双击启动。
target_link_options(EasyXMaze PRIVATE -static -static-libgcc -static-libstdc++)
