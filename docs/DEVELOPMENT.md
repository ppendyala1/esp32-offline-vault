# Development setup and file locations

The repository separates portable code (`core/`), ESP-IDF integration (`main/`), host tools (`tools/`), software tests (`tests/`), documentation (`docs/`), and mechanical files (`hardware/enclosure/`). Keep ESP-IDF's root `CMakeLists.txt`, partition table, and SDK configuration at the project root.

## Compilation database and editor setup

`compile_commands.json` records the compiler, include paths, defines, and flags for each C/C++ source file. It helps IntelliSense and other code-analysis tools; it is not an additional source file that you need to maintain.

From an activated ESP-IDF 6.1 terminal, generate or refresh it with:

```powershell
idf.py -B build-idf reconfigure
idf.py -B build-idf build
```

ESP-IDF generates `build-idf/compile_commands.json`. Keep it inside that ignored build directory: it contains machine-specific absolute paths. Do not copy or commit it to the repository root.

Use the ESP-IDF extension to build. For Microsoft's C/C++ extension, set the following in your local `.vscode/settings.json`:

```json
{
    "C_Cpp.default.compileCommands": "${workspaceFolder}/build-idf/compile_commands.json",
    "idf.buildPath": "${workspaceFolder}/build-idf",
    "cmake.configureOnOpen": false
}
```

Remove obsolete PlatformIO configurations that refer to `src/`, `.pio/`, or Arduino SDK headers. If `.vscode/c_cpp_properties.json` exists, its active configuration should use the same `compileCommands` path. Editor settings are local and ignored by Git.

If VS Code reports a bad CMake executable, activate the installed ESP-IDF environment and check `cmake --version`. Set the local `cmake.cmakePath` to your installed `cmake.exe` if VS Code cannot locate it, then reload the window. Avoid publishing your installation path as a project-wide requirement.

## Results, logs, and generated files

There is currently **no recorded performance benchmark dataset**. Software correctness tests, firmware image size, and CAD validity checks are separate forms of evidence; they do not measure on-device unlock latency, power consumption, or battery runtime.

| Location | Contents | In Git? |
| --- | --- | --- |
| [TESTING.md](TESTING.md) | Recorded software results, image size, and pending board acceptance checks | Yes |
| `build-idf/log/` | Local ESP-IDF stdout/stderr logs from build invocations | No |
| `build-idf/` | Compilation database, ELF, BIN, MAP, CMake cache, and other firmware build outputs | No |
| `build-host/` | Native test library, object files, local compiler/dependencies, and rendering caches | No |
| `build/desktop-demo.bin` | Desktop demo storage; may contain user data, not benchmark results | No |
| [enclosure validation](../hardware/enclosure/validation.json) | CAD solid validity and volumes; not physical fit results | Yes |
| [preview provenance](images/enclosure-preview-provenance.json) | STL hashes, triangle counts, and rendering offsets | Yes |

Host tests normally print their results to the terminal and do not save a report automatically. To retain a new run on Windows, use the commands below from the repository root after installing the dependencies described in [TESTING.md](TESTING.md):

```powershell
New-Item -ItemType Directory -Force build-host/results | Out-Null
python tests/run_tests.py --compiler build-host/deps/ziglang/zig.exe 2>&1 | Tee-Object build-host/results/core-tests.txt
python -m unittest discover -s tests -p test_provision.py -v 2>&1 | Tee-Object build-host/results/provision-tests.txt
```

Check each command's exit status as well as its log. These files describe host software tests, not ESP32 performance measurements. Keep future measured benchmark datasets under `docs/benchmarks/`, with the firmware revision, board, measurement method, units, and raw results. Add that directory when actual measurements exist.

Do not delete the whole `build/` directory as routine build cleanup: the desktop demo uses it for storage. Build caches and demo data have different lifetimes.
