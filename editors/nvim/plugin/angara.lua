vim.api.nvim_create_autocmd("FileType", {
  pattern = "angara",
  callback = function()
    require("angara").setup()
    vim.lsp.start({ name = "angara" })
  end,
})
