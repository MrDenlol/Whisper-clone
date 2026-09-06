# Статус проекта и план работ

Обновлено после шага «трей + settings.json + история + autostart». Все утверждения ниже сверены
с кодом и с прогонами, которые реально выполнялись (команды указаны).

## Готовность по слоям

| Слой | Состояние | Чем подтверждено |
| :--- | :--- | :--- |
| 1. Инфраструктура (MIT, LICENSES.md, CI, игноры) | ✅ | `LICENSE` = MIT; CI-джобы `Windows (MSVC, Release)`, `Linux (GCC, CPU)`, `License audit` |
| 2. Захват звука (WASAPI → mono float 16 кГц) | ✅ | `src/AudioCapture.cpp`, общий `include/Resampler.h` |
| 3. Локальное распознавание (whisper.cpp v1.9.3, MIT) | ✅ | `src/Transcriber.cpp`; запуск `WhisperFlowClone` печатает `whisper.cpp 1.9.3` и список CPU-фич ggml |
| 4. Продукт: хоткей, вставка, защита сессии, фильтр тишины | ✅ | `src/Hotkey.cpp`, `src/TextInjector.cpp`, `include/SessionGuard.h`, `src/SpeechGate.cpp` |
| 5. Скорость: VAD-обрезка тишины, сжатие `audio_ctx`, потокобезопасный аккумулятор, тайминги | ✅ | `src/Vad.cpp`, `include/AudioBuffer.h`, `audioContextForSamples()` в `src/Transcriber.cpp` |
| 6. Продукт: трей, settings.json, история фраз, автозапуск, скрытое окно (без консоли) | ✅ | `src/TrayIcon.cpp`, `src/Overlay.cpp`, `src/Settings.cpp`, `src/PhraseHistory.cpp`, `src/Autostart.cpp`, `--tray` в `src/main.cpp` |
| 7. Сдача: релизная папка `dist/` (exe + MSVC DLL, без PDB), скрипт модели с SHA-1, LICENSES.md, CONTRIBUTING.md, аудит мусора/секретов в CI | ✅ | `install(... COMPONENT whisperflow)` в `CMakeLists.txt`, `scripts/package_release.ps1`, `scripts/download_model.ps1`, джоба `License audit` |

## Слой 6: трей-режим

`WhisperFlowClone --tray` поднимает общий `Win32` message loop, в котором живут:
- **трей** (`src/TrayIcon.cpp`): статус, Repeat last insertion, Language `auto/ru/en`, Model
  `tiny/base/small/medium`, Open models folder, Open settings file, Edit punctuation dictionary,
  Reload dictionary, Start with Windows (check),
  Exit. `Shell_NotifyIconW` без внешних зависимостей.
- **оверлей** (`src/Overlay.cpp`): пилюля `Listening…` / `Transcribing…` с
  `WS_EX_NOACTIVATE` — не крадёт фокус у целевого окна.
- **settings.json** (`src/Settings.cpp`): мини-JSON парсер без сторонних библиотек; портативный
  `settings.json` рядом с exe приоритетнее `%APPDATA%\WhisperFlowClone\settings.json`;
  битый/частичный файл не мешает запуску (дефолты).
- **история фраз** (`src/PhraseHistory.cpp`): ограниченная, локальная (`phrase_history.json`),
  дедуп повтора, экранирование переводов строк — питает Repeat last insertion.
- **автозапуск** (`src/Autostart.cpp`): честный `HKCU\…\CurrentVersion\Run`, команда
  `"<exe>" --tray`.
- **иконка**: собственный MIT-ресурс `assets/app.ico` (id 101), без стоковых/чужих ассетов.

## Скорость отклика (шаг «VAD + стриминг чанков»)

Задержка «отпустил клавишу → текст в поле» уменьшена двумя независимыми, складывающимися оптимизациями:

1. **Energy-VAD** (`src/Vad.cpp`, лицензионно чистый, наш MIT-код — без ONNX/Silero, без
   лишних весов и лицензий). Обрезает ведущую и хвостовую тишину: кодировщику не скармливается
   мёртвый эфир, который пользователь всегда захватывает, пока тянется к клавише и отпускает её.
2. **Сжатие `audio_ctx`** (`audioContextForSamples()`): энкодер whisper по умолчанию всегда
   обрабатывает полное окно 30 с (1500 фрейм-контекста). Для фразы 2–5 с это впустую. Контекст
   ужимается до реальной длины клипа — главный выигрыш по CPU для коротких высказываний.

Оба флага включены по умолчанию, отключаются: `--no-vad`, `--no-shrink-context` (или `vad`,
`shrink_context` в `config.ini`). Быстрый путь — «весь utterance после stop» — остаётся: инференс
запускается один раз после отпускания клавиши, без риска для точности.

**Грубый замер (модельная фраза: удержание 4.5 с = 0.8 с тишины + 2.5 с речи + 1.2 с тишины):**

| Вариант | Обрезка | `audio_ctx` фреймов | Работа энкодера vs наивный «всё + полное окно» |
| :--- | :--- | :--- | :--- |
| Наивно (без VAD, полное окно 30 с) | — | 1500 | 100% |
| Только сжатие контекста | — | 241 | ~16% |
| **VAD + сжатие (по умолчанию)** | −1.76 с тишины | 153 | **~10% (≈ в 9–10 раз меньше работы энкодера)** |

Энкодер — доминирующая часть инференса на CPU для коротких клипов, поэтому итоговая задержка
«отпустил → вставилось» падает в разы. Точные `encode_ms` / `decode_ms` теперь берутся из
`whisper_get_timings()` и логируются на каждом высказывании.

**Логи таймингов** на каждом высказывании (фоновый режим):
`[Timing] capture … | vad_trim … (kept X/Y s, cut … ms) | encode … | decode … | inject … | inference … (Zx realtime)`.

**Потокобезопасность:** аккумулятор аудио вынесен в `AudioBuffer` (`include/AudioBuffer.h`) —
один буфер с заранее зарезервированной ёмкостью (~30 с @ 16 кГц). Мелкие коллбэки WASAPI делают
`insert` без аллокации на каждый вызов; `take()` забирает накопленное одним `swap` под мьютексом
и восстанавливает ёмкость для следующей сессии.

**Стриминг во время удержания** (инференс по последним N секундам ещё до отпускания): осознанно
оставлен невыполненным на этом шаге. whisper.cpp не даёт дешёвого инкрементального энкодинга, а
частичные прогоны портят точность на границах чанков и рискуют стабильностью (критерий «не жертвуй
стабильностью»). Быстрый путь после stop + VAD + сжатие контекста уже даёт основной выигрыш; если
понадобится — точка расширения задокументирована как флаг, по умолчанию выключенный.

## Что проверялось локально (песочница Linux, без MSVC и без модели)

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWHISPERFLOW_BUILD_TESTS=ON
cmake --build build          # whisper.cpp v1.9.3 + ggml + whisperflow_core + WhisperFlowTests, -Wall -Wextra -Wpedantic -Werror
ctest --test-dir build       # 89/89 test cases passed
cmake --install build --prefix dist --component whisperflow
                             # dist/: бинарник, settings.example.json, dictionary.json, LICENSE*, NOTICE,
                             # README.md, models/README.txt, scripts/download_model.ps1 - и ничего лишнего
```

Это покрывает всё, что не зависит от `windows.h`: `Transcriber` (с настоящим `whisper.h`),
`SpeechGate`, `SessionGuard`, `ModelLocator`, `AppConfig`, `HotkeySpec`, `Settings`,
`PhraseHistory`, `TextNormalizer`, `Vad`, `WavFile`, `Autostart` (командная строка), Linux-ветку
`TextInjector`. Win32-модули (`AudioCapture`, `Hotkey`, `Overlay`, `TrayIcon`, `WinMain`) компилирует
только CI на `windows-latest` с `/W4 /WX` — он же собирает `dist/` с MSVC-DLL и проверяет, что в
нём нет `.pdb`/`.lib`/`.bin`.

## Что проверялось в CI (прошлый шаг, коммит `48984a4` → `main` = `ca3e121`)

`gh run view 33977217909` → `conclusion: success`, все три job'ы:
`Windows (MSVC, Release)` ✅ (configure, build, ctest, парсинг PS1, smoke-тест, артефакт),
`Linux (GCC, CPU)` ✅, `License audit (no copyleft)` ✅.

## Открытые риски (осознанные, не потерянные)

| Где | Проблема | Критерий хакатона |
| :--- | :--- | :--- |
| `include/Resampler.h` | даунсемплинг линейной интерполяцией без антиалиасингового ФНЧ | точность распознавания |
| `src/AudioCapture.cpp:127` | буфер клиента WASAPI = 1 с при `EVENTCALLBACK` | скорость отклика |
| `src/TextInjector.cpp` | восстанавливается только текст (`CF_UNICODETEXT`); файлы/картинки из буфера теряются | продуктовая полнота |

## Что показал первый прогон на живом Windows (не в CI)

CI только компилирует: ни микрофона, ни аудиостека, ни чужих хоткеев там нет. Первый запуск на
реальной машине вскрыл три дефекта, которые компиляция и тесты поймать не могли:

| Симптом | Причина | Исправление |
| :--- | :--- | :--- |
| `HRESULT 2147500034` при захвате | в `AudioCapture.cpp` GUID `IID_IAudioCaptureClient` был набран вручную с ошибкой в двух последних байтах → `E_NOINTERFACE` | `0x39, 0x5c, 0xd3, 0x17` |
| язык определялся, а текст был пустым | `detect_language = true` у whisper означает «определить и выйти», сегментов ноль | `language = "auto"`, `detect_language = false` |
| `RegisterHotKey failed ... (error 1409)` | Windows сама держит `Win+Space` и другие `Win+X`; комбинация `Ctrl+Win+Space` несвободна | хоткей стал настройкой, дефолт `ctrl+shift+space` |

Вывод, который стоит помнить: значения, набранные вручную (`DEFINE_GUID`, флаги чужого API),
компилируются и линкуются при любой ошибке и падают только в рантайме. Где можно — брать
константы из заголовков и линковать `uuid.lib`.

## Шаг «подготовка к сдаче» (что изменилось в коде)

- `src/main.cpp`: в трей-режиме оверлей теперь показывает `Transcribing...` (раньше — только
  `Listening...`, хотя README это обещал); удалены мёртвые `UiState`/`state_`/`captureStart_`
  и `(void)`-заглушки; при невозможности стартовать трей (хоткей занят, иконка не создалась)
  показывается `MessageBox` с причиной вместо молчаливого выхода; `WinMain` подключается к
  родительской консоли (`AttachConsole`), поэтому `--help`/`--list-models`/логи видны из
  cmd/PowerShell, а двойной клик без аргументов запускает `--tray`.
- `src/Autostart.cpp`: сборка строки без `"..." + std::string&&` — GCC 12 при `-O3` давал
  ложный `-Werror=restrict` (замечено при локальной сборке Linux-ветки).
- `scripts/download-model.*` → `scripts/download_model.*` (по ТЗ), с проверкой SHA-1.
- Убраны пустые `assets/.gitkeep`, `cmake/.gitkeep` (папки давно непустые).

## План следующих шагов

1. **Точность**: ФНЧ перед децимацией (или ресемплер средствами WASAPI) + прогон на эталонных WAV.
2. **Скорость**: стриминговый/частичный инференс, прогрев контекста, метрики hotkey→текст.
3. **Продукт**: окно настроек поверх `settings.json` (вместо ручного редактирования), выбор
   устройства микрофона, инсталлятор.
4. **Качество**: тесты на `TrayIcon`/`Overlay`/`Hotkey` через инъекцию зависимостей, релизный
   артефакт в GitHub Releases.
