# Third-party components

- Qt 6.8.0, official MinGW x64 SDK from https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_680/qt6_680/ .
  The application dynamically links Qt Core, Gui and Widgets. windeployqt copies their runtime dependencies.
  Upstream source: https://github.com/qt/qtbase/tree/v6.8.0 .
  Source archive: https://download.qt.io/archive/qt/6.8/6.8.0/submodules/qtbase-everywhere-src-6.8.0.tar.xz .
  License texts supplied with the release are in licenses/Qt.
- cJSON 1.7.19, https://github.com/DaveGamble/cJSON/tree/v1.7.19 .
  MIT license, retained in third_party/cjson/LICENSE and copied to the release licenses directory.
- EasyX Graphics Library 26.10.25 is retained under third_party/easyx as upstream history.
  The current Qt build does not compile or link EasyX.
  Original download: https://easyx.cn/download/EasyX_26.10.25.exe .
- The supplied songs, WAV files and charts come from the original NoteCollision project.
  Their provenance is described in each song's README.md.
