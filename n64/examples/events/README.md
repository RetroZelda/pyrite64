# Events example

A small scene wired entirely with the Events window (the event graph): no script refers to another
object, and every connection is a binding you can see and change in the editor.

| Object | What it is | Wired to |
|---|---|---|
| Ticker | built-in **Delay**, 1 second, Repeat on | `Start` listens to `scene::Loaded`; `Elapsed` fires `scene::Ticker.Elapsed` |
| Counter | built-in **Counter**, Target 3 | `Add` listens to the ticker; `Reached` opens the door; `Reset` listens to the button |
| Door | the crate, with `ExDoor` | `Open` / `Close` slide it; `Opened` adds a point to the global **Score** system and shows the HUD banner; `Closed` hides it |
| Button | `ExButton` | `Pressed` (A on controller 1) closes the door and resets the counter |
| ExHud | UI canvas with a banner | `Show` / `Hide` pins, bound in the canvas file |
| ExScore | global script, system `Score` | inputs `Add`, `Clear`, output `Changed`; it also drives the UI and prints the score |

Open the project in the editor and pick the **Events** tab next to the 3D viewport to see the graph.
Drag from an output to an input to add a wire; click a wire and press Delete to remove it.

The same wiring can be done without the editor:

```sh
pyrite64 --cli --cmd graph --scene 1 project.p64proj
pyrite64 --cli --cmd wire --scene 1 --from 'Counter|Counter.Reached' --to 'Door|ExDoor.Open' project.p64proj
```
