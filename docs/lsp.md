# Tamarin Language Server Protocol (`tamarin-lsp`)

`tamarin-lsp` is the official Language Server Protocol implementation for the Tamarin programming language. It connects directly to the compiler's frontend (`tamarin_lib`), providing real-time diagnostics, semantic navigation, type inspection, intelligent code completion, and signature hints directly inside your editor.

---

## 1. Features Supported

| LSP Feature | Method | Description |
| :--- | :--- | :--- |
| **Document Synchronization** | `textDocument/didOpen`, `didChange`, `didClose` | In-memory Virtual File System (VFS) with UTF-16 code unit position translation. |
| **Real-Time Diagnostics** | `textDocument/publishDiagnostics` | Live error and warning squiggles emitted on document change with parser recovery. |
| **Hover Tooltips** | `textDocument/hover` | Type signatures, struct layouts, doc comments, and parameter info in markdown. |
| **Go-to-Definition** | `textDocument/definition` | Direct jump to function definitions, struct declarations, fields, methods, and variables. |
| **Find References** | `textDocument/references` | Search all token occurrences and variable usages across open documents. |
| **Document Symbols** | `textDocument/documentSymbol` | Hierarchical outline tree and editor breadcrumbs for functions, structs, enums, and fields. |
| **Code Completion** | `textDocument/completion` | Full keyword suggestions, in-scope identifiers, and struct dot-completion (`v.`). |
| **Signature Help** | `textDocument/signatureHelp` | Interactive parameter lists and active parameter tracking during function calls. |

---

## 2. Editor Configuration Guide

### Visual Studio Code

Install the official extension located in `editors/vscode`:

```bash
cd editors/vscode
npm install
npx @vscode/vsce package
code --install-extension tamarin-vscode-0.18.0.vsix
```

Or configure custom server paths in `settings.json`:
```json
{
  "tamarin.lsp.path": "/usr/local/bin/tamarin-lsp"
}
```

---

### Neovim (`nvim-lspconfig`)

In your `init.lua`:

```lua
local lspconfig = require('lspconfig')
local configs = require('lspconfig.configs')

if not configs.tamarin_lsp then
  configs.tamarin_lsp = {
    default_config = {
      cmd = { 'tamarin-lsp', '--stdio' },
      filetypes = { 'tamarin' },
      root_dir = lspconfig.util.root_pattern('troop.toml', '.git'),
      settings = {},
    },
  }
end

lspconfig.tamarin_lsp.setup{
  on_attach = function(client, bufnr)
    local opts = { noremap=true, silent=true, buffer=bufnr }
    vim.keymap.set('n', 'gd', vim.lsp.buf.definition, opts)
    vim.keymap.set('n', 'K', vim.lsp.buf.hover, opts)
    vim.keymap.set('n', 'gr', vim.lsp.buf.references, opts)
    vim.keymap.set('i', '<C-k>', vim.lsp.buf.signature_help, opts)
  end
}

vim.filetype.add({
  extension = {
    tam = 'tamarin',
    tmn = 'tamarin',
  },
})
```

---

### Helix

Add the following to your `~/.config/helix/languages.toml`:

```toml
[[language]]
name = "tamarin"
scope = "source.tamarin"
injection-regex = "tamarin"
file-types = ["tam", "tmn"]
roots = ["troop.toml", ".git"]
language-servers = ["tamarin-lsp"]

[language-server.tamarin-lsp]
command = "tamarin-lsp"
args = ["--stdio"]
```

---

### Sublime Text (`LSP`)

Add to `Preferences > Package Settings > LSP > Settings`:

```json
{
  "clients": {
    "tamarin-lsp": {
      "enabled": true,
      "command": ["tamarin-lsp", "--stdio"],
      "selector": "source.tamarin"
    }
  }
}
```
