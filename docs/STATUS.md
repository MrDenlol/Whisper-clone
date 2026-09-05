# Статус проекта и план работ

Срез сделан 2026-09-05 по коммиту `b537a5f` (ветка `main` == `origin/main` == рабочая ветка сессии).

## Что проверено фактически

| Проверка | Результат |
| :--- | :--- |
| `git rev-parse HEAD` / `origin/main` | `b537a5f9e6539ff1c94e8ec4156c24c022c0c371` — оба совпадают, расхождений нет |
| Коммитов в репозитории | 1 содержательный (`feat: implement native WASAPI microphone capture...`) + стартовый |
| CI `Windows Build` run `33965133364` | `conclusion: success`, `head_sha = b537a5f`, шаги Configure/Build — success |
| Лицензия репозитория (GitHub API) | `mit` |
| Лицензия whisper.cpp (`ggml-org/whisper.cpp/LICENSE`) | MIT, `(c) 2023-2026 The ggml authors` |
| Объём кода | `include/AudioCapture.h` 33, `src/AudioCapture.cpp` 267, `src/main.cpp` 45 = 345 строк |
| Продуктовые модули (Hotkey/Transcriber/TextInjector/Tray/Settings/Vad) | отсутствуют — ни одного файла |
| `third_party/` | пуст, только `.gitkeep` |
| `tests/` | каталога нет; `WHISPERFLOW_BUILD_TESTS` OFF |

Сборка локально в песочнице не выполнялась: там нет ни `cmake`, ни MSVC/MinGW, а apt не имеет доступа к сети.
Единственный реальный прогон компилятора — CI на GitHub, и он зелёный именно на текущем HEAD.

## Готовность по слоям

1. **Инфраструктура** — готово: структура, MIT, LICENSES.md, NOTICE, .gitignore, CI.
2. **Захват звука** — готово: WASAPI shared mode + event callback, конвертация в mono float32,
   даунсемплинг до 16 кГц, вывод через `std::function`, pImpl, рабочий поток, корректный cleanup.
3. **Распознавание** — 0 %: whisper.cpp не подключён, движка нет.
4. **Продукт** — 0 %: нет глобальной горячей клавиши, вставки текста, трея, настроек, VAD.

Итого: рабочий аудиоконвейер-фундамент + консольное демо (`main.cpp` считает сэмплы).
Продукта, который можно показать жюри, пока нет — оценка ~15–20 % готовности.

## Найденные в коде риски (по строкам)

| Где | Проблема | Влияние на критерий |
| :--- | :--- | :--- |
| `AudioCapture.cpp:143`, `26–62` | Даунсемплинг 48→16 кГц линейной интерполяцией без антиалиасингового ФНЧ | **точность распознавания** |
| `AudioCapture.cpp:11` | `WIN32_LEAN_AND_MEAN` + `<stdexcept>` не включён, но `std::runtime_error` используется (111–141) | портируемость (на MSVC проходит транзитивно) |
| `AudioCapture.cpp:193, 200` | `sum / channels` — целочисленное деление до приведения к float | точность (мелочь) |
| `AudioCapture.h:22–23` | move-конструктор не обнуляет `pImpl_` источника → `stopRecording()` в деструкторе разыменует null | стабильность (латентно) |
| `AudioCapture.cpp:127` | буфер клиента 1 с при `EVENTCALLBACK` | скорость отклика (лишний запас) |
| `CMakeLists.txt:16` | `add_executable` без `WIN32` → консольное подсистемное окно | продуктовая полнота |
| `.github/workflows/windows-build.yml:5` | `push` только по `main`/`master` → фичевые ветки не проверяются | скорость отклика команды |

## Порядок работ

**Sprint 1 — замкнуть MVP-петлю (критично)**
1. Вендорить whisper.cpp в `third_party/` (submodule или subtree), снять флаг `EXCLUDE_FROM_ALL`-зависимость,
   собрать `whisper` как static, выключить `WHISPER_CPP_BUILD_EXAMPLES/TESTS`.
2. `include/Transcriber.h` + `src/Transcriber.cpp` — `std::function<void(const std::string&)>` на результат,
   preload модели на старте, инференс в отдельном потоке.
3. `include/HotkeyManager.h` — `RegisterHotKey` + message-only окно (`HWND_MESSAGE`).
4. `include/TextInjector.h` — вставка в активное окно, запасной путь через буфер обмена.
5. Скрипт скачивания модели (`*.bin`/`*.gguf` уже в `.gitignore`).

**Sprint 2 — скорость отклика**
- VAD (энергетический порог + hysteresis) для авто-остановки;
- прогрев модели и контекста до первого нажатия;
- int8-модель (`base`/`small`), `whisper_full_params` с `n_threads = hardware_concurrency`,
  `no_context = true`, `single_segment = true`;
- метрики в лог: hotkey→stop, stop→first token, total.

**Sprint 3 — продуктовая полнота**
- `WIN32` подсистема + трей-иконка, окно настроек (модель, язык, хоткей, устройство ввода),
  автозапуск, инсталлятор, артефакт `.exe` в GitHub Actions Release.

**Sprint 4 — качество и доверие**
- тесты (Catch2/GoogleTest — обе permissive) на ресемплер и VAD, `-DWHISPERFLOW_BUILD_TESTS=ON` в CI;
- триггер CI на все ветки + job с релизом;
- README привести в соответствие с реально работающим (сейчас заявлены фичи, которых в коде нет);
- в `LICENSES.md` добавить строку про веса моделей (MIT у OpenAI + оговорка research-only) и про Catch2/GoogleTest.
