#include <vtkCallbackCommand.h>
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
#include <vtkRenderer.h>
#include <vtkShaderProgram.h>
#include <vtkSmartPointer.h>
#include <vtkTextureObject.h>
#include <vtkUnsignedCharArray.h>
#include <vtk_glew.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>

static const int width = 480;
static const int height = 480;
static const int framerate = 60;
vtkNew<vtkTextureObject> displayTexture, loaderTexture;
std::atomic<bool> initialized, finalize;
std::mutex texMtx;

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

void Allocate2D(vtkTextureObject *texture) {
  texture->Allocate2D(width, height, 4, VTK_UNSIGNED_CHAR, 0);
}

void Upload2D(vtkSmartPointer<vtkUnsignedCharArray> arr,
              vtkTextureObject *texture) {

  if (!initialized) {
    return;
  }
  std::unique_lock<std::mutex> lk(texMtx);
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

void CopyToFrameBuffer(vtkTextureObject *texture) {
  if (!initialized) {
    return;
  }
  std::unique_lock<std::mutex> lk(texMtx);
  texture->CopyToFrameBuffer(nullptr, nullptr);
}

void loader(vtkRenderWindow *sharedWin) {
  vtkNew<vtkRenderWindow> rwin;
  vtkLogger::SetThreadName("Resource Loader");
  rwin->SetWindowName("Resource Loader");
  rwin->SetSize(width, height);
  rwin->SetSharedRenderWindow(sharedWin);
  rwin->ShowWindowOff();
  auto oglRwin = vtkOpenGLRenderWindow::SafeDownCast(rwin);

  oglRwin->Initialize();
  loaderTexture->SetContext(oglRwin);
  Allocate2D(loaderTexture);
  displayTexture->AssignToExistingTexture(loaderTexture->GetHandle(),
                                          loaderTexture->GetTarget());
  initialized = true;
  int seed = 0;
  while (!finalize) {
    auto image = GenerateImage(width, height, seed++);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000/framerate));
    Upload2D(image, loaderTexture);
  }
  loaderTexture->ReleaseGraphicsResources(oglRwin);
}

int main(int argc, char *argv[]) {
  vtkNew<vtkRenderWindowInteractor> iren;
  vtkNew<vtkRenderWindow> rwin;
  vtkNew<vtkRenderer> ren;
  vtkLogger::SetThreadName("Display");
  rwin->SetWindowName("Display");
  rwin->SetSize(width, height);
  rwin->AddRenderer(ren);
  auto oglRwin = vtkOpenGLRenderWindow::SafeDownCast(rwin);

  iren->SetRenderWindow(rwin);
  iren->Initialize();

  initialized = false;
  auto result = std::async(std::launch::async, &loader, rwin.GetPointer());

  vtkNew<vtkCallbackCommand> cmd;
  cmd->SetClientData(displayTexture);
  cmd->SetCallback([](vtkObject *renObj, unsigned long, void *cd, void *) {
    auto rend = vtkRenderer::SafeDownCast(renObj);
    auto tex = reinterpret_cast<vtkTextureObject *>(cd);
    CopyToFrameBuffer(tex);
  });
  ren->AddObserver(vtkCommand::EndEvent, cmd);

  // on windows, wglShareLists will fail if the target render context
  // is current in another thread. let's release our context and wait
  // till loader thread render context is initialized.
  oglRwin->ReleaseCurrent();

  // wait till initialize.
  while (!initialized)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  displayTexture->SetContext(oglRwin);
  displayTexture->Resize(width, height);
  iren->EnableRenderOff();
  while (!iren->GetDone()) {
    if (!displayTexture->GetHandle()) {
      finalize = true;
      result.get();
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000/framerate));
    rwin->Render();
    iren->ProcessEvents();
  }
  finalize = true;
  result.get();
  return 0;
}
