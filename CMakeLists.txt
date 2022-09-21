cmake_minimum_required(VERSION 3.12)
project(OglCtxShare C CXX)
find_package(VTK COMPONENTS 
  CommonCore
  InteractionStyle
  RenderingCore
	RenderingUI
  RenderingOpenGL2
)
add_executable(main MACOSX_BUNDLE main.cxx)
target_link_libraries(main PRIVATE ${VTK_LIBRARIES})
vtk_module_autoinit(
	TARGETS main
	MODULES ${VTK_LIBRARIES}
)
