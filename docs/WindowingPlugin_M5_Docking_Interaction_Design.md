# M5 docking interaction design

## Workspace and groups

The viewport has four dock edges and a center. Each populated edge hosts a named docker group. A group owns an ordered layout of panels. Use panel handles and group IDs for identity, so two panels can share a display name. Keep the panel's original widget instance, logical handle, and floating placement key when moving it.

The three sketches define these layout transitions:

1. **Default.** The right docker owns the top-right corner. The top docker ends at the left boundary of the right docker. The left docker retains its full height.
2. **Top docker → Settings → Expand Right.** Give the top docker the top-right corner. The right docker starts below the top docker, with the same width and bottom edge. The left and bottom layout stay as drawn. Offer **Release Right Corner** while the top docker owns the corner.
3. **Right docker → Settings → Undock Group.** Move the right docker into one native window with its name, panels, order, and internal splits intact. The viewport releases its right-side allocation and recomputes the top, center, and bottom bounds. **Dock Group Right** returns the group to its last edge when that edge is free. If another group owns the edge, return an occupied-edge result and keep the group floating.

Represent each corner with one owner chosen from its two neighboring edges. When only one neighboring edge is present, give it the available corner. Recompute rectangles from the current viewport size, edge extents, and corner ownership. A command changes ownership for one corner. It does not change the size preference for either edge. Reject an expansion that would leave a neighbor below its minimum size. The same layout calculation serves resize and collapse. A fixed nested arrangement of four boxes can provide the initial M5.1 view, while changing corner ownership requires a layout that can position each region independently.

## Group layout and headers

Give each docker and each panel exactly three controls in its managed header. Use **Close**, **Collapse/Expand**, and **Settings**. Place docking and group actions in Settings. Use the same managed group header in the viewport and inside the floating host. The floating host should expose one visible set of these controls. If it uses a custom caption, preserve native drag, resize, and system-close behavior.

The group owns its name, placement, collapsed state, and ordered child layout. Use a layout tree with panel leaves and horizontal or vertical splits. Image 3 shows a vertical layout with a two-panel horizontal split in its middle row. M5.1 can start with one flat split per group and preserve that shape when moving the group. Nested splits extend the same model without changing individual panel identity.

Collapse on a panel hides its body and leaves its header. Collapse on a group hides the group body and keeps the group header. Restore the last preferred expanded extents subject to available space. Close on a panel closes that panel. The proposed group-close behavior asks for confirmation before closing its child panels. Docking, undocking, renaming, or changing corner ownership preserves all child handles and produces presentation or layout events. Closing a child still produces its normal close event once.

## Generate Settings from attachment and capabilities

Use one Settings menu renderer. Build the items each time the menu opens from a context containing the session, target kind, target ID, current attachment, edge if docked, and available capabilities. The attachment distinguishes a panel in its own native window, a panel inside a docked group, a panel inside a floating group, a group attached to a viewport edge, and a floating group. Evaluate occupancy and corner ownership against the current workspace.

| Target and attachment | Settings examples |
|---|---|
| Panel in its own native window | Dock Left, Right, Top, or Bottom where allowed |
| Panel inside a docked or floating group | Undock Panel into its own window, move to an available group if supported |
| Top group attached to the viewport | Rename, Undock Group, Expand Right or Release Right Corner |
| Other group attached to the viewport | Rename, Undock Group, eligible corner actions for its edge |
| Floating group | Rename, Dock Group to its last available edge or choose another free edge |

Define actions in a registry owned by the WindowingSubsystem or the workspace service. Each entry has a stable command ID, display label, applicability predicate, availability check, and execute handler. Applicability controls visibility. A relevant action that fails a temporary constraint can appear disabled with a reason, such as an occupied edge. Built-in actions and host-provided actions use the same descriptor and context. In C++, a provider can register another descriptor with the service. If Blueprint extension is needed, expose a provider interface that supplies descriptors and receives the selected command ID.

On selection, resolve the target again from its ID and rebuild the context. Validate session ownership, attachment, capabilities, and transfer state before dispatch. A menu opened before a panel moved or closed should refresh or dismiss. Store identity in menu callbacks and resolve objects through the service. Use unique namespaced IDs for host actions. **Rename** takes a text input, changes the group title, and preserves the group ID.

## Implementation order

1. **M5.1 — Transfer.** Finish the existing panel transfer foundation. Add group IDs and the flat ordered child model. Add a group-level transfer between one viewport edge and one native host. Treat the whole group transfer as one operation and keep its old host usable if creating the destination fails.
2. **M5.2 — Headers and Settings.** Add the three controls, collapse and expand, the command registry, and Rename. Route all docking and undocking choices through the context menu in the user-facing workspace. The test harness can still call the underlying operations directly.
3. **M5.3 — Layout and resizing.** Add the corner ownership resolver, Expand Right and its reverse, group and panel dividers, and the nested layout shown in the floating Right Docker. Keep floating group placement separate from viewport geometry.

## Live acceptance

1. Open the default workspace. Verify the top-right corner belongs to the right docker and each managed header has three controls.
2. Open Top Docker Settings and choose **Expand Right**. Confirm the top docker extends to the viewport's right edge and the right docker starts beneath it. Reverse the action and check the default geometry.
3. Rename the Right Docker, enter text in a child, and undock the group. Confirm one native group window appears with the new name, the child text, and the same child arrangement. Confirm the viewport fills the space it released. Dock the group back and verify its contents and handles.
4. Compare Settings before and after group undock. Register a test action that applies only to floating groups and verify it appears only in that context. Open a menu, change the target attachment, then attempt the old action. It should be revalidated against the current state.
5. Resize and collapse a panel and a group. Verify that the center minimum, group headers, and child contents are preserved. Repeat the transfer after collapse and after resizing.
