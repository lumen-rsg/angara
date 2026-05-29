local M = {}

M.config = {
  cmd = function()
    local path = vim.g.angara_lsp_path
      or vim.env.ANGARA_LSP_PATH
      or "angc"
    return { path, "lsp" }
  end,
  filetypes = { "angara" },
  root_markers = { ".git", ".angara", "*.abs" },
  name = "angara",
}

function M.setup(opts)
  opts = opts or {}
  local config = vim.tbl_deep_extend("force", M.config, opts)
  vim.lsp.config("angara", config)
end

return M
