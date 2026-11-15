# WindowingPlugin

WindowingPlugin opens UMG widgets in independent Windows desktop windows. It targets Unreal Engine 5.8 and exposes a GameInstance subsystem for Blueprint and C++.

Copy the plugin into your project's `Plugins/WindowingPlugin` directory, enable it, and build the project. Get the **Wp Windowing Subsystem**, call **Open Widget Window** with a widget class, and store the returned handle on `Success`. Use it to query, rename, or request closure of the window. Bind **On Window Closed** to observe completion.

Each open creates a fresh widget. Keep state in the host application if it should survive closing and reopening.

See the [public usage guide](docs/Usage.md) for widget references, docking, and workspace setup.
