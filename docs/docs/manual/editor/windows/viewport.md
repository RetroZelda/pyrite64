# 3D Viewport

The 3D Viewport is the central window where you see and edit the scene. It renders the
currently loaded scene in real time, lets you navigate with a free-fly camera, and is where
objects are selected and transformed.

## Navigation

The camera is a free-fly / orbit camera. Movement only applies to the viewport your mouse
started the drag in, so multiple viewports never move in lockstep.

| Action | Control |
|--------|---------|
| **Look around (fly)** | Hold right mouse and move |
| **Move while flying** | Hold a mouse button and use `W` `A` `S` `D`, with `E` / `Q` for up / down |
| **Orbit** | `Alt` + left mouse drag |
| **Pan** | Middle mouse drag |
| **Zoom** | Mouse wheel (or two-finger scroll) |
| **Faster movement** | Hold `Shift` while moving |
| **Adjust fly speed** | Hold right mouse and scroll (when enabled in Preferences) |
| **Focus selection** | Frame the selected object(s) with the focus key |

The movement and gizmo keys depend on the active keymap preset (Blender by default) and can be
remapped in {doc}`Preferences <../../launcher>`. The current fly speed multiplier is shown on
the right of the toolbar.

### Orientation cube

The cube in the top-right corner shows the current view orientation. Click an axis to snap
the camera to that side, or drag the cube to rotate the view. Each viewport has its own cube;
it is hidden while a viewport is locked to a scene camera.

## Selecting objects

- **Click** an object to select it.
- **Click** a spline's curve to select its object. This works over empty space or level
  geometry, wherever the curve is not hidden behind level geometry; other objects standing on
  the path are still picked first.
- **Click-drag** on empty space to box-select multiple objects.
- Hold **`Ctrl`** while clicking or box-selecting to add to the current selection.

Selection is shared across all viewports and with the {doc}`Graph <sceneGraph>` window.
Only objects flagged as selectable can be picked.

## Transform gizmo

When an object is selected, a transform gizmo is drawn on it. The mode is chosen from the
toolbar (or with the gizmo keys):

| Tool | Description |
|------|-------------|
| **Translate** | Move the object along an axis or plane. |
| **Rotate** | Rotate around an axis. |
| **Scale** | Scale along an axis or uniformly. |
| **World / Local** | Toggle whether the gizmo aligns to world or to the object's local axes. |

Holding `Ctrl` while dragging snaps to fixed increments. Holding `Shift` transforms only the
object itself without carrying its children along. Each viewport keeps its own gizmo state.

(viewport-spline-editing)=
## Editing spline points

Objects with a {doc}`Spline <../components/spline>` component (for example AI patrol paths,
created with **Add Spline Path** in the {doc}`Graph <sceneGraph>` right-click menu) are drawn as
a curve with direction arrows and a green start marker. Their points are edited directly in the
viewport.

**Entering point edit**

- Select a single spline object: its points show as small outlined handles. Click a handle to
  start editing with that point selected; you can keep the mouse down and drag it right away.
  Where the object's own transform gizmo is drawn over a handle, the gizmo wins the click.
- Press the **Toggle Point Edit** key (`Tab` in the Blender keymap, `P` in Industry Compatible)
  with a single spline object selected.
- Press **Edit Points** in the Spline component in the {doc}`Object <objectInspector>` inspector,
  or click a row number in its point table.

**Leaving point edit**: press `Esc` (see below), the toggle key again, **Done** in the
inspector, or select another object or nothing (for example in the Graph, or with a click into
a viewport that shows the camera-resolution clean preview, see {ref}`viewport-spline-viewports`).

While editing, clicks, box select, `Delete` and the gizmo act on points instead of objects. The
object itself stays selected: `Delete` / `Backspace` in the {doc}`Graph <sceneGraph>` window
don't delete it either (its right-click **Delete** still does, which ends point edit). Object
copy / paste (`Ctrl+C` / `Ctrl+V`) does nothing, and the facing axes of the rotate tool are
hidden.

### Mouse

| Action | Control |
|--------|---------|
| **Select a point** | Click its handle. |
| **Toggle a point** | `Ctrl` + click its handle. The point is toggled when you release the mouse and never moves, even if you drag. |
| **Box select points** | Drag on empty space (hold `Ctrl` to add to the selection). |
| **Clear the point selection** | Click on empty space (edit mode stays active). |
| **Move points by their handle** | Press a handle without `Ctrl` and drag: the selected points follow the mouse on a horizontal plane through the grabbed point (a camera-facing plane when looking almost level). Hold `Ctrl` after pressing the handle to snap to a 0.1 m grid, press `Esc` to cancel the drag. |
| **Move points with the gizmo** | Drag the translate gizmo at the centre of the selected points. The **World / Local** toolbar toggle picks the axes, holding `Ctrl` (before or during the drag) snaps to 0.1 m. |
| **Add a point on geometry** | `Shift` + click on level geometry (**Model (Static)** meshes; elsewhere on the horizontal plane of the selected point). The point is appended when the last point (or no point) is selected, prepended when point 0 of an open path is selected, and otherwise inserted after the selected point. It then becomes the selected point, so repeated `Shift` + clicks draw a path. |
| **Insert a point on the curve** | `Shift` + click close to the curve inserts a point at exactly that spot. |

While `Shift` is held, a preview shows where the point would go: a green **+** on the curve, or a
faint line from the selected point to the spot under the mouse.

On a handle, `Ctrl` means two different things depending on when it is held: held as you press
the handle, the click toggles that point and nothing moves; held while you drag a handle you
pressed without it, the drag snaps to the grid.

### Keys

| Action | Blender | Industry Compatible | Keymap entry |
|--------|---------|---------------------|--------------|
| **Enter / leave point edit** (single selected spline object) | `Tab` | `P` | Toggle Point Edit (also works outside point edit) |
| **Extrude** from the selected point: at an open end the path continues in the same direction, elsewhere the new point goes halfway along the next segment | `E` | `N` | Extrude Point |
| **Insert** a point after each selected point (appends when nothing is selected) | `I` | `I` | Insert Point |
| **Select all / none** | `Ctrl+A` | `Ctrl+A` | Select All Points |
| **Delete** the selected points (never the object while editing) | `Delete` | `Delete` | Delete Object |
| **Snap** the selected points (all points when none are selected) to a 0.1 m grid | `Shift+S` | `Shift+S` | Snap Object |
| **Frame** the selected points (the whole path when none are selected) | `.` | `F` | Focus Object |
| **Cancel** the current drag or box, else clear the point selection, else leave point edit | `Esc` | `Esc` | fixed |

The keys can be remapped under **Spline Editing** in the keymap preferences. They only act in
the focused viewport and never while typing in a text field; all but `Esc` also do nothing
while the camera is being dragged with the right or middle mouse button.

The viewport windows don't use ImGui's keyboard navigation, so `Tab` (Toggle Point Edit in the
Blender keymap, Toggle Ortho in Industry Compatible) never moves the keyboard focus onto the
toolbar or the camera dropdown.

### Banner, undo and prefabs

- A banner at the top of every viewport shows the object and component being edited, how many
  points are selected and the main shortcuts.
- Every change is one undo step: a whole drag (gizmo or handle) is recorded when you release the
  mouse, and each add, insert, delete, extrude, snap or inspector edit is one step. The point
  selection itself is not part of the undo history.
- Undo or redo (`Ctrl+Z` / `Ctrl+Y`) while you drag points cancels the drag: nothing is
  recorded for it.
- The points of a Spline that comes from a prefab are shared by every instance of that prefab. On
  an instance they are locked: the handles are grey and entering point edit shows a
  notification. Click **Edit** on the prefab instance (prefab edit mode) to change them; the
  change then applies to every instance, marks the prefab as modified and **cannot be undone**
  (the banner says so). A Spline added directly on a prefab instance is edited like any other.

(viewport-spline-viewports)=
### Spline editing in several viewports

The point-edit session is shared: every open viewport shows the handles, the banner and the point
gizmo of the edited spline. Mouse input only goes to the viewport under the mouse and keys only to
the focused viewport, so each action happens exactly once.

A viewport in **Camera** resolution mode (the clean preview, see {ref}`viewport-resolution-mode`)
shows none of the point editor. A click on its image is a normal object click: selecting another
object (or nothing) that way ends point edit. `Esc` and the **Toggle Point Edit** key pressed in
that viewport leave point edit right away (the toggle key does not start it there).

## Toolbar

The toolbar across the top of the viewport holds, from left to right:

| Control | Description |
|---------|-------------|
| **Translate / Rotate / Scale** | The active transform gizmo mode. |
| **World / Local** | Gizmo space toggle. |
| **Grid** | Show or hide the ground grid. |
| **Collision Mesh** | Show or hide collision-mesh wireframes. |
| **Collision Bodies** | Show or hide collider shapes. |
| **Icons** | Show or hide the object billboards (camera, light, audio and generic object icons). |
| **Splines** | Show or hide spline paths. A spline whose points are being edited stays visible. |
| **Camera** | Mirror the view onto a scene camera (see below). |
| **Resolution** | Switch between free and camera resolution (see below). |
| **Cam Speed** | The current fly-speed multiplier. |

These overlay toggles are local to each viewport, so different viewports can show different
helpers at the same time.

## Placing prefabs

Drag a prefab asset from the {doc}`Files <assetBrowser>` window into the viewport to spawn an
instance. It is placed on the level geometry under the mouse (**Model (Static)** meshes), or in
front of the camera when there is none, and selected so you can position it right away.

## Camera preview

A viewport can mirror a {doc}`Camera <../components/camera>` from the scene to preview exactly
what that camera frames.

- Pick a camera from the **Camera** dropdown in the toolbar (or "Free Camera" to fly freely).
- While locked, the view continuously matches the camera object's position, rotation, FOV and
  aspect ratio. Free-fly controls and the orientation cube are disabled.
- The bound camera's own icon and frustum are hidden in that viewport so they don't block the view.

(viewport-resolution-mode)=
### Resolution mode

The **Resolution** toggle (enabled once a camera is bound) chooses how the preview is rendered:

| Mode | Description |
|------|-------------|
| **Free** | Renders at the viewport's own pixel density, letterboxed to the camera's aspect ratio. |
| **Camera** | Renders at the camera's native resolution (for example 320x240) for an authentic, low-res preview. |

In **Camera** resolution mode the viewport becomes a clean preview: the grid, collision
overlays, splines and icons are all forced off (and their toolbar toggles greyed out) so you
see only the rendered image, just like the game would output it. The spline point editor is
hidden there as well (see {ref}`viewport-spline-viewports`).

## Multiple viewports

You can open any number of viewports, each with its own camera, overlay toggles and camera
binding.

- Open a new one with `View` > `New Viewport`, or by right-clicking a viewport tab and
  choosing **New Viewport**.
- Close a viewport with the `x` on its tab, like any other window.
- Open viewports are remembered between sessions; the default layout always has one.
- `View` > `Reset Layout` collapses back to a single default viewport.

A typical use is to keep one free-fly viewport for editing and a second one locked to a
gameplay camera in camera-resolution mode to preview the final framing.

## See also

- {doc}`Camera component <../components/camera>`: the scene camera that a viewport can mirror.
- {doc}`Spline component <../components/spline>`: the path whose points are edited in the viewport.
- {doc}`Graph <sceneGraph>`: the scene hierarchy, which shares the selection with the viewport.
- {doc}`Object <objectInspector>`: edit the selected object's transform and components.
