# pipewire-wob

Send [wob](https://github.com/francma/wob) notification on volume change in pipewire.

## Installation

You need `meson` and `wireplumber` development libraries (`wireplumber-devel` on Fedora).

Build with:

```
meson setup build
ninja -C build
```

And put `build/pipewire-wob` in `~/.local/bin` or other location.

Create the following systemd unit in `~/.config/systemd/user`:

```
[Unit]
Description=Send wob notification on volume change in pipewire
PartOf=graphical-session.target
After=wob.socket

[Service]
ExecStart=%h/.local/bin/pipewire-wob

[Install]
WantedBy=graphical-session.target
```

And activate with `systemctl --user enable --now pipewire-wob.service`.

Control volume with `wpctl`. Example `sway` config:

```
bindsym XF86AudioRaiseVolume exec wpctl set-volume @DEFAULT_AUDIO_SINK@ -l 1.0 5%+
bindsym XF86AudioLowerVolume exec wpctl set-volume @DEFAULT_AUDIO_SINK@ -l 1.0 5%-
bindsym XF86AudioMute exec wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle
```
