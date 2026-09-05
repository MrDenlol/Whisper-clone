# Статус проекта и план работ

Обновлено после шага «hotkey + вставка». Все утверждения ниже сверены с кодом и с прогонами,
которые реально выполнялись (команды указаны).

## Готовность по слоям

| Слой | Состояние | Чем подтверждено |
| :--- | :--- | :--- |
| 1. Инфраструктура (MIT, LICENSES.md, CI, игноры) | ✅ | `LICENSE` = MIT; CI-джобы `Windows (MSVC, Release)`, `Linux (GCC, CPU)`, `License audit` |
| 2. Захват звука (WASAPI → mono float 16 кГц) | ✅ | `src/AudioCapture.cpp`, общий `include/Resampler.h` |
| 3. Локальное распознавание (whisper.cpp v1.9.3, MIT) | ✅ | `src/Transcriber.cpp`; запуск `WhisperFlowClone` печатает `whisper.cpp 1.9.3` и список CPU-фич ggml |
| 4. Продукт: хоткей, вставка, защита сессии, фильтр тишины | ✅ | `src/Hotkey.cpp`, `src/TextInjector.cpp`, `include/SessionGuard.h`, `src/SpeechGate.cpp` |
| 5. Продукт: трей, окно настроек, скрытое окно (без консоли) | ⏳ | — |

## Что проверялось локально (песочница Linux, без MSVC и без модели)

```
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude -fsyntax-only <каждый src/*.cpp>
g++ ... tests/test_*.cpp src/{AppConfig,ModelLocator,SpeechGate,WavFile,HotkeySpec,TextInjector}.cpp \
    -o wftests && ./wftests
→ 37/37 test cases passed   (44 в проекте; 7 из них в `test_transcriber.cpp`, нужен `whisper.h`)
```

Эти прогоны покрывают `SpeechGate`, `SessionGuard`, `ModelLocator`, `AppConfig`, `HotkeySpec`,
`WavFile` и Linux-ветку `TextInjector`.
Они **не** покрывают `Transcriber.cpp` (нужен `whisper.h`) и Win32-модули `Hotkey`/`TextInjector`/
`AudioCapture` (нужен `windows.h`) — их компилирует CI на `windows-latest` с `/W4 /WX`.

Локальный прогон уже поймал две настоящие ошибки, которые иначе уехали бы в репозиторий:
неиспользуемый параметр в Linux-ветке `TextInjector::inject` (`-Werror`) и
`isMeaningfulText("[Blues] [Music]") == true` — такой текст вставлялся бы в документ.

## Что проверялось в CI (прошлый шаг, коммит `48984a4` → `main` = `ca3e121`)

`gh run view 33977217909` → `conclusion: success`, все три job'ы:
`Windows (MSVC, Release)` ✅ (configure, build, ctest, парсинг PS1, smoke-тест, артефакт),
`Linux (GCC, CPU)` ✅, `License audit (no copyleft)` ✅.

## Открытые риски (осознанные, не потерянные)

| Где | Проблема | Критерий хакатона |
| :--- | :--- | :--- |
| `include/Resampler.h` | даунсемплинг линейной интерполяцией без антиалиасингового ФНЧ | точность распознавания |
| `src/AudioCapture.cpp:127` | буфер клиента WASAPI = 1 с при `EVENTCALLBACK` | скорость отклика |
| `include/AudioCapture.h:22-23` | move-конструктор не обнуляет `pImpl_` источника | стабильность (латентно) |
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

## План следующих шагов

1. **Точность**: ФНЧ перед децимацией (или ресемплер средствами WASAPI) + прогон на эталонных WAV.
2. **Скорость**: стриминговый/частичный инференс, прогрев контекста, метрики hotkey→текст.
3. **Продукт**: `WIN32`-подсистема без консоли + трей-иконка, окно настроек (модель, язык, хоткей,
   устройство), автозапуск, инсталлятор.
4. **Качество**: тесты на `Hotkey`/`TextInjector` через инъекцию зависимостей, релизный артефакт
   в GitHub Releases.
