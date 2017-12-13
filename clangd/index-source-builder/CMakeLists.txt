include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)

set(LLVM_LINK_COMPONENTS
    Support
      )

add_clang_executable(index-source-builder
  IndexSourceBuilderMain.cpp
  )

target_link_libraries(index-source-builder
  PRIVATE
  clangAST
  clangIndex
  clangDaemon
  clangBasic
  clangFrontend
  clangTooling
  clangToolingRefactor
)
