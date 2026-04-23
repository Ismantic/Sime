# Repository Guidelines

## Project Structure & Module Organization
`include/` and `src/` contain the C++20 input engine and language-model logic. `bin/` holds CLI entry points such as `sime`, `sime-construct`, and `sime-converter`. `pipeline/` contains training scripts, corpus inputs, and a Makefile that generates model artifacts under `pipeline/output-new/`. `Android/` is the APK project, with Java sources in `Android/app/src/main/java/`, JNI glue in `Android/app/src/main/jni/`, and unit tests in `Android/app/src/test/`. `Linux/fcitx5/` contains the optional Fcitx5 frontend plugin.

## Build, Test, and Development Commands
Build the core tools from the repository root:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Enable the Linux plugin when needed:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSIME_ENABLE_FCITX5=ON
cmake --build build
```
Train or refresh language-model assets from `pipeline/`:
```bash
make dict
make count
make construct
make compact
make convert
```
Run Android unit tests or build an APK from `Android/`:
```bash
./gradlew testDebugUnitTest
./gradlew assembleDebug
```

## Coding Style & Naming Conventions
Follow the existing style instead of reformatting aggressively. C++ uses 4-space indentation, `CMAKE_CXX_STANDARD 20`, `-Wall -Wextra -pedantic -Wconversion`, `PascalCase` for types, and `snake_case_` for private members. Java uses 4-space indentation, `PascalCase` class names, and `camelCase` methods and fields. Keep file names aligned with the main type or tool, for example `src/sime.cc` or `InputKernelTest.java`.

## Testing Guidelines
Android logic is covered with JUnit 4 tests under `Android/app/src/test/java/com/semantic/sime/ime/`. Name new tests `*Test.java` and add focused cases around buffer state, candidate selection, and mode switching. There is no committed C++ unit-test suite yet, so validate engine changes by building the CLI tools and exercising `./build/sime` with representative dictionaries and counts.

## Commit & Pull Request Guidelines
Recent commits use short, imperative subjects with tight scope, for example `T9`, `Speed`, and `DecodeSentence`. Keep commit titles concise and topic-focused. Pull requests should explain the affected surface area (`src/`, `pipeline/`, `Android/`, or `Linux/fcitx5/`), list exact verification commands, and include screenshots or screen recordings for keyboard/UI changes. Do not commit large generated corpora, model outputs, keystores, or local SDK configuration.
