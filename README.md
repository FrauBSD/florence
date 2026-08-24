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

## What's new in 0.7.1

See [`NEWS`](./NEWS). Highlights:

- Fn **F4 / F5 / F6** -> rewind, play/pause, fast-forward (`XF86Audio*`)
- Fn **F10 / F11** -> airplane mode (`XF86RFKill`), Print Screen (**psc**)
- Matching Fn-layer symbol artwork for those keys
- **About** dialog: FrauBSD project home, dual copyright, credits
- **Build:** `docs/florence.1` and `docs/florence_applet.1` are no longer
  vendored; `configure` writes them from `docs/*.1.in` (with or without
  `--with-docs`)
- **Build:** `gmake distclean` restores a pre-build tree without deleting
  vendored `sounds.xml` or translated `fr`/`ru` doc XML; removes
  `autom4te.cache`; implies **`clean`** (no need for `gmake clean distclean`)
- **Build:** `configure` uses unversioned `aclocal`/`automake` (no
  `automake-1.14` pin) for current FreeBSD autotools
- **Fix:** Preferences no longer exit on open (`florence.rng` accepts
  `resize`/`fn` layout actions added in 0.7.0)
- **Build:** `PKG_CHECK_MODULES(xrandr)` — link via `XRANDR_LIBS` (with
  `-L…`); FreeBSD ports can drop `LDFLAGS+= -lXrandr`
- **Build:** shipped `configure` is **0.7.1** with `dteske@FreeBSD.org`
  (no post-patch reinplace of `0.6.3` / `f.agrech@gmail.com`)
- **Fix:** remove spurious `wait()` from session-icon draw (SourceForge 0.6.3
  baggage; port builds tolerated it as a warning only)
- **Fix:** AT-SPI focus callback matches modern at-spi2 (in-tree builds no
  longer rely on `-Wno-error=incompatible-function-pointer-types`, which the
  FreeBSD port used for 0.7.0)

## What's new in 0.7.0

Continuation release; see [`NEWS`](./NEWS) for the full changelog.

- Fix magnifier +/− crash (clamp scale; drop bigger/smaller from default
  layouts); **Resize** key with clamped live scale
- iPad-style **sticky modifiers**, Caps/Num lock sync from XKB,
  hold-to-repeat
- **Move** key: WM drag for mouse, seat-grab for touch
- Session placement above the taskbar; RandR portrait fit / landscape
  restore
- Framework-style **Fn row** (2/3-height F-keys, Esc placement, `KEY_FN`)
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
