# blockzip

Консольный архиватор на C++20: блочное сжатие LZ77 + канонический Хаффман,
собственный формат файла, потоковая обработка и многопоточность.

Требования и этапы приёмки — в [SPEC.md](SPEC.md).

## Требования

- Компилятор с поддержкой C++20 (GCC 12+, Clang 15+).
- CMake 3.20+.
- Сеть при первой конфигурации: GoogleTest подтягивается через `FetchContent`
  (если в системе нет установленного GoogleTest).

## Сборка и тесты

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Строгая сборка (предупреждения как ошибки):

```bash
cmake -S . -B build -G Ninja -DBLOCKZIP_WERROR=ON
cmake --build build --parallel
```

## Сборка под санитайзерами

```bash
cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBLOCKZIP_SANITIZE=ON
cmake --build build-san --parallel
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ctest --test-dir build-san --output-on-failure
```

## Статус этапов

| Этап | Содержание | Статус |
| ---- | ---------- | ------ |
| 0 | Каркас: CMake, GoogleTest, CI | готово |
| 1 | BitWriter/BitReader, CRC32 | готово |
| 2 | Канонический Хаффман | готово |
| 3 | LZ77 | — |
| 4 | ICodec, контейнер, CLI | — |
| 5 | Потоковость, многопоточность | — |
| 6 | Бенчмарки | — |
| 7 | README | — |
