# WindowingPlugin

**WindowingPlugin** is a modular window-management plugin for Unreal Engine 5.8, designed to bring flexible desktop-style interfaces to Unreal applications.

The plugin aims to let developers display Unreal Motion Graphics (UMG) widgets in independent native operating-system windows, alongside the application's main window. These additional windows can be moved, resized, and arranged across multiple monitors, making it possible to build customizable, multi-window workspaces.

## Planned Features

- **Native desktop windows:** Open UMG interfaces in separate operating-system windows.
- **Multiple windows:** Create and manage several independent windows at once.
- **Window controls:** Open, close, show, hide, resize, reposition, and update window titles programmatically.
- **Blueprint and C++ integration:** Control windows and respond to lifecycle events through straightforward APIs.
- **Focus and input awareness:** Detect which managed window is active so applications can coordinate their own input behavior.
- **Persistent layouts:** Save and restore window placement, including practical handling of multi-monitor configurations.
- **Extensible workspace support:** Provide a foundation for future features such as detachable panels and docking.
- **Reusable design:** Keep window management independent of any specific application's data or UI structure.

## Platform and Engine Support

WindowingPlugin targets **Unreal Engine 5.8** and is designed for desktop runtime applications, with **Windows** as the initial platform. Support for other desktop operating systems may be explored in the future.

## Project Status

WindowingPlugin is currently in development. The capabilities described above represent the intended direction of the plugin and are not all implemented yet.
