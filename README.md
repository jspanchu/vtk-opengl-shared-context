# vtk-opengl-shared-context

This example loads OpenGL resources asynchronously and renders them in the main thread - with the VTK OpenGL rendering infrastructure.

The main thread renders an image from `vtkTextureObject` into a `vtkRenderWindow`, while the asynchronous texture loader generates an image and uploads it to `vtkTextureObject`.

The key steps to set this up:
1. `vtkRenderWindow::SetSharedRenderWindow`:    The loader thread's OpenGL context shares OpenGL objects with that of display thread.
2. `vtkTextureObject::AssignToExistingTexture`: Assign the loader thread's texture resource to the display thread's texture.
