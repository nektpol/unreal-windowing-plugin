# WindowingPlugin

WindowingPlugin opens UMG widgets in independent Windows desktop windows. It targets Unreal Engine 5.8 and exposes a GameInstance subsystem for Blueprint and C++.

Copy the plugin into your project's `Plugins/WindowingPlugin` directory, enable it, and build the project. Get the **Wp Windowing Subsystem**, call **Open Widget Window** with a widget class, and store the returned handle on `Success`. Use it to query, rename, or request closure of the window. Bind **On Window Closed** to observe completion.

Each open creates a fresh widget. Keep state in the host application if it should survive closing and reopening.

| Milestone | Deliverable |
|---|---|
| M0 | Native UMG window creation, title and state controls, lifecycle events, and shutdown handling. [Implementation notes](docs/WindowingPlugin_M0.md) · [Blueprint sample](docs/WindowingPlugin_M0_Step_6_Test_Guide.md) |
| M1 | Lifecycle and session isolation with three simultaneous windows, tested in packaged Win64 builds. [Automated acceptance](docs/WindowingPlugin_M1_Tests.md) |
| M2 | Keyed placement persistence, DPI conversion, and safe recovery when a display is unavailable. [Implementation guide](docs/WindowingPlugin_M2_Guide.md) |
| M3 | Report managed-window activation and focus changes so the host can decide how to route viewport or camera input. |
| M4 | Prepare the plugin for distribution with Blueprint usage docs, a sample project, repeatable packaging, and a release test matrix. |

The v1.0 release follows the Windows distribution checks. Docking, existing-widget attachment, and additional platforms are possible extensions.
