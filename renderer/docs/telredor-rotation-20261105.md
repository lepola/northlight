# Telredor: stutters при вращении, новый отчёт 5 ноября

Исследован `qa_report/northlight-renderer.log`, 6 886 строк, SHA-256 `44f88d1bb1472624335f5ae013c30003d5dfd979f3417b3332d459b75ef58560`. BUILD source `32110bc36c1ede9d94f3a495a0c5bf0f5734a1fb3fc609c431a0b7667cdf0d13`, 17:50:28 UTC. RTX 3080, DXVK 3.1.1, 2560×1440, HDR off, MSAA 0. Это новый прогон; прежний отчёт с SHA `d53fb948...` относится к другой записи и сборке.

Снимок лога, его identity и воспроизводимый разбор сохранены в `out/telredor-rotation-20261105/{renderer.log,snapshot.json,analyze.py,analysis.json}`. Production DLL и инсталлер в этом проходе не собирались, игра не запускалась.

## Что действительно тормозит

| Участок | Наблюдение в новом логе | Что из этого следует |
|---|---|---|
| Point shadows | frame 1524: 162.927 мс CPU bucket, 17 760 draws; 17 706 replay + 54 union; 6 426 684 caster triangles | Главный измеренный всплеск — повторная отправка теневой геометрии. Это не стоимость одного fullscreen compositor shader. |
| Effects / GPU | тот же frame 1524: Effects 168.252 мс, весь GPU frame 198.852 мс; GameScene 27.548 мс | Длительность подтверждается GPU timestamps. Она может включать ожидание подачи команд; это не доказательство 168 мс чистой shader arithmetic. |
| Границы caster geometry | пример: 820 packets, только 573 valid; 292 cold visits, 45 evictions = 45 ready = 45 expired, 2 048 entries при 2.57 МиБ | Кэш упирается в количество entries и потерю source ownership proof, задолго до лимита байтов. Unbounded caster консервативно идёт во все грани каждого подходящего куба. |
| Синхронные buffer locks | окна, заканчивающиеся stream frame 1200 / 1800 / 2400: SyncMs 13.869 / 16.738 / 13.088 на кадр; 5 sync storms; к концу 24 473 NoShadow passes | Повторные Lock/Unlock прерывают параллельную подачу команд. В пике storm — 324 sync calls/frame. |
| Отказ в полном mirror | 913 732.5 МиБ суммарной длины отказов / 24 473 отказа ≈ 37.34 МиБ; full-shadow item max 24 МиБ, large allowance 36 МиБ уже занят на 34.5 МиБ | Полный mirror слишком велик для старого пути. Резервировать полный большой буфер и его кольцо ради небольших записей не требуется. По старому логу нельзя определить точный buffer ID, диапазоны и flags каждого отказа. |

Frame 1542 при focus=1 занимает 295.843 мс: scene 46.008 мс, effects 249.120 мс. Это не только редкий профильный кадр: `sampled=0`, queryCalls=0. В окне 120 кадров до frame 1564: median 76.300 мс, p95 241.705 мс, max 295.784 мс. Другое окно до frame 1924: median 73.624 мс, p95 194.310 мс.

Эти величины нельзя складывать: CPU submission, GPU elapsed time и ожидания producer частично описывают одну критическую цепочку. Устранение дорогой теневой отправки само по себе может сократить время buffer waits; удаление waits также может перенести ожидание в Present при упоре в GPU.

Отдельные bare/loading кадры 425–428 с секундами задержки не смешивались с вращением в загруженном мире. Последние окна focus=0 также не использовались как доказательство плавности foreground-игры. Строки slow/sample не являются полной выборкой для вычисления глобального p99.

## Исправление 1: готовые source envelopes переживают реконструкцию Mesh

В `src/replay/replay_envelopes.inl` оба lookup теперь используют полный immutable `replayIdentity` либо `sourceIdentity`, вместе с точным position-program/declaration key. Hash выбирает bucket, но не заменяет сравнение. Untracked geometry сохраняет прежнюю проверку raw address и weak control block.

Сохранённая source envelope не содержит текущую позу или камеру. При восстановлении Mesh optional evaluated memo освобождается; текущие constants/pose/inverseView снова участвуют в вычислении bounds. Истечение weak owner больше не обесценивает готовую immutable envelope. Кэш не удерживает тяжёлые vertex/index payload.

Лимиты 2 048 entries / 8 МиБ, рабочие бюджеты и scheduler не увеличены. Неизвестная и неподдержанная геометрия по-прежнему получает консервативную обработку. В existing bounds log добавлены `sourceIdentityHits` и `sourceReconstructedHits`, чтобы в следующем игровом логе видеть реальное переиспользование.

Контроль actual production job schedule: 48 вращений, 60 928 пересозданных packets, 1 900 source identities:

| Результат | Frozen old | Исправление |
|---|---:|---:|
| Valid current-pose bounds | 0 | 60 928 |
| Cube-face submissions | 365 568 | 61 372 |
| Source vertices rebuilt | 827 488 | 0 |

Это в 5.956 раза меньше отправок в грани на данном сценарии. Независимые проверки всех вершин подтверждают текущую позу/камеру. В GPU-контроле 144/144 R32F cube faces побайтно совпали с отправкой всех объектов во все грани: draws 6 912 → 1 350, 53 409 непустых shadow pixels.

Точный proof и команды: `out/telredor-rotation-20261105/bounds-identity/REPORT.md`. Отдельная оценка CPU самого bounds scheduler не ускорилась: warm pass около +0.036 мс; восстановление valid bounds тоже имеет стоимость. Выигрыш предназначен для устранения повторных builds и лишнего shadow submission fanout. Эти тесты не измеряют игровой FPS и не доказывают, что каждый cold packet старого лога был реконструкцией известного source.

## Исправление 2: bounded range mirror для oversized dynamic buffers

Добавлен `src/stream/buffer_range_shadow.h`; подключён к существующим Lock/Unlock в `stream_proxies.h`. Для DYNAMIC буферов больше 24 МиБ сохраняется один активный интервал точных CPU bytes: до 4 МиБ на буфер и до 8 МиБ суммарно, включая старую и новую allocation во время роста. Счётчик памяти включён в stream memory accounting.

Повторный Lock известного интервала получает те же байты через существующие Scratch/Block, а Unlock ставит в очередь обычную запись диапазона. Очередь владеет собственными upload bytes и не читает изменяемый mirror. Повторное выделение полного буфера или нового кольца не требуется.

Новый интервал после действительного DEFAULT-pool DISCARD можно инициализировать асинхронно, пока сохранены все записи после DISCARD. Это соответствует контракту D3D9: DISCARD vertex/index buffer обесценивает весь буфер, даже при частичном Lock. Источник: [Microsoft D3DLOCK](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dlock). Для managed pool действует существующая нормализация flags, поэтому его DISCARD не даёт этого исключения.

Неизвестные данные без такого proof остаются синхронными. Разрыв/вытеснение диапазона, превышение лимита и allocation failure сбрасывают proof; READONLY читает backend; ProcessVertices очищает mirror и запрещает дальнейшее переиспользование. Native Unlock failure также инвалидирует данные. Отдельный Busy mode и повторная проверка после pumped budget wait защищают от nested Lock/Unlock и записи GPU во время allocation.

В строку CSTREAM добавлены rangeShadow bytes/cap и hits/stores/refused. Следующий игровой лог покажет фактический hit rate. Число отказов старого лога не позволяет заранее утверждать, что все 13–17 мс waits исчезнут.

Проверки `tests/buffer_range_shadow_scenarios.h` через реальный production command stream и deterministic D3D9 backend:

- 12 000 случайных изменений диапазонов, DISCARD, gaps, invalidation и allocation refusal с независимой моделью bytes.
- 400 повторных VB re-locks без SyncLock/Unlock; frozen old падает на проверке отсутствия этих sync calls.
- 16 × 32 DISCARD/append writes без sync, 32 IB re-locks без sync; все bytes проверены READONLY через backend.
- Неизвестный диапазон и GPU-written buffer сохраняют ровно SyncLock + SyncUnlock.
- Настоящий pumped budget wait: nested Lock/Unlock отклонены, queued ProcessVertices инвалидирует pending fast path, outer lock использует backend.
- Zig O2 и явный UBSan PASS; полный прежний command-stream suite и threaded suite PASS. Дополнительно настоящий GCC 14 ASan/UBSan прошёл новые тесты и весь прежний suite, включая ordered uploads, lifetime, pressure, overlap/rename, reset и randomized equivalence. Работоспособность ASan отдельно подтверждена намеренной heap out-of-bounds ошибкой.

Receipts и точные команды: `out/telredor-rotation-20261105/stream/validation.json`, `verify.py`, `real-asan-validation.json`, `real_asan.py`. Zig portable tests выполнены через WSL, настоящие ASan/UBSan — GCC 14.2.0-19 в уже установленном локальном Docker image `sha256:76e563191d1ade120313a8736df24154d21da5155c0756f147c0b01bd19d9087`, с readonly source и отключённой сетью. Windows renderer integration прошла отдельную object-only компиляцию, без линковки DLL.

При аудите обнаружено, что combined `-fsanitize=address,undefined` у использованного Zig не включал ASan: в бинарнике был только UBSan. Первоначальная маркировка ASan исправлена, явный UBSan повторён, а настоящий ASan независимо проверен GCC с отрицательным runtime-контролем. Это уточнение относится и к bounds receipts; их финальный отчёт разделяет Windows Zig UBSan и Linux GCC ASan/UBSan.

## Что пока не доказано

Изображение не упрощалось: shadow resolution, caster set, distance, faces, refresh cadence, AO, weather и освещение не урезались. Контроль теневых texels и buffer bytes прошёл, но это не полный повтор исходной сцены Телредора.

В прогретом старом логе GameScene сам занимает около 22–27 мс GPU; CPU capture также остаётся заметным. Фиксы нацелены на подтверждённые крупные всплески и барьеры, а не обещают нулевую стоимость Northlight. Для оценки итоговых p95/p99/FPS нужен новый игровой прогон после будущей разрешённой сборки. По этому source-only проходу нельзя честно объявить идеальный frametime достигнутым.

Существующая `renderer/frd9.dll` сохранена: SHA-256 `3861b4c1cbe396d583513efc8d0cdeb1b2b1cd0a397bf30eb84b21de9b2ca5ea`, mtime 17:51:46 UTC. Новые изменения в исходниках в неё не включены.
