# Build and push M'DEE from an Android phone

This repository contains one firmware target: `mdee-robot` for the ESP32-S3 N16R8.

## 1. Install Termux tools

```sh
pkg update -y && pkg upgrade -y
pkg install -y git unzip python
termux-setup-storage
```

Allow storage access when Android asks.

## 2. Extract the project

Put the project ZIP in Downloads, then run:

```sh
cd ~
cp ~/storage/downloads/MDEE-single-board.zip .
unzip -o MDEE-single-board.zip
cd Update-main
ls -a
```

You should see `main`, `scripts`, `workflow_backup`, `termux_push.sh`, and `.github`.

## 3. Create a GitHub repository

Create an empty GitHub repository. Do not initialize it with a README or license.

## 4. Push the project

```sh
cd ~/Update-main
bash termux_push.sh "MDEE single-board firmware"
```

On the first run, enter your GitHub username and repository name if requested. Use a GitHub token with repository contents and workflow permissions as the HTTPS password.

The script restores `.github/workflows/mdee.yml` from `workflow_backup/mdee.yml` before pushing.

## 5. Check the build

Open the repository on GitHub, choose **Actions**, and open the latest **Build M'DEE firmware** run. The workflow uses ESP-IDF 6.1 and builds:

```sh
python scripts/build.py mdee-robot --name mdee-robot --language en-US --wake-word wn9_hijason_tts2
```

When the job succeeds, download the artifact named `mdee_<commit-sha>`. It contains `merged-binary.bin`; flash this complete image at address `0x0`.

## 6. Push later changes

```sh
cd ~/Update-main
bash termux_push.sh "describe the change"
```

## Troubleshooting

| Problem | What to check |
|---|---|
| Workflow does not appear | Check `.github/workflows/mdee.yml` and `workflow_backup/mdee.yml`; run `bash termux_push.sh` again. |
| GitHub rejects the push | Confirm the token has repository contents and workflow permissions. |
| Build fails during component resolution | Retry on a stable connection; the build downloads ESP-IDF managed components. |
| Build fails during C++ compilation | Copy the first compiler error and send it for diagnosis. Do not flash a failed or missing artifact. |
| Robot resets when motors or servos move | Disconnect the motors/servos, measure both 5 V rails, check common ground and supply current capacity. |

## Phone-first workflow notes

- Keep the hidden `.github` folder when copying the project. Use `cp -a source/. destination/`, not `source/*`.
- Do not use `git add *`; `termux_push.sh` uses `git add -A` and force-adds the workflow.
- Do not connect USB while an external 5 V rail is attached unless the board's power-path protection is confirmed.
