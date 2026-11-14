# M'DEE single-board firmware

M'DEE is a XiaoZhi-based ESP32-S3 N16R8 robot firmware project.

- [Wiring and power guide](WIRING_GUIDE.md)
- [Functions and controls guide](FUNCTIONS_GUIDE.md)
- [Build and push from Termux](TERMUX_GUIDE.md)
- [Complete changed/added/deleted file list](FILE_CHANGES.md)

The GitHub Actions workflow builds one merged firmware image for the ESP32-S3 N16R8. The merged image is flashed at address `0x0`.

**Important:** the source has not been compiled in this workspace. The guides distinguish implemented source features from requested behaviours that still need hardware validation or further implementation. Always test power rails with a multimeter before connecting the ESP32-S3.
