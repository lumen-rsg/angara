local M = {}

function M.setup(opts)
  opts = opts or {}
  require("angara.lsp").setup(opts)
end

return M
