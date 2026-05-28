fn main() {
    let src_dir = std::path::Path::new("src");
    let mut c_config = cc::Build::new();
    c_config.include(&src_dir);
    c_config
        .warnings(false)
        .opt_level(2)
        .file(src_dir.join("parser.c"))
        .file(src_dir.join("scanner.c"));

    let scanner_path = src_dir.join("scanner.c");
    if scanner_path.exists() {
        c_config.file(&scanner_path);
    }

    c_config.compile("tree-sitter-angara");
}
