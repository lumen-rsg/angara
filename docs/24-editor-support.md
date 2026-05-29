# Editor Support

IDE and editor integrations for Angara.

---

## VS Code

A full extension with LSP integration is available in `editors/vscode/`. Features:

- Syntax highlighting
- Diagnostics (errors and warnings)
- Code completion
- Hover information
- Go-to-definition

Install by copying the extension directory to your VS Code extensions folder.

## Neovim

LSP client configuration for Neovim is provided. Connect to the built-in LSP server:

```sh
angc lsp
```

Configure your Neovim LSP client to start `angc lsp` as the language server for `.an` files.

## Vim

Syntax highlighting for Vim is available in `vim-angara/`. Install with your preferred plugin manager.

## Tree-sitter

A Tree-sitter grammar is available in `tree-sitter-angara/` for syntax highlighting in any editor that supports Tree-sitter:

- Neovim (native Tree-sitter support)
- Helix
- Emacs (via tree-sitter.el)
- Zed

## LSP Server

The built-in Language Server Protocol server provides:

- **Diagnostics** -- Real-time error and warning reporting
- **Completion** -- Context-aware code completion
- **Hover** -- Type and documentation information on hover
- **Go-to-definition** -- Jump to the source of any symbol

Start the server:

```sh
angc lsp
```

## REPL

The interactive Read-Eval-Print Loop supports JIT execution via LLVM ORC:

```sh
angc repl
```

Use the REPL for experimentation, debugging, and quick prototyping.
