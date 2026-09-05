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
g++ ... tests/test_*.cpp src/{AppConfig,ModelLocator,SpeechGate,WavFile}.cpp -o wftests && ./wftests
→ 28/28 test cases passed   (13 из них — новые SpeechGate и SessionGuard)
```

Эти прогоны покрывают `SpeechGate`, `SessionGuard`, `ModelLocator`, `AppConfig`, `WavFile`.
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
| `include/Hotkey.h` | хоткей — константа, а не настройка | продуктовая полнота |

## План следующих шагов

1. **Точность**: ФНЧ перед децимацией (или ресемплер средствами WASAPI) + прогон на эталонных WAV.
2. **Скорость**: стриминговый/частичный инференс, прогрев контекста, метрики hotkey→текст.
3. **Продукт**: `WIN32`-подсистема без консоли + трей-иконка, окно настроек (модель, язык, хоткей,
   устройство), автозапуск, инсталлятор.
4. **Качество**: тесты на `Hotkey`/`TextInjector` через инъекцию зависимостей, релизный артефакт
   в GitHub Releases.
