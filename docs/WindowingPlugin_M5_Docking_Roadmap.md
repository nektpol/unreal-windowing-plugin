# M5 roadmap — dockable windows

M5 lets a host move an existing WindowingPlugin widget between its own native Windows window and a dock in the game viewport. A docked panel can collapse to its header, expand, and resize by dragging a divider. A panel keeps its handle, widget instance, and placement key while it moves between hosts.

## Widget access delivered in M5.2

**Open Panel Window** creates the widget and returns its handle and the live widget, typed from the chosen Blueprint class. **Get Widget From Handle** returns the base widget reference for a live handle in the current session. The same instance remains available through dock and undock. Closed or invalid handles return null.

The managed header has **Close**, **Collapse/Expand**, and **Settings**. Settings supplies actions for the current panel or group and its attachment. Left and right dockers start with vertical panel layouts. Top and bottom dockers start with horizontal panel layouts. A docker group can float as one native window. The design specifies corner ownership, nested splits, and how to extend Settings. [Docking interaction design](WindowingPlugin_M5_Docking_Interaction_Design.md)

## M5.1 — Add a dock workspace and transfer a panel

Implemented. Panel dock and undock passed the host project's manual Standalone check. The sample harness also covers individual transfers on all four edges and a two-panel group round trip. The viewport sizing limitation below remains open.

Follow the file-by-file guide for the Blueprint API, workspace, transfer sequence, and live test. [M5.1 dock and undock guide](WindowingPlugin_M5_1_Dock_Undock_Guide.md)

### M5.1 layout limitation to resolve

The initial Blueprint workspace uses a fixed nested box hierarchy. A top dock and bottom dock each add their native content host to horizontal boxes. When those rows have no constrained height and the child widgets request large desired sizes, Slate can allocate more height than the viewport and push the bottom row off-screen. M5.3.1 owns the top-plus-bottom overflow fix and its verification. Give the center region the flexible fill space, constrain dock row heights, and define sensible minimum and maximum dock sizes. For robust resizing and corner ownership, implement the region layout with explicit sizing in M5.3 rather than relying on content desired size in nested boxes.

1. Add a runtime UMG dock workspace that the host adds to its viewport. Register it with `UWpWindowingSubsystem` and keep a weak reference scoped to its GameInstance. Put the main content between the dock edges so the workspace can measure and protect its usable area.
2. Add a presentation mode to the runtime entry with `Floating` and `Docked` values. Keep it separate from the existing lifecycle state. An open panel remains open in either mode. Store its dock edge and order while docked, and remember its last floating geometry for undock.
3. Add Blueprint operations `DockWindow(Handle, Side)`, `UndockWindow(Handle)`, and `GetWindowPresentation(Handle)`. Reuse `FWpWindowResult` for mutations and append new result codes for a missing workspace and an in-progress transfer. Validate the session, handle, lifecycle state, and target slot before changing the current presentation. Treat an identical request as a successful no-op.
4. Keep one `UUserWidget` and one retained Slate content widget per open panel. Use one visual parent at a time. Extend `FWpNativeWindowHost` with explicit attach and detach operations. Keep the current GC anchor throughout transfer.
5. On docking, capture the floating bounds, reserve the dock slot, and move the retained content into it. Once attachment succeeds, release the old native host through the existing deferred Slate teardown boundary. Give each native host a generation token. Only a close callback from the current floating host can close the logical panel. A callback from a retiring host completes that host's cleanup.
6. On undocking, create and register a replacement native host while the dock is still intact. Prepare its bounds using the last floating rectangle, the M2 resolver, and the current monitor layout. Transfer the content and show the window. If native creation or attachment fails, roll back to the original dock slot and keep its collapse state. Notify listeners of the presentation change only after a successful transfer.
7. Route Close and title changes to the current presenter. Closing a docked panel detaches it and retires the handle once. Unregistering a workspace closes its docked panels through the normal lifecycle, while floating panels in that session remain available. On session shutdown, retire both forms of presentation and suppress late events as the current service does.

Reuse M2's keyed placement for floating bounds. Docking and resizing must never replace floating coordinates with viewport coordinates. Reuse M3's focus event for native foreground focus. When a panel docks, its old native handle must disappear from that snapshot. The docked widget receives keyboard focus through the main viewport's UMG input path.

**Exit check.** In Standalone Game, open A and B, type into A, dock A, and undock A. A keeps its handle and text, and B stays open. The dock and undock actions emit one presentation change each and zero open or close events. Closing A after the final transfer emits one close event. Repeat with a missing dock workspace and a failed native registration to verify the original presentation remains usable.

Then add a group ID and ordered child layout. Transfer the entire group between its edge and a single native host. Verify that all child handles, text, and order survive the transfer and a failed destination leaves the group usable.

## M5.2 — Add collapse and expand

Implemented. The implementation guide covers widget access, managed headers, collapse, built-in context menus, and group Rename. [M5.2 headers, collapse, and Settings guide](WindowingPlugin_M5_2_Headers_Collapse_Settings_Guide.md)

1. Give every panel and group header exactly three controls for Close, Collapse/Expand, and Settings. Generate Settings from attachment and capabilities and add Rename for groups. Collapse hides the relevant content area and retains its widget instances. Set the body to Slate/UMG collapsed visibility so it consumes no layout space or input. Keep the header visible and focusable.
2. Add `SetWindowCollapsed(Handle, bCollapsed)` and include `bCollapsed` in the presentation snapshot. Accept collapse changes only for docked panels. Repeating the current value succeeds without an event. Undocking always shows the floating content. A later dock can restore that panel's last docked collapse preference.
3. Keep the last expanded size separately from the collapsed header size. Restore the preferred size on expand, subject to the current workspace constraints. When a panel collapses while a child has keyboard focus, move focus to its header so hidden text fields stop receiving input.

**Exit check.** Type into A, collapse it, and type again with its header focused. The hidden field must retain the original text. Expand A and verify its content and previous size return. Undock while collapsed and verify the floating body is visible. Collapse and expand must leave B and the lifecycle event counts unchanged.

## M5.3.0 — Organize the plugin before adding layout code

Completed. Win64 Development Editor built in the sample, both M2 placement automation tests passed, and M5.1/M5.2 passed in Standalone. The placement tests now live under `Private/Placement/Tests`. They still exercise private placement helpers.

Reorganize the existing runtime module by responsibility before starting M5.3.1. Keep Unreal's `Public` and `Private` boundaries and the `WindowingPlugin` module name. Move implementation files into feature folders so the new layout code has a clear home.

| Location | Responsibility |
|---|---|
| `Public/` | Supported C++ and Blueprint-facing headers, including the subsystem, shared types, and workspace class. Keep existing public include paths during this pass. |
| `Private/Lifecycle/` | Lifetime service, runtime entries and records, subsystem implementation. |
| `Private/Docking/` | Workspace implementation, group state, and dock/undock operations. |
| `Private/Presentation/` | Managed headers and native window hosting. |
| `Private/Placement/` | Floating placement, monitor geometry, and placement storage. |
| `Private/Layout/` | Workspace geometry and sizing code introduced in M5.3.1. |

Separate tests by how they run. The packaged Standalone scenarios already live in the independent `WindowingM1` sample game module under `Tests/Sample`. Keep those scenarios there so Development and Shipping can exercise the public plugin API. Use a test-only Editor module named `WindowingPluginTests` for engine automation that can use a supported module boundary. Keep the existing placement tests beside the placement code while they depend on private helpers. Move them only when there is a narrow test boundary. Keep Editor automation out of the shipped runtime module and preserve existing automation test names when moving tests.

Use a dedicated reorganization commit. Update includes, build rules, and affected documentation paths. Preserve reflected class and function names, serialized paths, public include paths, and runtime behavior. The implementation guide should list each file move with its source and destination, give complete replacements for any changed functions or build rules, and include the test module setup if it moves tests.

**Exit check.** Build Win64 Development Editor, run the existing M2 placement automation and the M5.1/M5.2 Standalone scenarios, and verify that existing Blueprint assets resolve the same classes and nodes. Verify that the runtime package excludes the Editor test module if it was added. Use the reorganization commit as the baseline for M5.3.1.

## M5.3 — Layout and resizing

Implement one stage at a time. Each stage starts from the previous stage's working checkout and ends with a build and an acceptance check.

| Stage | Deliverable | Exit check |
|---|---|---|
| **M5.3.1 — Bounded workspace layout** | Resolve the five viewport regions from the available space. Keep the default corner arrangement and the existing panel transfer model. | Dock Top and Bottom together, shrink Standalone, and verify that both docks and the usable center stay inside the viewport. Enlarge it and verify that preferred sizes return. |
| **M5.3.2 — Resize dock edges** | Add draggable dividers at the edge/center boundaries. Store preferred extents separately from the effective clamped sizes. | Drag all four dividers. Verify size limits and cancel the drag if an edge disappears. |
| **M5.3.3 — Corner ownership and Expand Right** | Allow each corner to be assigned to one neighboring edge. Wire **Expand Right** and its reverse into Top's Settings. | Give Top the top-right corner, confirm Right starts below it, and reverse the action. |
| **M5.3.4 — Resize panels inside a group** | Resize the flat split, keep collapsed headers reachable, and scroll when panel minimums exceed the available space. | Resize two panels, collapse one, resize its neighbor, then expand it. Confirm the saved preference returns when space allows. |
| **M5.3.5 — Nested splits and packaged regression** | Extend groups to a tree with horizontal and vertical splits. Transfer the whole tree between dock and native hosts. | Recreate the nested Right Docker, resize both axes, float and return it, and verify child order, handles, state, and geometry. |

### M5.3.1 — Bounded workspace layout

Create a workspace panel that arranges the four existing dock containers and center from explicit rectangles. Keep the four edge binding names and types used by `UWpDockWorkspace`. The first layout keeps left and right full height, with top and bottom in the central column. Keep the native widget host attachment path so viewport resizing leaves live panels attached.

Implement a pure geometry resolver that takes viewport size, edge occupancy, preferred edge extents, and a center minimum in Slate units. Clamp the center reserve to the available space first. Allocate the rest to opposing occupied edges, reducing both proportionally when their preferences exceed that budget. Empty edges get zero extent. Clip each region to its allocated bounds. Test zero-sized viewports, opposing edges, empty edges, and a shrink/expand round trip.

### M5.3.2 — Resize dock edges

Add handles at the boundaries produced by the resolver. Capture the dragged edge, starting local pointer coordinate, and starting effective extent. Convert pointer movement to workspace-local Slate units and clamp it against the opposing edge and center reserve. Update the active edge's preferred extent. Release capture on mouse-up, capture loss, edge removal, group float, and workspace teardown. Preserve the preferences across viewport size changes and group float/return.

### M5.3.3 — Corner ownership and Expand Right

Extend the resolver with four independent corner assignments. Default each corner to its vertical edge. When the owner is absent, let the other occupied edge use the corner. Revalidate the Top group's attachment when executing **Expand Right** or its reverse. Change ownership without changing either edge's preferred size. Test both Top/Right arrangements and edge combinations for overlap.

### M5.3.4 — Resize panels inside a group

Build a divider between expanded participants in the current flat group. Store preferred expanded extents by panel handle. A collapsed header reserves its measured extent, and a group scrolls along its axis when minimums exceed the space. Preserve preferences during a whole-group transfer. Cancel an active drag if a participant closes, collapses, undocks, or loses capture.

### M5.3.5 — Nested splits and packaged regression

Represent layout nodes as panel leaves or ordered splits. Convert a flat group into one split on its current axis and reuse the panel size rules at each nested split. Retire empty branches when a panel leaves. Run the M5.1 and M5.2 scenarios, M5.3 geometry tests, and live layout checks in extracted Win64 Development and Shipping builds. Keep cross-launch workspace persistence and drag-to-dock outside this acceptance gate.

### Format for each implementation guide

Publish one guide per stage. Identify the starting commit or prerequisite and give exact target files and edit order. Provide the full contents of new files and complete replacement definitions for every existing function that changes. Include the required declarations, fields, includes, and module dependencies with their exact placement. For Blueprint changes, give the asset, hierarchy, settings, nodes, pins, and execution order. End with the build command, automated checks, and manual steps with expected results. Start with M5.3.0 against the committed M5.2 source, then write M5.3.1 against the reorganized tree.

## M5 acceptance and delivery order

| Gate | Evidence |
|---|---|
| M5.1 transfer | Dock and undock with a stable handle and widget state. Verify rollback, stale native callbacks, and exactly-once close. |
| M5.2 collapse | Preserve widget contents and preferred expanded size, with keyboard input leaving the hidden content. |
| M5.3 resizing | Verify dock and panel extents on all four sides, collapsed headers, minimums, scrolling, and resize cancellation. |
| Regression | Run M1 lifecycle, shutdown, and registration rollback against the updated plugin. Check M2 floating placement and M3 native activation after transfers. |
| Distribution | Run the live dock controls in Standalone Game and in an extracted Win64 Development and Shipping sample. Record engine build, commit, configuration, and results. |

Build the workspace and transfer first. Collapse depends on a working dock, and splitter behavior depends on the expanded and collapsed size rules. Keep drag-to-dock and persistent workspace layouts outside the initial acceptance gate.

## M6 — Runtime header appearance

Add one appearance profile per workspace session for the plugin's managed panel and group headers. Apply it to docked and floating headers, including ones created after a change. Expose the title font and size, title and icon colors, bar background color, header height or padding, and button and icon sizes. Include button background colors for normal, hover, and pressed states. The appearance profile applies to plugin headers. Panel content keeps its own UMG styling and Windows draws its native caption.

Expose Blueprint operations to read the current appearance, apply new settings, save them, load them, and reset to defaults. Applying or loading settings updates existing headers during play and recomputes layout when a font, padding, or size changes. Keep font choices as stable asset references and resolve them against a packaged fallback. Validate sizes and colors before applying them. Persist the appearance in a versioned file under the project's writable Saved directory. A missing file uses defaults. A failed load keeps the current appearance and reports the reason.

**Exit check.** Open docked and floating panels and a floating group. Change the bar color, title font size, and button size during play. Confirm existing and newly opened headers use the settings and that the panel's own UMG content retains its styling. Save, change the settings again, and reload without restarting. Restart and load the saved profile again.

## M7 — Save and load named workspace layouts

Expose Blueprint operations to list, save, and load named layouts during play. Support switching between multiple saved presets without restarting. Keep layout presets separate from header appearance and M2's floating-window placement file.

Use a versioned file under the project's writable Saved directory. JSON is the starting format because the nested split tree and named fields map directly to it. XML remains an option if the host needs it for interoperability. Store dock edges, corner ownership, group names and order, nested split orientation and preferred sizes, collapse state, and floating group or panel geometry. Save preferred sizes so a layout loaded in a smaller viewport can clamp them temporarily. Resolve saved floating bounds against the current monitors.

Identify panels with stable keys supplied by the host. Persist a key and enough information for the host to reopen a missing panel. Live window handles are session-specific. When loading a preset, reuse matching open widget instances and handles. Ask the host to create panels that are missing and keep unrelated open panels available. Panel-specific data inside widgets is a separate host concern.

Validate the schema, split tree, panel keys, and destinations before modifying the live workspace. Build a restore plan and report unresolved keys or unsupported versions. If restoring a target host fails, roll back the transfer and keep the current layout usable. Write presets through a temporary file before replacing an existing save.

**Exit check.** Save two layouts with different edges, corner assignments, nested splits, collapse states, and floating positions. Switch between them while the game is running and verify that matching panels keep their handles and live text. Restart and load either preset on a different viewport or monitor arrangement. Try a malformed preset and verify that the current workspace remains usable.
