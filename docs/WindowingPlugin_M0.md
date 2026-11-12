# M0 implementation notes

M0 opens UMG widgets in top-level, nonmodal Windows desktop windows. Call the API on the game thread. A null owning player uses the GameInstance. A supplied controller must be local and belong to the same GameInstance.

| API | Behavior |
|---|---|
| `OpenWidgetWindow` | Creates a new widget and publishes a transient GUID after registration and show. Failure leaves the output handle invalid. |
| `GetWindowState`, `IsWindowOpen` | Query a handle in the calling session. Unknown, foreign, and retired handles resolve to `Invalid` for the state query. |
| `SetWindowTitle` | Updates the title of an open window. |
| `RequestCloseWindow` | Accepts a request. A repeat while the record is closing returns `AlreadyClosing`. |
| `OnWindowOpened` | Fires before the open call returns. A listener can request close using the event handle. |
| `OnWindowClosed` | Fires once after native teardown, before record retirement. Shutdown can suppress delivery. |

Closing and reopening creates a fresh GUID and widget. The host owns any state it wants to retain.

## Ownership and close ordering

The GameInstance subsystem owns a session GUID. The module owns the lifetime service, which keeps each record reachable through `FGCObject` and a `TObjectPtr`. The record keeps its widget reachable through a reflected property. The native host owns its Slate window reference.

In the installed UE 5.8 source, a Slate destroy request can run close callbacks inline. The service sets state before requesting destruction. The pre-close callback records the close, then the service waits for Slate post-tick and checks that the window has left the top-level list and its native handle has cleared. An operation-depth guard prevents the drain from running inside an active plugin operation or event dispatch. GameInstance teardown closes its session's windows. Slate pre-shutdown drains remaining records before module unload.

## Validation

The UE 5.8 Win64 Development Editor build compiled and linked the M0 baseline with the `TObjectPtr` GC fix and no collector warning. The Blueprint sample smoke test was reported as working. The separate M1 C++ sample verified lifecycle, GC, rollback, and shutdown in packaged Win64 Development and Shipping. [Blueprint sample](WindowingPlugin_M0_Step_6_Test_Guide.md) · [Packaged results](WindowingPlugin_M1_Tests.md#verification)
