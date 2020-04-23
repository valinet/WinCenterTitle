# WinCenterTitle
WinCenterTitle is a simple tool that allows you to center align the text in Windows 10 titlebars, the same way it was in Windows 8, 8.1, or even 3.1.

## Installation

* Download archive from [releases](https://github.com/valinet/WinCenterTitle/releases)
* Unpack to folder of your choice (preconfigured for *C:\WinCenterTitle*)
* Edit *WinCenterTitle.xml* and edit line 40, which contains path to application, according to your system
* Register scheduled task which will enable the tweak at startup: run *install.bat* as administrator. This is one way of running the application using the SYSTEM account, which is required in order to be able to hook into DWM.
* VERY IMPORTANT: Hooking DWM requires special permissions for the folder that contains the application and the library. Make sure folder owner is set to *Administrator**s*** and that the *Users* group is added as well, with default permissions (Read & execute, List folder contents, and Read). Otherwise, the application will terminate with error code -6, being unable to hook the library into Desktop Window Manager.
* Reboot system or manually run the created scheduled task. This will center title bars for all windows. You can close or rerun the application from Task Scheduler, allowing you to toggle between center aligned and left aligned (default) title bars. No DWM restart is required and the library should leave DWM in a clean state when unloading.

## To do

* Hook window activation and optionally hide icon in title bar (could be achieved with [SetWindowThemeAttribute](https://docs.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-setwindowthemeattribute))
* Add option to center between window margins, as opposed to space between icon and window controls
* User settings, system tray icon, configuration GUI etc (unlikely)

## How it works

The application injects into DWM and hooks *DrawText* method that is used to compute the area, and draw the title bar text for the window. By doing so, we achieve the effect with least amount of overhead, as the method is called by DWM only when the window is an unskinned (no custom client side decoration aka no [DwmExtendFrameIntoClientArea](https://docs.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmextendframeintoclientarea)). After injecting, the application monitors DWM and reinjects should it crash or reload by user action.

## License

Hooking is done using the excellent [funchook](https://github.com/kubo/funchook) library (GPLv2 with linking exception), which in turn is powered by the [diStorm3](https://github.com/gdabah/distorm/) (3-clause BSD) disassembler. Thus, I am offering this under GNU General Public License Version 2.0, which I believe is compatible.

## Compiling

In order to compile, you need to statically link the 2 dependencies. Thus, you have to do statically build them (it is super simple, with great instructions provided on the project pages). Then, place the files in a folder called *libs*, like in the following tree:

```
repo
├── .git
├── ...
└── libs
    ├── distorm
    │   ├── include
    │   │   ├── distorm.h
    │   │   └── mnemonics.h
    │   └── lib
    │       └── distorm.lib
    └── funchook
        ├── include
        │   └── funchook.h
        └── lib
            └── funchook.lib
```

