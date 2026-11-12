# Test M5.2 from Unreal Editor

## 1. Prepare the test map

Open `empty-level` and confirm it adds and registers `WBP_DockWorkspace` on BeginPlay. Use the existing `WBP_WindowingControl` in the workspace's Center box. Put the temporary test buttons below in that control, where you already store A and B's handles. Leave **Close during opened event** unchecked. Compile and save the widgets. Choose **Standalone Game** in the Editor's Play menu so you can exercise both the viewport and the native windows.

Use `WBP_WindowingSmokeTest` as the panel class. Keep its editable field for checking state. In its Designer, select the label you want to change and enable **Is Variable**. In its Graph, create a public function **Set Panel Label** with a **Text** input named `NewLabel`.

```text
Set Panel Label(NewLabel)
  → Set Text
      Target = panel's label Text Block
      In Text = NewLabel
```

## 2. Open A and B and keep their exact widget references

In `WBP_WindowingControl`, add `WidgetA` and `WidgetB` variables of type **WBP Windowing Smoke Test Object Reference**. Keep the existing **Wp Window Handle** variables for A and B. Use the same **Wp Windowing Subsystem** target you use for the other window commands. Update **Open A** and **Open B** to call **Open Panel Window**.

```text
OnClicked Open A
  → Open Panel Window
      Target = Wp Windowing Subsystem
      Widget Class = WBP_WindowingSmokeTest
      Options = existing A options, with Title = "M5.2 A"
      Owning Player = Get Owning Player
  → Break Wp Window Result (Return Value)
  → Branch (Code == Success)
      False → Print String (Code and Message)
      True  → Set HandleA = Out Handle
            → Is Valid? (Out Widget)
                Invalid → Print String ("A opened without a live widget")
                Valid   → Set WidgetA = Out Widget
                        → Set Panel Label (WidgetA, "Panel A from Blueprint")
```

With the panel class selected on the node, Out Widget has that Blueprint class as its type. Refresh the node if it still shows a base User Widget pin. Repeat for B, using `HandleB`, `WidgetB`, and different title and label text. Open both and check the labels inside the actual native windows. Type `A keeps this text` into A's editable field and `B stays open` into B's. The editable text is independent from the label.

## 3. Add an Inspect button and collapse buttons

Wire an **Inspect A** button. Use **Get Window Presentation** on the subsystem and **Break Wp Window Presentation**. Use object equality to compare the lookup with the saved reference. A closed widget reference can still be held by your test control, so check the lookup result separately.

```text
OnClicked Inspect A
  WidgetNow = Get Widget From Handle (HandleA)
  Presentation = Get Window Presentation (HandleA)
  → Print String
      Open = Is Window Open (HandleA)
      LookupValid = Is Valid (WidgetNow)
      SameInstance = LookupValid AND (WidgetNow == WidgetA)
      PresentationValid = Presentation.Is Valid
      Mode = Presentation.Mode
      Collapsed = Presentation.Collapsed
      GroupId = Presentation.Group Id
```

Read Mode and Collapsed only when PresentationValid is true. Repeat for **Inspect B** with B's variables. To test updating A through the handle later, add another button.

```text
OnClicked Rename Panel Label By Handle
  → Is Valid? (Get Widget From Handle (HandleA))
      Valid → Cast To WBP_WindowingSmokeTest
            → Set Panel Label ("Panel A updated by handle")
      Invalid → Print String ("A has no live widget")
```

Add **Collapse A via API** and **Expand A via API**. They use the same graph with different values.

```text
OnClicked Collapse A via API / Expand A via API
  → Set Window Collapsed
      Target = Wp Windowing Subsystem
      Handle = HandleA
      Collapsed = true for Collapse, false for Expand
  → Break Wp Window Result
  → Print String (Code and Message)
```

If your control already binds **On Window Presentation Changed**, extend that handler. Otherwise bind once in **Event On Initialized**. Keep the existing opened and closed counters.

```text
Event On Initialized
  → Bind Event to On Window Presentation Changed (Target = subsystem)

Custom Event PresentationChanged (Handle, Presentation)
  → PresentationCount = PresentationCount + 1
  → Print String (Handle.Id, Presentation.Mode, Presentation.Collapsed, Presentation.Group Id)
```

Record the count before each operation. A group transfer emits one presentation event per member. The group's own Collapse toggle changes the group frame, while **Inspect A/B → Collapsed** reports the individual panel body.

## 4. Check panel Settings and collapse

1. Open A and B. Inspect A. Expect `Floating`, a valid lookup, and `SameInstance = true`.
2. In A's **managed** header, select **Settings → Dock Right**. A moves to the right edge. Inspect A and expect `Docked`, a valid group ID, and the same widget. Check A's text and B's independent native window.
3. Click **Collapse** on A's panel header inside the group. The panel body hides and its header shows **Expand**. Inspect A and expect `Collapsed = true` and one presentation event. Click **Collapse A via API** while it is already collapsed. Expect `Success` with zero additional events.
4. Click **Expand A via API**. A's original text returns, `Collapsed = false`, and the count increases by one. Use **Rename Panel Label By Handle** and verify that only A's label changes.
5. To check keyboard focus specifically, use the temporary keyboard shortcut below. Focus A's editable field and use the shortcut. Type after collapse, then expand. The original text should remain. A mouse click on Collapse can move focus before the body is hidden, so the shortcut exercises the case where focus starts inside the body.

```text
In WBP_WindowingSmokeTest, override On Preview Key Down:
  If Key == F8:
      Get Wp Windowing Subsystem
      → Set Window Collapsed (Handle = this panel's test handle, Collapsed = true)
      → Return Handled
  Otherwise → Return Unhandled
```

For the shortcut, add a test-only **Wp Window Handle** variable to the panel Blueprint. Set it from `Out Handle` after validating Out Widget in Section 2. Use A while it is docked and expanded. Remove the shortcut after the focus check.

6. Collapse A again. On **A's panel header**, choose **Settings → Undock Panel**. The individual native window opens with its body visible and the same text. Inspect A for `Floating` and `Collapsed = false`. **Collapse A via API** now returns `InvalidState`. The floating header's **Collapse** button is disabled. Clicking it leaves the body visible and produces no presentation event.
7. Use A's floating **Settings → Dock Right**. A's previous docked collapse preference returns. Expand A to prepare for the group test.

## 5. Check the group header, rename, and transfer

1. Use B's floating **Settings → Dock Right**. A and B should appear in the same right group in that order. Inspect both and compare their group IDs. Both lookups should still return their original widget references.
2. Click **Collapse** on the **group** header above both panels. Both panel frames hide and the group header stays visible. Click **Expand** and confirm both fields and their text return. The opened and closed counts should stay the same.
3. Open **group Settings → Rename**, enter `Test Tools`, and press **Enter**. Confirm the group header updates. Open Rename again, submit only spaces, and verify the title stays `Test Tools`. Close the rename dialog with its native X to cancel.
4. Use **group Settings → Float Group**. Expect one native window titled `Test Tools`, with both panels inside. Inspect both and expect `In Floating Group`, the same group IDs, and the same widget references. The presentation count should increase by two. Open group Settings again and verify it offers **Return Group** and **Rename**.
5. While floating, rename the group to `Floating Tools` and verify the native title updates. Choose **Return Group**. Both panels should return to the right edge in order with the new name and original text. Expect two presentation events and zero opened or closed events for the return.

### Check horizontal collapse and maximize

1. Return A and B to individual floating windows, then dock both to Top. Collapse B from its panel header. B should use only its header width and A should take the released space. Expand B and confirm its text is still there.
2. Float the Top group and repeat the collapse and expand check. The pixels below B's header should clear when its body hides.
3. Expand both children, then collapse the group using the group header. Click the native Windows Maximize button. The group expands and both panels are visible. The group Collapse button is disabled while maximized. Restore the native window and check that the group can collapse again.
4. Return the group to Top and repeat the child check on Left or Right. A collapsed child on a vertical edge should use only its header height.

## 6. Check close and stale handles

1. Use **Close** on **A's panel header**. Inspect A after the close event. Expect `Open = false`, `LookupValid = false`, and `PresentationValid = false`. B remains open and still resolves to `WidgetB`.
2. Open A again and dock it beside B. Record the closed count. Click **Close** on the **group** header. Expect one close event for each child and the empty group header to disappear. Both handles should now return invalid widget lookups.
3. Repeat a group float and close it using the **native X**. Both children should close once. Stop Standalone with no lingering group window.

If a docking command fails with `DockWorkspaceUnavailable`, verify the map added the workspace to the viewport before registration. If you only see the old open node, reopen Unreal Editor with the updated Win64 Development Editor build loaded. The managed row places Settings, Collapse or Expand, and Close from left to right. Hover over each symbol for its label. Check that the managed title is legible after undocking and that the symbols are visible. Hold the pointer over an icon for about half a second to see its tooltip. The host project sets this delay for Slate tooltips with `Slate.TooltipSummonDelay=0.5` under `[SystemSettings]` in `Config/DefaultEngine.ini`.
