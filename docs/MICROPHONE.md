# Microphone access on Windows

WhisperFlowClone records through WASAPI, so it uses the **default communications/ input
device** and needs the same permission any desktop recorder needs. Nothing leaves the
machine — see the privacy note in the [README](../README.md).

## Windows 11

1. `Win + I` → **Privacy & security** → **Microphone**
   (direct link: `ms-settings:privacy-webcam` for the camera, `ms-settings:privacy-microphone` for the mic).
2. **Microphone access** → **On**.
3. **Let apps access your microphone** → **On**.
4. **Let desktop apps access your microphone** → **On**.
   This is the switch that matters here: `WhisperFlowClone.exe` is a classic Win32
   desktop application, not a Store app, so it is covered by this entry and shows up in
   the list below it after the first recording attempt.

## Windows 10

1. `Win + I` → **Privacy** → **Microphone**.
2. **Allow access to the microphone on this device** → **Change…** → **On**.
3. **Allow apps to access your microphone** → **On**.
4. **Allow desktop apps to access your microphone** → **On**.

## Pick the right input device

`Settings` → `System` → `Sound` → **Input** → choose the microphone you actually speak into
and check that the level bar moves. WhisperFlowClone always opens the *default* capture
endpoint; per-device selection is on the roadmap.

## Verify in 30 seconds

```cmd
.\build\bin\WhisperFlowClone.exe --interactive
```

Press `Enter`, speak for two seconds, press `Enter` again. You should see the
`[Audio] recording... N s` counter grow and then a transcript. If the counter stays at
`0 s`, the permission or the default device is the problem — not the recognizer.

## Paste does not work in some windows

Text is inserted with `SendInput` (`Ctrl+V`), which Windows filters through UIPI:

- A **non-elevated** `WhisperFlowClone.exe` cannot type into an **elevated** window
  (an administrator Task Manager, an installer, an elevated editor). Run the app elevated
  too if you need that.
- Some applications block synthetic input on purpose (password fields, remote desktop
  sessions, certain games). In that case the transcript stays on the clipboard and the
  console prints `[Paste failed] ... press Ctrl+V manually`.

## Antivirus / EDR

`SendInput` plus clipboard writes is exactly what keyloggers do, so aggressive EDR
products may prompt. Allow-listing `WhisperFlowClone.exe` resolves it. The binary makes no
network calls, which is easy to confirm with Resource Monitor.
