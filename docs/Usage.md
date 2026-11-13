# Using WindowingPlugin

Enable WindowingPlugin in an Unreal Engine 5.8 Win64 project and build the project. In Blueprint, get the **Wp Windowing Subsystem** from the current GameInstance.

## Open and manage a panel

Use **Open Panel Window** with a User Widget class and creation options. On success, store **Out Handle** for later operations. With a concrete widget class selected, **Out Widget** uses that class so you can call its Blueprint functions directly. **Get Widget From Handle** resolves an open handle to its widget. Bind **On Window Closed** to observe the end of the panel's lifetime.

Use **Set Window Title** to change a panel's title and **Request Close Window** to close it. Set a unique placement key if the window should restore its floating bounds on a later open. Each new open creates a fresh widget. A presentation transfer keeps the existing widget.

## Configure a dock workspace

Create a Widget Blueprint derived from **Wp Dock Workspace**. The inherited bindings require **LeftDock** and **RightDock** to be Vertical Boxes, and **TopDock** and **BottomDock** to be Horizontal Boxes. Leave the four boxes empty in the Designer.

For bounded viewport sizing, put the four boxes and a widget named **Center** directly inside a **Dock Layout Panel** that fills the workspace. The panel exposes the preferred edge sizes and the minimum center size in Slate units. Add the workspace to the viewport, then call **Register Dock Workspace** with that instance.

Use a panel's Settings menu to dock it to an edge or undock it. The group Settings menu can float the whole group, return it to the workspace, or rename it. Use the panel or group header to collapse and expand it where the command is available. Docked panels remain open, and their handles and widget references remain valid during transfers.
