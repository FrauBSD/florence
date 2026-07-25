# Florence

Extensible scalable virtual keyboard for X11.

This is the **FrauBSD continuation** of
[Florence](http://florence.sourceforge.net) by François Agrech, whose
SourceForge tree stalled at 0.6.3 (2014). The canonical tree is now
[FrauBSD/florence](https://github.com/FrauBSD/florence).

Florence is for you if you cannot use a hardware keyboard — because of
injury, handicap, a broken keyboard, or a tablet / convertible touchscreen —
but can use a pointing device (mouse, trackball, stylus, or touchscreen).
It stays out of your way when you don't need it, offers a timer-based
auto-click input method, and a gesture-based ramble mode.

## What's new in 0.7.0

Continuation release; see [`NEWS`](./NEWS) for the full changelog.

- Fix magnifier +/− crash (clamp scale; drop bigger/smaller from default
  layouts); **Resize** key with clamped live scale
- iPad-style **sticky modifiers**, Caps/Num lock sync from XKB,
  hold-to-repeat
- **Move** key: WM drag for mouse, seat-grab for touch
- Session placement above the taskbar; RandR portrait fit / landscape
  restore
- Framework-style **Fn row** (⅔-height F-keys, Esc placement, `KEY_FN`)
- **Fn layer** with XF86 media / brightness / display / gear symbol artwork
- Unified **greeter** binary (`--greeter` / argv0) for display manager
  login screens; prefs dialog fixes (`--export-dynamic`)

## Build

Autotools:

```sh
./configure --without-docs
gmake
gmake install
```

Dependencies: GTK+ 3, cairo, librsvg2, libxml2, gettext/intltool, and
optionally libnotify, libXtst, GStreamer.

On FreeBSD, use the port
[`x11/florence`](https://www.freshports.org/x11/florence/), which carries a
`FREEBSD_ORB` option rendering the Super key with a FreeBSD orb glyph.

## Usage

```sh
florence            # start the keyboard
florence -c         # open the configuration window
florence -G -f      # display manager greeter mode
florence hide       # control a running instance: show / hide / move x,y
```

See [`florence(1)`](./docs/florence.1.in) for the full manual.

## License

GPLv2 or later — see [`COPYING`](./COPYING). Documentation is GFDL — see
[`COPYING-DOCS`](./COPYING-DOCS).
