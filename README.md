# WindowingPlugin

WindowingPlugin opens UMG widgets in independent Windows desktop windows. It targets Unreal Engine 5.8 and exposes a GameInstance subsystem for Blueprint and C++.

Copy the plugin into your project's `Plugins/WindowingPlugin` directory, enable it, and build the project. Get the **Wp Windowing Subsystem**, call **Open Widget Window** with a widget class, and store the returned handle on `Success`. Use it to query, rename, or request closure of the window. Bind **On Window Closed** to observe completion.

Each open creates a fresh widget. Keep state in the host application if it should survive closing and reopening.

| Milestone | Deliverable |
|---|---|
| M0 | Native UMG window creation, title and state controls, lifecycle events, and shutdown handling. [Implementation notes](docs/WindowingPlugin_M0.md) · [Blueprint sample](docs/WindowingPlugin_M0_Step_6_Test_Guide.md) |
| M1 | Lifecycle and session isolation with three simultaneous windows, tested in packaged Win64 builds. [Automated acceptance](docs/WindowingPlugin_M1_Tests.md) |
| M2 | Keyed placement persistence, DPI conversion, and display recovery. The editor build and placement tests passed, and the live placement check was confirmed. [Implementation and verification](docs/WindowingPlugin_M2_Guide.md) |
| M3 | Managed-window activation and keyboard focus are implemented. The live check was confirmed. [Implementation and verification](docs/WindowingPlugin_M3_Guide.md) |
| M4 | Next milestone. Build the Win64 distribution and sample archives, then verify a clean install and packaged behavior. [Release guide and acceptance test](docs/WindowingPlugin_M4_Guide.md) |

The v1.0 release follows the Windows distribution checks. Docking, existing-widget attachment, and additional platforms are possible extensions.
