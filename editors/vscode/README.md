# Tamarin VS Code Extension

Official Visual Studio Code extension for the **Tamarin** programming language, powered by `tamarin-lsp`.

## Features

- **Real-Time Diagnostics**: Live compiler syntax and semantic error squiggles without having to save or manually build.
- **Hover Type Inspection**: Rich markdown tooltips displaying inferred types, parameter lists, layout directives (`@c_layout`, `@layout(scan)`), and docstrings.
- **Go-to-Definition**: Jump directly to function declarations, struct definitions, variables, and enum variants across files.
- **Find References**: Locate all usages of functions, types, and variables across your project.
- **Document Outline / Breadcrumbs**: Hierarchical symbol navigation in the editor breadcrumbs and Outline tree.
- **Code Completion**:
  - Full keyword and built-in type suggestions.
  - In-scope variables and function parameters.
  - Struct dot-completion (`v.`) listing fields and methods.
- **Signature Help**: Interactive parameter hints and active parameter tracking while typing function calls.
- **Syntax Highlighting**: Comprehensive TextMate grammar for `.tam` and `.tmn` source files.

## Installation & Setup

1. Build `tamarin-lsp` from the main repository:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```
2. Make sure `tamarin-lsp` is on your system `PATH` (or configure `tamarin.lsp.path` in VS Code settings).
3. Package and install the extension:
   ```bash
   cd editors/vscode
   npm install
   npx @vscode/vsce package
   code --install-extension tamarin-vscode-0.18.0.vsix
   ```

## Configuration

| Setting | Default | Description |
| :--- | :--- | :--- |
| `tamarin.lsp.path` | `tamarin-lsp` | Absolute path or binary name for `tamarin-lsp` |
| `tamarin.lsp.trace.server` | `off` | Trace JSON-RPC communication between VS Code and `tamarin-lsp` (`off`, `messages`, `verbose`) |
