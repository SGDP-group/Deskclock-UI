# Raspberry Pi Kiosk Autostart

This folder contains a systemd setup for booting Deskclock automatically in kiosk mode on Raspberry Pi OS Lite.

## Files

- `start_kiosk.sh`: starts `libcamerify ./lvgl_app` from `/home/sdgp/UI/Deskclock-UI`
- `deskclock-kiosk.service`: systemd unit that runs the launcher as user `sdgp`
- `install_kiosk_service.sh`: installs and enables the service

## First-time setup on Raspberry Pi

From `/home/sdgp/UI/Deskclock-UI`:

```bash
chmod +x scripts/raspi/start_kiosk.sh scripts/raspi/install_kiosk_service.sh
sudo scripts/raspi/install_kiosk_service.sh
```

## Useful commands

```bash
systemctl status deskclock-kiosk.service
sudo systemctl restart deskclock-kiosk.service
journalctl -u deskclock-kiosk.service -f
```

## If camera/input permissions fail

```bash
sudo usermod -aG video,input sdgp
sudo reboot
```
