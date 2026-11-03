# Debugging notes

Part of [Hemisphere](../README.md).

* **Surfaces and the lens.** Running the lens formula backwards in the
  shader for every pixel would cost several trig functions per pixel and
  wouldn't agree exactly with the ring corrections. Baking it into a lookup
  texture on the CPU (a fraction of a second, only when calibration changes)
  makes surfaces cheap and exact.
* **Settings file location.** Settings files are kept beside the program
  (via `realpath()`), so launching from another folder doesn't lose them.
* **F5 on compact keyboards.** Some small keyboards send `AC Search`
  instead of F5. Both are handled, and S saves as well.
* **Dish depth.** Recessed dots are placed along the line of sight from a
  viewer in front of the globe, which gives real parallax. Near the edge of
  the dome the near rim of the bowl hides the dots behind it.
* **Grid squares vanishing.** A straight chord between two points on the
  globe dips inside it, so wide grid squares used to look recessed and got
  hidden. Every line dot is pushed back out to the surface.
* **Bright poles.** All the meridians meet at the poles, so they are
  thinned out above 60 and 75 degrees.
* **Calibrating from a PC.** The program window only sees the Pi's own
  keyboard, so keys typed in the SSH window are read separately and fed in
  as the same key presses.
* **Slow scenes.** Liquid light, the reactor core, the hologram globe and
  the planet scan were too slow on the Pi 3 because they stacked noise for
  every pixel. All four now use sine waves. Julia's `iter` now starts at
  32 rather than 64, and the Death Star's motion trail is one faint copy
  rather than two. The newer scenes give each style its own small shader,
  so only the one on screen costs anything. If another style stutters, lower `res`, or rebuild its pattern
  from sine waves the same way.
* **The original two-pass design.** Version one filmed the model with a
  normal camera and fisheye-warped the picture. A camera only sees the
  front of a ball, so the warp put a small flat picture in the middle of the
  dome. Mapping every point straight onto the dome fixed it, and the
  surface lookup texture uses the same idea for colour.
