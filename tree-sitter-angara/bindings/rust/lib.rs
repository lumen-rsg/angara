//! This crate provides Angara language support for the [tree-sitter][] parsing library.
//!
//! Typically, you will use the [language][language func] function to add this language to a
//! tree-sitter [Parser][], and then use the parser to parse some code:
//!
//! ```
//! let code = r#"
//!     attach io;
//!     func main() {
//!         io.println(1, "Hello, world!");
//!     }
//! "#;
//! let mut parser = tree_sitter::Parser::new();
//! parser.set_language(&tree_sitter_angara::language()).expect("Error loading Angara grammar");
//! let tree = parser.parse(code, None).unwrap();
//! assert!(!tree.root_node().has_error());
//! ```
//!
//! [Parser]: https://docs.rs/tree-sitter/*/tree_sitter/struct.Parser.html
//! [tree-sitter]: https://tree-sitter.github.io/

use tree_sitter_language::LanguageFn;

extern "C" {
    fn tree_sitter_angara() -> *const ();
}

/// The tree-sitter [`LanguageFn`] for this grammar.
///
/// [`LanguageFn`]: https://docs.rs/tree-sitter-language/*/tree_sitter_language/struct.LanguageFn.html
pub fn language() -> LanguageFn {
    LanguageFn(unsafe { tree_sitter_angara })
}

/// The content of the [`node-types.json`][] file for this grammar.
///
/// [`node-types.json`]: https://tree-sitter.github.io/tree-sitter/using-parsers#static-node-types
pub const NODE_TYPES: &str = include_str!("../../src/node-types.json");

/// The syntax highlighting query for this grammar.
pub const HIGHLIGHTS_QUERY: &str = include_str!("../../queries/highlights.scm");

/// The indentation query for this grammar.
pub const INDENTS_QUERY: &str = include_str!("../../queries/indents.scm");

/// The textobjects query for this grammar.
pub const TEXT_OBJECTS_QUERY: &str = include_str!("../../queries/textobjects.scm");

#[cfg(test)]
mod tests {
    #[test]
    fn test_can_load_grammar() {
        let mut parser = tree_sitter::Parser::new();
        parser
            .set_language(&super::language())
            .expect("Error loading Angara grammar");
    }
}
