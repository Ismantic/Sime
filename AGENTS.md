# Repository Guidelines

## Project Structure & Module Organization
`include/` and `src/` contain the C++20 input engine and language-model logic. `bin/` holds CLI entry points such as `sime`, `sime-construct`, and `sime-converter`. `pipeline/` contains training scripts, corpus inputs, and a Makefile that generates model artifacts under `pipeline/output-new/`. Platform applications and their tests live in the separate `SimeApp` repository.

## Build, Test, and Development Commands
Build the core tools from the repository root:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Build only the reusable library with `-DSIME_BUILD_TOOLS=OFF`.
Train or refresh language-model assets from `pipeline/`:
```bash
make dict
make count
make construct
make compact
make convert
```

## Coding Style & Naming Conventions
Follow the existing style instead of reformatting aggressively. C++ uses 4-space indentation, `CMAKE_CXX_STANDARD 20`, `-Wall -Wextra -pedantic -Wconversion`, `PascalCase` for types, and `snake_case_` for private members. Keep file names aligned with the main type or tool, for example `src/sime.cc`.

## Testing Guidelines
There is no committed C++ unit-test suite yet, so validate engine changes by building the CLI tools and exercising `./build/sime` with representative dictionaries and counts. Application and UI tests belong in SimeApp.

## Commit & Pull Request Guidelines
Recent commits use short, imperative subjects with tight scope, for example `T9`, `Speed`, and `DecodeSentence`. Keep commit titles concise and topic-focused. Pull requests should explain the affected surface area (`src/`, `include/`, `bin/`, or `pipeline/`) and list exact verification commands. Do not commit large generated corpora or model outputs.
