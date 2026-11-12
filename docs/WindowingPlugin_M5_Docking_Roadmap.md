# M5 roadmap — dockable windows

M5 lets a host move an existing WindowingPlugin widget between its own native Windows window and a dock in the game viewport. A docked panel can collapse to its header, expand, and resize by dragging a divider. A panel keeps its handle, widget instance, and placement key while it moves between hosts.

## API follow-up — return and look up the created widget

`OpenWidgetWindow` creates the `UUserWidget` internally and currently returns only an `FWpWindowHandle`. That prevents a Blueprint caller from keeping the exact created widget reference or setting instance-specific values after opening it. Add a Blueprint-callable `GetWidgetFromHandle` lookup and an output widget reference on `OpenWidgetWindow` (or an equivalent create-and-open result). The lookup must return the existing widget instance for a live handle, preserve identity across dock and undock, and return null for an invalid, stale, or closed handle. Update C++ and Blueprint examples to show opening a window, storing both outputs, then setting a widget variable through the returned reference. This API is required for normal caller workflows and should be scheduled before the plugin is treated as complete.

The managed header has **Close**, **Collapse/Expand**, and **Settings**. Settings supplies actions for the current panel or group and its attachment. Left and right dockers start with vertical panel layouts. Top and bottom dockers start with horizontal panel layouts. A docker group can float as one native window. The design specifies corner ownership, nested splits, and how to extend Settings. [Docking interaction design](WindowingPlugin_M5_Docking_Interaction_Design.md)

## M5.1 — Add a dock workspace and transfer a panel

Implemented. Panel dock and undock passed the host project's manual Standalone check. The sample harness also covers individual transfers on all four edges and a two-panel group round trip. The viewport sizing limitation below remains open.

Follow the file-by-file guide for the Blueprint API, workspace, transfer sequence, and live test. [M5.1 dock and undock guide](WindowingPlugin_M5_1_Dock_Undock_Guide.md)

### M5.1 layout limitation to resolve

The initial Blueprint workspace uses a fixed nested box hierarchy. A top dock and bottom dock each add their native content host to horizontal boxes. When those rows have no constrained height and the child widgets request large desired sizes, Slate can allocate more height than the viewport and push the bottom row off-screen. The M5.1 guide should treat top-plus-bottom docking as a layout verification case, and the layout must be corrected before the docking experience is considered complete. Give the center region the flexible fill space, constrain dock row heights, and define sensible minimum and maximum dock sizes. For robust resizing and corner ownership, implement the region layout with explicit sizing in M5.3 rather than relying on content desired size in nested boxes.

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

Next milestone. The implementation guide includes widget access, reusable headers, and the collapse API. [M5.2 headers, collapse, and Settings guide](WindowingPlugin_M5_2_Headers_Collapse_Settings_Guide.md)

1. Give every panel and group header exactly three controls for Close, Collapse/Expand, and Settings. Generate Settings from attachment and capabilities and add Rename for groups. Collapse hides the relevant content area and retains its widget instances. Set the body to Slate/UMG collapsed visibility so it consumes no layout space or input. Keep the header visible and focusable.
2. Add `SetWindowCollapsed(Handle, bCollapsed)` and include `bCollapsed` in the presentation snapshot. Accept collapse changes only for docked panels. Repeating the current value succeeds without an event. Undocking always shows the floating content. A later dock can restore that panel's last docked collapse preference.
3. Keep the last expanded size separately from the collapsed header size. Restore the preferred size on expand, subject to the current workspace constraints. When a panel collapses while a child has keyboard focus, move focus to its header so hidden text fields stop receiving input.

**Exit check.** Type into A, collapse it, and type again with its header focused. The hidden field must retain the original text. Expand A and verify its content and previous size return. Undock while collapsed and verify the floating body is visible. Collapse and expand must leave B and the lifecycle event counts unchanged.

## M5.3 — Add dock resizing

Replace the initial fixed box layout with a workspace layout that can assign each corner to either neighboring edge. Implement **Expand Right** for the top docker using that resolver before wiring its Settings action. Reuse the layout calculation during splitter changes and when a group undocks.

1. Use a vertical splitter for the top dock, middle row, and bottom dock. Within the middle row, use a horizontal splitter for the left dock, center, and right dock. Each dock uses a splitter along its own panel axis. A runtime Slate `SSplitter` wrapped by the dock workspace can supply drag capture and layout. Use Slate units for dock sizes and leave native screen coordinates to M2.
2. Store a preferred width for left and right docks and a preferred height for top and bottom docks. Store each panel's preferred expanded extent along its dock's panel axis. Track the effective size separately, since the viewport and neighboring panels can constrain it. Reserve the main content minimum first, then header space and expanded panel minimums. Scroll along the panel axis when those minimums exceed the available space.
3. Exclude collapsed bodies from resize allocation. Hide dividers that have no expanded neighbor to resize. During a drag, clamp the split to the available space and the minimum size on each side. Cancel capture if a participant closes, collapses, undocks, or its workspace is removed. Recompute when the viewport size changes.
4. Keep preferred dock sizes and collapse state for the live session. If cross-launch dock layout restore is added, store it in a separate versioned workspace layout keyed by the existing placement profile and panel key. Preserve M2's floating placement file and defaults.

**Exit check.** Dock A and B on the same edge. Drag the edge divider and the divider between the panels. Verify both minimum sizes and the usable center area. Collapse A, resize B, then expand A. Verify that A keeps its preferred size when space allows and B remains usable. Shrink the game viewport and verify headers remain reachable, including in the scrolling stack.

## M5 acceptance and delivery order

| Gate | Evidence |
|---|---|
| M5.1 transfer | Dock and undock with a stable handle and widget state. Verify rollback, stale native callbacks, and exactly-once close. |
| M5.2 collapse | Preserve widget contents and preferred expanded size, with keyboard input leaving the hidden content. |
| M5.3 resizing | Verify dock and panel extents on all four sides, collapsed headers, minimums, scrolling, and resize cancellation. |
| Regression | Run M1 lifecycle, shutdown, and registration rollback against the updated plugin. Check M2 floating placement and M3 native activation after transfers. |
| Distribution | Run the live dock controls in Standalone Game and in an extracted Win64 Development and Shipping sample. Record engine build, commit, configuration, and results. |

Build the workspace and transfer first. Collapse depends on a working dock, and splitter behavior depends on the expanded and collapsed size rules. Keep drag-to-dock and persistent workspace layouts outside the initial acceptance gate.
