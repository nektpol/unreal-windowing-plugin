# WindowingPlugin

WindowingPlugin opens UMG widgets in independent Windows desktop windows. It targets Unreal Engine 5.8 and exposes a GameInstance subsystem for Blueprint and C++.

Copy the plugin into your project's `Plugins/WindowingPlugin` directory, enable it, and build the project. Get the **Wp Windowing Subsystem**, call **Open Widget Window** with a widget class, and store the returned handle on `Success`. Use it to query, rename, or request closure of the window. Bind **On Window Closed** to observe completion.

Each open creates a fresh widget. Keep state in the host application if it should survive closing and reopening.

- [M0 implementation notes](docs/WindowingPlugin_M0.md)
- [M0 Blueprint sample and acceptance tests](docs/WindowingPlugin_M0_Step_6_Test_Guide.md)
- [M1 lifecycle and multi-window plan](docs/WindowingPlugin_M1_Plan.md)
