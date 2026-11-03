# Third-party notices for DSO138 Studio (Windows x64)

## DSO138 Studio

Copyright (c) 2026 Martin Dodek. The DSO138 Studio source code is distributed under the MIT License; the full text is in `LICENSE.txt` in the Windows package and in the [project repository](https://github.com/dodekm/dso138-firmware).

## Qt 6.8.3

DSO138 Studio dynamically links to Qt 6.8.3 Core, Gui, Widgets, and Serial Port. The deployment includes the Qt Windows platform plugin and other runtime dependencies discovered for this build. These Qt components are distributed under the GNU Lesser General Public License version 3 (LGPL-3.0-only) or the GNU General Public License version 3 (GPL-3.0-only); this package uses the LGPL option. The full upstream license texts are included as `licenses/LGPL-3.0.txt` and `licenses/GPL-3.0.txt`.

Qt's own licensing and compliance guidance:

- [Qt open-source licensing obligations](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt licensing overview](https://doc.qt.io/qt-6/licensing.html)
- [Qt for Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html)
- [Qt 6.8.3 official source archive index](https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/)
- [`qtbase` 6.8.3 source archive](https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz) (Qt Core, Gui, Widgets, and Windows platform plugin)
- [`qtserialport` 6.8.3 source archive](https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/qtserialport-everywhere-src-6.8.3.tar.xz)
- [GNU LGPL version 3.0 text](https://www.gnu.org/licenses/lgpl-3.0.txt)
- [GNU GPL version 3.0 text](https://www.gnu.org/licenses/gpl-3.0.txt)

The `licenses/sbom/qtbase-6.8.3.spdx.json` and `licenses/sbom/qtserialport-6.8.3.spdx.json` files are the SPDX SBOMs shipped with the Qt installation used to build this package. They identify the Qt components, embedded third-party attributions, and declared license expressions. Review those SBOMs together with the matching Qt source archives for component-level notices.

The Qt libraries are separate shared libraries in the package and can be replaced with compatible modified builds. Application source and build instructions are in the development checkout. Distributors should preserve these notices, make the corresponding Qt sources available as required by their distribution method, and permit replacement of the Qt libraries. See the LGPL text and Qt guidance above for the full terms.

## Microsoft Visual C++ runtime

The package may contain a subset of the x64 VC143 C runtime DLLs imported by the executable and deployed Qt DLLs. They are copied from the exact Visual Studio 14.44 toolset-selected redistributable directory and remain unmodified. Redistribution is subject to the Microsoft Software License Terms applicable to Visual Studio and its redistributable files.

- [Microsoft: Redistribute Visual C++ files and licensing](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170)
- [Microsoft: Choose a C++ deployment method, including app-local deployment](https://learn.microsoft.com/en-us/cpp/windows/choosing-a-deployment-method?view=msvc-170)

No other third-party libraries are intentionally added by this Windows application. The build manifest and Qt SBOMs record the runtime files and Qt distribution metadata used for the package.
