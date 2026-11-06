# 60 дополнительных CPU-оптимизаций Northlight

Серия 2026-11-02 / 2026-11-03. Это отдельные 60 изменений поверх предыдущей серии; исходное состояние сохранено в `.local/out/cpu60-followup-20261102/baseline/src`. Изменено 13 исходных файлов. Полный ledger с SHA-256 до/после: `.local/out/cpu60-followup-20261102/optimizations.json`.

## Проверки

Общий прогон: **256 PASS, 1 FAIL, 225 SKIP**, без timeout. Единственный FAIL — `test_world_screen_effects_gpu.py`, assert aggregate admission при искусственном ограничении VA. Тот же бинарник отдельно прошёл MSAA 0 и 2; затем штатный изолированный прогон с пересборкой прошёл **1 PASS, 0 FAIL**. Причина первого непостоянного сбоя не доказана; исходный FAIL сохранён. Итог для 257 доступных тестов с учётом повтора: **257 PASS**. SKIP не считаются успешными проверками.

Официальные результаты: `.local/out/test-output/20261102-234143/results.tsv` и `.local/out/test-output/20261102-235644/results.tsv`. Дополнительный повтор того же GPU-бинарника: `.local/out/cpu60-followup-20261102/gpu-screen-isolated-2/results.json`.

Все восемь GPU-стендов, ранее требовавших закрытой игры, теперь проверены: AO actor, AO pipeline, AO quality, HDR diffuse, projected draw audit, settings overlay, world lighting capture, world screen effects. Во время финального прогона WoW был закрыт пользователем.

Дополнительно выполнены native O0/O2 с включёнными assert для dynamic/moving probes, полного и bounded constant banks, pose groups, actor textures, particle lights, world GI и mesh plan. Переносимые actor deformation/scene job, texture worker и mesh pages выполнены через Zig; texture worker и paging прошли ASan/UBSan. Это отдельные проверки сверх prerequisites обычного runner.

Новый зарегистрированный `tests/test_cpu_optimizations.py` проверяет все 65 536 half-значений бит-в-бит; все 65 536 значений четырёх 16-битных форматов; unaligned/padded строки и SIMD tails; BC oracle по архивному decoder, uniform/mixed selectors, alpha и block sums; page eviction, block boundaries, shrink/regrow, aliasing и IndexMap epochs.

Клиентский корпус: 5 594 шейдера, 5 136 поддержанных программ, 738 304 сравнений, включая Strict, ошибки и aliasing. Отдельно — 432 неизменные actor scenes, точные вершины, нормали, UV, материалы, hash и alpha BVH. После финальной корректировки GI повторены O0/O2 dynamic/moving regressions; после inline differencing повторены O0/O2 constant/pose regressions.

## CPU A/B

Медиана девяти чередующихся раундов в одном native стенде; исходники до этой серии и текущие собраны одним Zig 0.15.2/O2. Используется production SSE2 memcmp. Это время компонентов, не игровой FPS; малые изменения около процента не подтверждают устойчивого ускорения.

| Сценарий | До, мс | После, мс | Изменение |
|---|---:|---:|---:|
| chunked_resize_assign_copy | 289.572800 | 16.074800 | -94.45% |
| spooled_push_read_resize | 9.099600 | 7.994900 | -12.14% |
| full_float_registers | 25.619000 | 24.751200 | -3.39% |
| bounded_float_registers | 36.708200 | 27.516700 | -25.04% |
| projected_overlays | 28.486700 | 28.361800 | -0.44% |
| RGB565 | 36.449200 | 4.599200 | -87.38% |
| ARGB1555 | 41.852300 | 4.804600 | -88.52% |
| XRGB1555 | 37.538900 | 4.359300 | -88.39% |
| ARGB4444 | 36.908800 | 4.308400 | -88.33% |
| dynamic_candidates_and_reuse | 6.730600 | 6.495700 | -3.49% |

Первый вариант GI-фильтра оказался медленнее и удалён: финальный код сохраняет более выгодную сортировку небольших массивов, а большие ограничивает через nth_element и sort префикса. Лог финальных измерений: `.local/out/cpu60-followup-20261102/benchmark-final.log`; воспроизведение — `build_benchmark.py` и `benchmark.cpp` в той же папке.

## Реестр 60 изменений

| № | Файл | Сокращённая CPU-работа | Проверки |
|---:|---|---|---|
| 1 | `src/gi/temporary_geometry.h` | Повторный доступ к текущей дисковой странице проходит через recent-slot без сканирования четырёх слотов. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 2 | `src/gi/temporary_geometry.h` | Буфер для существующей дисковой страницы выделяется без обнуления перед полным чтением. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 3 | `src/gi/temporary_geometry.h` | Новая дисковая страница устанавливается в кэш без обратного чтения только что записанных байтов. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 4 | `src/gi/temporary_geometry.h` | Рост дискового массива заполняет диапазоны страниц вместо push_back на каждый элемент. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 5 | `src/gi/temporary_geometry.h` | Чтение/запись нулевого диапазона сохраняет валидацию границ и пропускает seek. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 6 | `src/gi/temporary_geometry.h` | Новая страница обнуляется один раз в resident-буфере; запись файла и resize используют те же готовые данные. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 7 | `src/gi/adaptive_geometry_array.h` | Копирование chunked-массива использует диапазоны блоков вместо push_back каждого элемента. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 8 | `src/gi/adaptive_geometry_array.h` | Переход flat -> chunks переносит диапазоны, сокращая поэлементную адресацию. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 9 | `src/gi/adaptive_geometry_array.h` | Chunked resize заполняет непрерывный остаток каждого блока. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 10 | `src/gi/adaptive_geometry_array.h` | Chunked assign заполняет блоки; значение заранее сохраняется для aliasing. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 11 | `src/gi/adaptive_geometry_array.h` | Append диапазонов forward-итераторов копирует блоками, включая chunked-источники; input-итераторы сохраняют потоковый путь. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 12 | `src/gi/adaptive_geometry_array.h` | Для арифметических native-блоков используется уже нулевая память VirtualAlloc без повторного value-initialize. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 13 | `src/gi/world_dynamic_probes.h` | Два буфера bounds переиспользуют capacity между наблюдениями вместо повторных аллокаций. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 14 | `src/gi/world_dynamic_probes.h` | Пустые material bounds удаляются из списка, по которому выполняются пространственные запросы. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 15 | `src/gi/world_dynamic_probes.h` | Поиск минимальной дистанции заканчивается при нулевой дистанции. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 16 | `src/gi/world_dynamic_probes.h` | Полный вес внутри 12 ярдов возвращается без sqrt. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 17 | `src/gi/world_dynamic_probes.h` | Результат выбранного solve записывается через найденный Solved; новые записи emplace не создают и не затирают пустой Probe. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 18 | `src/gi/world_dynamic_probes.h` | Массив кандидатов сразу резервирует до bounded candidate ceiling, без начальной цепочки роста. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 19 | `src/gi/world_dynamic_probes.h` | При числе кандидатов более двух ceilings сортируется только нужный префикс; небольшие массивы сохраняют более выгодный полный sort. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 20 | `src/gi/world_dynamic_probes.h` | Массив mismatched заранее резервируется под число отобранных кандидатов. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 21 | `src/gi/world_dynamic_probes.h` | Cached-only публикация не строит массив кандидатов на solve. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 22 | `src/gi/world_dynamic_probes.h` | Сортировка возраста mismatched пропускается после исчерпания solve-бюджета. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 23 | `src/gi/world_dynamic_probes.h` | Кандидат сохраняет стабильный указатель unordered_map на Solved, исключая повторный поиск при reuse/stale blending. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 24 | `src/replay/replay_constants.h` | knownFloatSpan проверяет крайние регистры, прежде чем сравнивать всю палитру с заведомо отличающимся краем. | `test_replay_constants.cpp`, `test_replay_constant_ranges.cpp` |
| 25 | `src/replay/replay_constants.h` | Полный float-bank использует bulk/chunk span differencing вместо SSE-сравнения каждого регистра по одному; hot helper встраивается в вызывающий код. | `test_replay_constants.cpp`, `test_replay_constant_ranges.cpp` |
| 26 | `src/replay/replay_constant_ranges.h` | Полностью известный float-диапазон использует bulk/chunk поиск dirty span. | `test_replay_constant_ranges.cpp`, `test_replay_pose_groups.cpp` |
| 27 | `src/replay/replay_constant_ranges.h` | Первое обновление банка после reset сразу копирует используемый диапазон без per-register validity checks. | `test_replay_constant_ranges.cpp`, `test_replay_pose_groups.cpp` |
| 28 | `src/replay/replay_constant_ranges.h` | Первый projected overlay копирует три сегмента без поиска отличий в неизвестном банке. | `test_replay_constant_ranges.cpp`, `test_replay_pose_groups.cpp` |
| 29 | `src/replay/replay_constant_ranges.h` | Полное покрытие банка отмечается allKnown без обхода слов validity mask. | `test_replay_constant_ranges.cpp`, `test_replay_pose_groups.cpp` |
| 30 | `src/replay/replay_pose_groups.h` | Для одинаковых указателей captured float/bool/int банки не сравниваются повторно; проверки диапазонов сохраняются. | `test_replay_pose_groups.cpp` |
| 31 | `src/lights/particle_lights.h` | Дистанция нового sample до камеры используется повторно при переполнении кластеров. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 32 | `src/lights/particle_lights.h` | Уже занятые tracks пропускаются до вычисления дистанции. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 33 | `src/lights/particle_lights.h` | Массив candidates не обнуляет неиспользуемые Light; каждый читаемый элемент записан полностью. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 34 | `src/lights/particle_lights.h` | Для вывода восьми lights сортируется только используемый префикс из максимум 24. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 35 | `src/lights/particle_lights.h` | История предыдущей публикации хранит только sourceId вместо полных 48-байтовых Light. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 36 | `src/lights/particle_lights.h` | История particle IDs копируется из уже построенного immediate без повторных membership scans. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 37 | `src/replay/actor_texture.h` | BC-блоки с одними endpoint selectors не вычисляют неиспользуемые интерполированные RGB entries. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 38 | `src/replay/actor_texture.h` | BC3 с нулевыми alpha selectors не строит интерполированную alpha palette; block sum получает alpha0*16. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 39 | `src/replay/actor_texture.h` | Одноцветный selector в полном BC-блоке даёт RGB sums прямым умножением вместо обхода четырёх цветов. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 40 | `src/replay/actor_texture.h` | RGB565 декодируется SSE2 по восемь пикселей. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 41 | `src/replay/actor_texture.h` | ARGB1555/XRGB1555 декодируются SSE2 по восемь пикселей с точной alpha semantics. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 42 | `src/replay/actor_texture.h` | ARGB4444 декодируется SSE2 по восемь пикселей. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 43 | `src/replay/actor_texture.h` | Плотные 16-битные строки объединяются в один линейный диапазон, без отдельных SIMD tails на строку. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 44 | `src/replay/actor_deformation.h` | Kernel ops резервируются по известному числу операций при компиляции. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 45 | `src/replay/actor_deformation.h` | Анализ записанных компонентов исключает обнуление temp-регистров, полностью записанных до первого чтения. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 46 | `src/replay/actor_deformation.h` | Нормальные half преобразуются через точные IEEE-биты без ldexp; особые значения сохраняют прежний путь. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 47 | `src/replay/actor_deformation.h` | Weighted skinning берёт единственную literal c0 непосредственно из definitions без чтения API-банка. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 48 | `src/replay/actor_deformation.h` | Single skinning с единственной c0 definition не сканирует её для каждой строки палитры. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 49 | `src/replay/actor_deformation.h` | Identity swizzle без модификатора копирует float4 без четырёх динамических индексов. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 50 | `src/replay/actor_deformation.h` | Broadcast swizzle читает и модифицирует один компонент, затем заполняет float4. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 51 | `src/gi/world_gi.h` | Идентичные TexelView с общим указателем не сравнивают весь массив texels. | `test_world_gi.cpp`, `test_shared_mesh_preparation.py` |
| 52 | `src/gi/world_gi.cpp` | Median BVH leaf не вычисляет centroid bounds, которые не используются без разделения. | `test_world_gi.cpp`, `test_world_mesh_pages.py`, `test_shared_mesh_preparation.py` |
| 53 | `src/gi/world_gi.cpp` | SAH binning заранее выбирает активные оси вместо повторных axis-policy checks для каждого треугольника. | `test_world_gi.cpp`, `test_world_mesh_pages.py`, `test_shared_mesh_preparation.py` |
| 54 | `src/gi/world_gi.cpp` | Wrap addressing для power-of-two размеров использует mask вместо integer modulo. | `test_world_gi.cpp`, `test_world_mesh_pages.py`, `test_shared_mesh_preparation.py` |
| 55 | `src/gi/world_gi.cpp` | sRGB lookup table запрашивается один раз на bilinear sample вместо четырёх обращений к static guard. | `test_world_gi.cpp`, `test_world_mesh_pages.py`, `test_shared_mesh_preparation.py` |
| 56 | `src/world/world_mesh_plan.cpp` | Подготовленные immutable material identities заимствуются без выделения и копирования второго вектора. | `test_world_mesh_plan.cpp`, `test_shared_mesh_preparation.py`, `test_world_mesh_pages.py` |
| 57 | `src/world/world_mesh_plan.cpp` | Обе scratch hash tables (content cells и groups) ограничивают initial reserve фактическим числом треугольников. | `test_world_mesh_plan.cpp`, `test_shared_mesh_preparation.py`, `test_world_mesh_pages.py` |
| 58 | `src/world/world_mesh_pages.h` | Content anchor хэширует уже скопированные page vertices вместо трёх повторных обращений к исходной, в том числе дисковой, геометрии. | `test_world_mesh_pages.py`, `test_cpu_optimizations.py`, `test_shared_mesh_preparation.py` |
| 59 | `src/world/world_mesh_pages.h` | Batch preflight не ищет missing vertices в remap, когда страница заведомо будет открыта заново. | `test_world_mesh_pages.py`, `test_cpu_optimizations.py`, `test_shared_mesh_preparation.py` |
| 60 | `src/world/world_mesh_pages.h` | IndexMap запоминает последний hash slot, повторный find/insert того же ключа не повторяет probing; clear инвалидирует shortcut. | `test_world_mesh_pages.py`, `test_cpu_optimizations.py`, `test_shared_mesh_preparation.py` |

## Сборка

Production DLL: `renderer/frd9.dll`, release 0.3.216, x86 PE proxy, 9,089,024 bytes. Собрана штатным `python scripts/build_renderer.py`, Zig 0.15.2, с неизменными shader freshness gates; `build_packages.check_dll` прошёл. DLL в игру не устанавливалась.

Source identity: `7cae1d5ab9da4c8f6ad8af8d5eba91c567a81c9f8ecb9fed9cce0d77052a0c30`.

DLL SHA-256: `d5b45f0c1fbb35d7dcda04c20cef1d87b06263d9c222b05d0444f6cdb5d10c65`.

Машиночитаемый итог: `.local/out/cpu60-followup-20261102/validation.json`.

Буферы GI bounds сохраняют capacity двух наблюдений для сокращения аллокаций; они включены в memory accounting. Ограничения геометрии, solve budget, candidate ceiling, видимость и формат пикселей не уменьшались.
