#include <atomic>
#include <mutex>
#include <vtkLogger.h>
#include <vtkObject.h>
#include <vtkOpenGLError.h>
#include <vtkOpenGLFramebufferObject.h>
#include <vtkOpenGLRenderUtilities.h>
#include <vtkOpenGLRenderWindow.h>
#include <vtkOpenGLShaderCache.h>
#include <vtkOpenGLState.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkShaderProgram.h>
#include <vtkSmartPointer.h>
#include <vtkTextureObject.h>
#include <vtkUnsignedCharArray.h>
#include <vtk_glew.h>

#include <future>

static const char *VertexShader =
    R"***(
  //VTK::System::Dec
  in vec4 vertexMC;

  void main()
  {
    gl_Position = vertexMC;
  }
  )***";

static const int width = 480;
static const int height = 480;
vtkNew<vtkTextureObject> displayTexture, loaderTexture;
std::atomic<bool> initialized, finalize;

vtkSmartPointer<vtkUnsignedCharArray> GenerateImage(int w, int h, int seed) {
  auto image = vtk::TakeSmartPointer(vtkUnsignedCharArray::New());
  image->SetNumberOfValues(w * h * 4);
  vtkLogF(INFO, "%d\n", seed);
  for (int i = 0, index = 0; i < h; ++i) {
    for (int j = 0; j < w; ++j) {
      unsigned char r = (j + seed) % 255;
      image->SetValue(index++, r);
      image->SetValue(index++, 0);
      image->SetValue(index++, 0);
      image->SetValue(index++, 255);
    }
  }
  return image;
}

void Allocate2D(vtkTextureObject *texture, vtkOpenGLRenderWindow *oglRwin) {
  texture->Allocate2D(width, height, 4, VTK_UNSIGNED_CHAR, 0);
}

void Upload2D(vtkSmartPointer<vtkUnsignedCharArray> arr,
              vtkTextureObject *texture, vtkOpenGLRenderWindow *oglRwin) {

  if (!initialized) {
    return;
  }
  const auto target = texture->GetTarget();
  const auto handle = texture->GetHandle();
  const auto components = texture->GetComponents();
  const auto vtktype = texture->GetVTKDataType();
  const auto datatype = texture->GetDataType(vtktype);
  const auto format = texture->GetFormat(vtktype, components, 0);
  const auto width = texture->GetWidth();
  const auto height = texture->GetHeight();
  texture->Bind();
  glTexSubImage2D(target, 0, 0, 0, width, height, format, datatype,
                  arr->GetPointer(0));
  vtkOpenGLCheckErrors("Upload2D: glTexSubImage2D failed. ");
  glBindTexture(target, 0);
}

void CopyToFrameBuffer(vtkTextureObject *texture,
                       vtkOpenGLRenderWindow *oglRwin) {
  if (!initialized) {
    return;
  }
  texture->CopyToFrameBuffer(nullptr, nullptr);
}

void loader(vtkRenderWindow *sharedWin) {
  vtkNew<vtkRenderWindowInteractor> iren;
  vtkNew<vtkRenderWindow> rwin;
  vtkLogger::SetThreadName("Resource Loader");
  rwin->SetWindowName("Resource Loader");
  rwin->SetSize(width, height);
  rwin->SetSharedRenderWindow(sharedWin);
  auto oglRwin = vtkOpenGLRenderWindow::SafeDownCast(rwin);

  iren->SetRenderWindow(rwin);
  iren->Initialize();
  loaderTexture->SetContext(oglRwin);
  Allocate2D(loaderTexture, oglRwin);
  displayTexture->AssignToExistingTexture(loaderTexture->GetHandle(),
                                          loaderTexture->GetTarget());
  displayTexture->Resize(width, height);
  initialized = true;
  int seed = 0;
  while (!finalize)
  {
    auto image = GenerateImage(width, height, seed++);
    rwin->Start();
    Upload2D(image, loaderTexture, oglRwin);
    CopyToFrameBuffer(loaderTexture, oglRwin);
    rwin->End();
    rwin->Frame();
  }
  loaderTexture->ReleaseGraphicsResources(oglRwin);
}

int main(int argc, char *argv[]) {
  vtkNew<vtkRenderWindowInteractor> iren;
  vtkNew<vtkRenderWindow> rwin;
  vtkLogger::SetThreadName("Display");
  rwin->SetWindowName("Display");
  rwin->SetSize(width, height);
  auto oglRwin = vtkOpenGLRenderWindow::SafeDownCast(rwin);

  iren->SetRenderWindow(rwin);
  iren->Initialize();
  initialized = false;
  displayTexture->SetContext(oglRwin);
  auto result = std::async(std::launch::async, &loader, rwin.GetPointer());

  while (!iren->GetDone()) 
  {
    if (initialized && !displayTexture->GetHandle())
    {
      finalize = true;
      result.get();
      break;
    }
    rwin->Start();
    CopyToFrameBuffer(displayTexture, oglRwin);
    rwin->End();
    rwin->Frame();
    iren->ProcessEvents();
  }
  return 0;
}