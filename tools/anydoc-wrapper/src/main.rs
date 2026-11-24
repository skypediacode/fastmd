//! Native command-line bridge between FastMD and AnyDoc.
//!
//! Stdout is reserved for one JSON result object so the Qt host never has to
//! scrape human-readable diagnostics. This binary is built outside FastMD's
//! CMake build, by scripts/sync-bundled-tools.ps1.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};

fn json_escape(value: &str) -> String {
    value.replace('\\', "\\\\").replace('"', "\\\"")
        .replace('\n', "\\n").replace('\r', "\\r")
}

fn fail(code: &str, message: impl AsRef<str>) -> ! {
    println!("{{\"ok\":false,\"code\":\"{}\",\"message\":\"{}\"}}",
             code, json_escape(message.as_ref()));
    std::process::exit(1);
}

fn argument(name: &str) -> String {
    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        if arg == name {
            return args.next().unwrap_or_else(|| fail("invalidArguments", format!("missing value for {name}")));
        }
    }
    fail("invalidArguments", format!("missing {name}"));
}

fn temp_sibling(path: &Path) -> PathBuf {
    let file_name = path.file_name().and_then(|n| n.to_str()).unwrap_or("output");
    path.with_file_name(format!(".{file_name}.fastmd-import-tmp"))
}

/// AnyDoc renders an embedded image as its alt text - Markdown cannot embed
/// bytes, and the crate keeps `render` private, so there is no supported way
/// to extract the assets and rewrite the links from out here. Counting them
/// lets FastMD say what was left behind instead of dropping it silently.
fn embedded_asset_count(bytes: &[u8], format: anydoc::Format) -> usize {
    // PDF converts straight to Markdown and has no document-model form.
    if format == anydoc::Format::Pdf {
        return 0;
    }
    anydoc::to_document(bytes, format).map(|doc| doc.assets.len()).unwrap_or(0)
}

fn main() {
    let input = PathBuf::from(argument("--input"));
    let output = PathBuf::from(argument("--output"));
    if !input.is_file() {
        fail("io", "input file does not exist");
    }
    if output == input {
        fail("invalidArguments", "output must not replace the source document");
    }
    // FastMD picks a free name before launching us; refuse rather than clobber
    // anything that appeared in between.
    if output.exists() {
        fail("outputConflict", "output file already exists");
    }

    let bytes = fs::read(&input).unwrap_or_else(|e| fail("io", e.to_string()));
    let format = anydoc::Format::from_bytes(&bytes)
        .or_else(|| anydoc::Format::from_path(&input))
        .unwrap_or_else(|| fail("unsupported", "unrecognized document format"));

    let dropped = embedded_asset_count(&bytes, format);
    let markdown = anydoc::to_markdown_bytes(&bytes, format)
        .unwrap_or_else(|e| fail(e.code(), e.to_string()));

    // Publish atomically, so a failed conversion never leaves a partial .md
    // where FastMD is about to open one.
    let temp = temp_sibling(&output);
    if temp.exists() {
        let _ = fs::remove_file(&temp);
    }
    fs::write(&temp, markdown.as_bytes()).unwrap_or_else(|e| fail("io", e.to_string()));
    fs::rename(&temp, &output).unwrap_or_else(|e| {
        let _ = fs::remove_file(&temp);
        fail("io", e.to_string());
    });
    println!("{{\"ok\":true,\"droppedImages\":{dropped}}}");
}
