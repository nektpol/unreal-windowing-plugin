# Using WindowingPlugin

Enable WindowingPlugin in an Unreal Engine 5.8 Win64 project and build the project. In Blueprint, get the **Wp Windowing Subsystem** from the current GameInstance.

## Open and manage a panel

Use **Open Panel Window** with a User Widget class and creation options. On success, store **Out Handle** for later operations. With a concrete widget class selected, **Out Widget** uses that class so you can call its Blueprint functions directly. **Get Widget From Handle** resolves an open handle to its widget. Bind **On Window Closed** to observe the end of the panel's lifetime.

Use **Set Window Title** to change a panel's title and **Request Close Window** to close it. Set a unique placement key if the window should restore its floating bounds on a later open. Each new open creates a fresh widget. A presentation transfer keeps the existing widget.

## Configure a dock workspace

Create a Widget Blueprint derived from **Wp Dock Workspace**. The inherited bindings require **LeftDock** and **RightDock** to be Vertical Boxes, and **TopDock** and **BottomDock** to be Horizontal Boxes. Leave the four boxes empty in the Designer.

For drag resizing and bounded viewport sizing, put the four boxes and a widget named **Center** directly inside a **Dock Layout Panel** that fills the workspace. The panel exposes the preferred edge sizes and the minimum center size in Slate units. Add the workspace to the viewport, then call **Register Dock Workspace** with that instance.

To dock a floating panel, open its Settings menu and choose **Dock Left**, **Dock Right**, **Dock Top**, or **Dock Bottom**. A host can also call **Dock Window** in Blueprint. Dragging the native title bar moves the floating window. The Settings menu on a docked panel offers **Undock Panel**. The group Settings menu can float the whole group, return it to the workspace, or rename it. Use the panel or group header to collapse and expand it where the command is available. Docked panels remain open, and their handles and widget references remain valid during transfers.

With the **Dock Layout Panel** set to **Visible**, drag a floating panel by its plugin title to an exposed viewport edge. Release over the blue preview to create a docker or append the panel to the group on that edge.

To place the panel beside a specific grouped panel, drag over the left, right, top, or bottom part of that panel's body. The **Moving** card follows the pointer, and the outline shows the insertion side. A drop across the current split axis creates a nested split. This works with docked groups and groups already in a floating window. The outer viewport bands remain available for whole-edge drops.

To create a floating group, drop one individually floating panel onto the highlighted side of another floating panel. The group opens at the target's normal window bounds and keeps both live widgets. Use the group gear to choose its first available dock edge. **Has Dock Side** in the presentation snapshot tells Blueprint when **Side** is set.

Once a docker contains a panel, drag the grey line on the edge facing **Center** to resize the whole region. For a Top docker, drag its bottom edge up or down. Drag the line between expanded panels to resize the panels within their group. Both lines are 2 Slate units wide, with a 6-unit drag target. Scroll along the group's axis when the panels need more space. Use **Expand Right** or **Restore Right Corner** in the docked Top group's Settings to choose which edge owns the top-right corner.

In Blueprint, call **Nest Adjacent Dock Panels** with two neighboring panel handles in the same split to create a child split on the perpendicular axis. The group keeps the existing panel widgets, handles, and order. Floating the group and returning it preserves the nested layout.
