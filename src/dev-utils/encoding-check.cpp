#include <vector>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>

/*
 * utf8_check() - scans a '\0'-terminated string for invalid UTF-8 sequences.
 * Returns a pointer to the first bad byte, or NULL if the string is valid UTF-8.
 *
 * Markus Kuhn <http://www.cl.cam.ac.uk/~mgk25/> -- 2005-03-30
 * License: http://www.cl.cam.ac.uk/~mgk25/short-license.html
 * (lightly modified to also return offset info)
 */
unsigned char *utf8_check(unsigned char *s)
{
    while (*s) {
        if (*s < 0x80) {
            // 0xxxxxxx
            s++;
        } else if ((s[0] & 0xe0) == 0xc0) {
            // 110xxxxx 10xxxxxx
            if ((s[1] & 0xc0) != 0x80 ||
                (s[0] & 0xfe) == 0xc0) {                          // overlong?
                return s;
            } else {
                s += 2;
            }
        } else if ((s[0] & 0xf0) == 0xe0) {
            // 1110xxxx 10xxxxxx 10xxxxxx
            if ((s[1] & 0xc0) != 0x80 ||
                (s[2] & 0xc0) != 0x80 ||
                (s[0] == 0xe0 && (s[1] & 0xe0) == 0x80) ||        // overlong?
                (s[0] == 0xed && (s[1] & 0xe0) == 0xa0) ||        // surrogate?
                (s[0] == 0xef && s[1] == 0xbf &&
                (s[2] & 0xfe) == 0xbe)) {                          // U+FFFE or U+FFFF?
                return s;
            } else {
                s += 3;
            }
        } else if ((s[0] & 0xf8) == 0xf0) {
            // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            if ((s[1] & 0xc0) != 0x80 ||
                (s[2] & 0xc0) != 0x80 ||
                (s[3] & 0xc0) != 0x80 ||
                (s[0] == 0xf0 && (s[1] & 0xf0) == 0x80) ||        // overlong?
                (s[0] == 0xf4 && s[1] > 0x8f) || s[0] > 0xf4) {  // > U+10FFFF?
                return s;
            } else {
                s += 4;
            }
        } else {
            return s;
        }
    }
    return NULL;
}

// Returns the 1-based line number of a pointer into a buffer.
static int byte_to_line(const std::vector<char>& buffer, const unsigned char* bad_byte)
{
    int line = 1;
    const char* p = buffer.data();
    const char* end = reinterpret_cast<const char*>(bad_byte);
    while (p < end) {
        if (*p == '\n') ++line;
        ++p;
    }
    return line;
}

// ---- Single-file check/fix logic ----------------------------------------

enum class FileResult { OK, HAS_BOM, BAD_UTF8, READ_ERROR, OPEN_ERROR };

struct CheckResult {
    FileResult result = FileResult::OK;
    int bad_line      = -1;   // for BAD_UTF8: 1-based line number
    unsigned bad_byte = 0;    // for BAD_UTF8: the offending byte value
};

CheckResult check_file(const std::string& filename, std::vector<char>& buffer_out)
{
    CheckResult cr;
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        cr.result = FileResult::OPEN_ERROR;
        return cr;
    }

    const auto size = file.tellg();
    if (size == 0) {
        cr.result = FileResult::OK;
        return cr;
    }

    file.seekg(0, std::ios::beg);
    buffer_out.resize(static_cast<size_t>(size));

    if (!file.read(buffer_out.data(), size)) {
        cr.result = FileResult::READ_ERROR;
        return cr;
    }
    buffer_out.push_back('\0');

    // Check for BOM  (EF BB BF)
    if (buffer_out.size() >= 4
        && (unsigned char)buffer_out[0] == 0xEF
        && (unsigned char)buffer_out[1] == 0xBB
        && (unsigned char)buffer_out[2] == 0xBF)
    {
        cr.result = FileResult::HAS_BOM;
        return cr;
    }

    // Check UTF-8 validity
    unsigned char* bad = utf8_check(reinterpret_cast<unsigned char*>(buffer_out.data()));
    if (bad != nullptr) {
        cr.result   = FileResult::BAD_UTF8;
        cr.bad_line = byte_to_line(buffer_out, bad);
        cr.bad_byte = static_cast<unsigned>(*bad);
        return cr;
    }

    cr.result = FileResult::OK;
    return cr;
}

// Strips a UTF-8 BOM from buffer and rewrites the file.
static bool fix_bom(const std::string& filename, std::vector<char>& buffer)
{
    // buffer still has the '\0' sentinel at the end; the real data is [0..size-2]
    // BOM is the first 3 bytes.
    std::ofstream out(filename, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    // Write everything after the 3-byte BOM (skip sentinel '\0' at back)
    out.write(buffer.data() + 3, static_cast<std::streamsize>(buffer.size()) - 4);
    return out.good();
}

// ---- Printing helpers ----------------------------------------------------

static void print_separator() {
    std::cerr << "======================================================\n";
}

static void report_error(const std::string& filename,
                         const std::string& target,
                         const CheckResult& cr)
{
    print_separator();
    std::cerr << "  ENCODING ERROR\n";
    std::cerr << "  File   : " << filename << "\n";
    std::cerr << "  Target : " << target << "\n";
    switch (cr.result) {
        case FileResult::HAS_BOM:
            std::cerr << "  Problem: File is UTF-8 WITH BOM (EF BB BF)\n";
            std::cerr << "  Fix    : Run with --fix to strip the BOM automatically,\n";
            std::cerr << "           or re-save as \"UTF-8 without BOM\" in your editor.\n";
            break;
        case FileResult::BAD_UTF8:
            std::cerr << "  Problem: File contains invalid UTF-8 (first bad byte 0x"
                      << std::hex << cr.bad_byte << std::dec
                      << " at line " << cr.bad_line << ")\n";
            std::cerr << "  Fix    : Re-save the file as UTF-8 without BOM.\n";
            break;
        case FileResult::OPEN_ERROR:
            std::cerr << "  Problem: Could not open file for reading.\n";
            break;
        case FileResult::READ_ERROR:
            std::cerr << "  Problem: Could not read file contents.\n";
            break;
        default:
            break;
    }
    print_separator();
    std::cerr << std::endl;
}

// ---- --scan-dir mode -------------------------------------------------------

static bool is_source_file(const std::string& ext)
{
    // Extend this list as needed
    static const char* exts[] = {
        ".cpp", ".cxx", ".cc", ".c",
        ".hpp", ".hxx", ".hh", ".h",
        nullptr
    };
    for (int i = 0; exts[i]; ++i)
        if (ext == exts[i]) return true;
    return false;
}

static int scan_directory(const std::string& dir_path, bool fix_mode)
{
    namespace fs = std::filesystem;
    int errors = 0, fixed = 0, checked = 0;

    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(dir_path, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        // lowercase the extension for comparison
        for (auto& c : ext) c = static_cast<char>(std::tolower((unsigned char)c));
        if (!is_source_file(ext)) continue;

        ++checked;
        std::string path = entry.path().string();
        std::vector<char> buf;
        CheckResult cr = check_file(path, buf);

        if (cr.result == FileResult::OK) continue;

        if (fix_mode && cr.result == FileResult::HAS_BOM) {
            if (fix_bom(path, buf)) {
                std::cout << "  FIXED (BOM stripped): " << path << "\n";
                ++fixed;
            } else {
                std::cerr << "  FAILED to fix (could not write): " << path << "\n";
                ++errors;
            }
        } else {
            report_error(path, "<scan-dir>", cr);
            ++errors;
        }
    }

    std::cout << "\nScan complete: " << checked << " files checked";
    if (fix_mode) std::cout << ", " << fixed << " BOM(s) stripped";
    std::cout << ", " << errors << " error(s) remaining.\n";
    return errors > 0 ? -2 : 0;
}

// ---- main ------------------------------------------------------------------

static void print_usage(const char* prog) {
    std::cerr << "\nUsage (build system mode):\n";
    std::cerr << "  " << prog << " [--fix] <target> <file>\n\n";
    std::cerr << "Usage (manual scan mode):\n";
    std::cerr << "  " << prog << " --scan-dir [--fix] <directory>\n\n";
    std::cerr << "Options:\n";
    std::cerr << "  --fix       Strip UTF-8 BOM from offending file(s) in place.\n";
    std::cerr << "              (Non-UTF-8 files still require manual re-encoding.)\n";
    std::cerr << "  --scan-dir  Recursively check all .cpp/.c/.hpp/.h files in a directory.\n\n";
}

int main(int argc, char const *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }

    // Parse flags
    bool fix_mode      = false;
    bool scan_dir_mode = false;

    std::vector<const char*> positional;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fix") == 0) {
            fix_mode = true;
        } else if (std::strcmp(argv[i], "--scan-dir") == 0) {
            scan_dir_mode = true;
        } else {
            positional.push_back(argv[i]);
        }
    }

    // --scan-dir mode: encoding-check --scan-dir [--fix] <directory>
    if (scan_dir_mode) {
        if (positional.size() != 1) {
            std::cerr << "Error: --scan-dir requires exactly one directory argument.\n";
            print_usage(argv[0]);
            return -1;
        }
        return scan_directory(positional[0], fix_mode);
    }

    // Normal build-system mode: encoding-check [--fix] <target> <file>
    if (positional.size() != 2) {
        print_usage(argv[0]);
        return -1;
    }

    const std::string target   = positional[0];
    const std::string filename = positional[1];

    std::vector<char> buffer;
    CheckResult cr = check_file(filename, buffer);

    if (cr.result == FileResult::OK) {
        return 0;
    }

    if (fix_mode && cr.result == FileResult::HAS_BOM) {
        if (fix_bom(filename, buffer)) {
            std::cout << "encoding-check: Fixed (BOM stripped): " << filename << "\n";
            return 0;
        } else {
            std::cerr << "encoding-check: ERROR: Could not write fixed file: " << filename << "\n";
            return -2;
        }
    }

    // Print a loud, clear error — prefixed with "error:" so MSBuild
    // treats it as a build error and shows it prominently.
    report_error(filename, target, cr);

    // Also emit an MSBuild-compatible error line so it appears in the
    // Error List window in Visual Studio:
    //   <file>(<line>) : error ENC001 : <message>
    switch (cr.result) {
        case FileResult::HAS_BOM:
            std::cerr << filename
                      << "(1) : error ENC001 : UTF-8 BOM detected in source file."
                         " Re-save as 'UTF-8 without BOM', or rebuild with --fix.\n";
            break;
        case FileResult::BAD_UTF8:
            std::cerr << filename << "(" << cr.bad_line
                      << ") : error ENC002 : Invalid UTF-8 byte (0x"
                      << std::hex << cr.bad_byte << std::dec
                      << ") detected. Re-save file as UTF-8 without BOM.\n";
            break;
        default:
            std::cerr << filename
                      << "(1) : error ENC003 : Could not read/open source file for encoding check.\n";
            break;
    }

    return -2;
}