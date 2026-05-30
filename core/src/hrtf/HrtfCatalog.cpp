// core/src/hrtf/HrtfCatalog.cpp
//
// Hand-rolled JSON parser for the small, fixed-schema catalog.json.
// No new dependency introduced — yaml-cpp (the only vendored parser) does
// not parse JSON objects, so we parse the fixed schema by hand rather than
// adding nlohmann/json for a 4-entry file.

#include "HrtfCatalog.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace spe::hrtf {

// ─── minimal JSON tokeniser ────────────────────────────────────────────────

namespace {

static bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

struct Parser {
    const char* p;
    const char* end;

    explicit Parser(const std::string& src)
        : p(src.data()), end(src.data() + src.size()) {}

    bool eof() const { return p >= end; }

    void skipWs() {
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
    }

    bool peek(char c) {
        skipWs();
        return !eof() && *p == c;
    }

    bool consume(char c) {
        skipWs();
        if (eof() || *p != c) return false;
        ++p;
        return true;
    }

    void expect(char c, const char* ctx) {
        if (!consume(c)) {
            std::string msg = "expected '";
            msg += c;
            msg += "' ";
            msg += ctx;
            throw std::runtime_error(msg);
        }
    }

    // Parse a JSON string (returns content without quotes, handles \uXXXX as-is).
    std::string parseString() {
        skipWs();
        if (eof() || *p != '"')
            throw std::runtime_error("expected '\"'");
        ++p;
        std::string out;
        while (!eof() && *p != '"') {
            if (*p == '\\') {
                ++p;
                if (eof()) throw std::runtime_error("unterminated escape");
                char esc = *p++;
                switch (esc) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'u': {
                        // skip 4 hex chars, emit '?' placeholder
                        for (int i = 0; i < 4 && !eof(); ++i) ++p;
                        out += '?';
                        break;
                    }
                    default:   out += esc;  break;
                }
            } else {
                out += *p++;
            }
        }
        if (eof()) throw std::runtime_error("unterminated string");
        ++p; // closing "
        return out;
    }

    // Parse a JSON number as int (integer portion only).
    int parseInt() {
        skipWs();
        if (eof()) throw std::runtime_error("expected number");
        bool neg = (*p == '-');
        if (neg) ++p;
        int val = 0;
        bool got = false;
        while (!eof() && std::isdigit(static_cast<unsigned char>(*p))) {
            val = val * 10 + (*p++ - '0');
            got = true;
        }
        if (!got) throw std::runtime_error("expected digit");
        return neg ? -val : val;
    }

    // Parse a JSON bool literal.
    bool parseBool() {
        skipWs();
        if (p + 4 <= end && std::strncmp(p, "true", 4) == 0) {
            p += 4; return true;
        }
        if (p + 5 <= end && std::strncmp(p, "false", 5) == 0) {
            p += 5; return false;
        }
        throw std::runtime_error("expected true|false");
    }

    // Skip any JSON value (for unknown keys).
    void skipValue() {
        skipWs();
        if (eof()) return;
        if (*p == '"') { parseString(); return; }
        if (*p == '{') {
            consume('{');
            while (!peek('}')) {
                parseString(); expect(':', "in obj"); skipValue();
                if (!consume(',')) break;
            }
            expect('}', "closing obj");
            return;
        }
        if (*p == '[') {
            consume('[');
            while (!peek(']')) {
                skipValue();
                if (!consume(',')) break;
            }
            expect(']', "closing arr");
            return;
        }
        if (p + 4 <= end && std::strncmp(p, "null",  4) == 0) { p += 4; return; }
        if (p + 4 <= end && std::strncmp(p, "true",  4) == 0) { p += 4; return; }
        if (p + 5 <= end && std::strncmp(p, "false", 5) == 0) { p += 5; return; }
        // number
        if (*p == '-' || std::isdigit(static_cast<unsigned char>(*p))) {
            while (!eof() && (*p == '-' || *p == '+' || *p == '.' ||
                              *p == 'e' || *p == 'E' ||
                              std::isdigit(static_cast<unsigned char>(*p))))
                ++p;
            return;
        }
        throw std::runtime_error(std::string("unexpected char: ") + *p);
    }

    CatalogEntry parseEntry() {
        CatalogEntry e;
        expect('{', "entry");
        while (!peek('}')) {
            std::string key = parseString();
            expect(':', "after key");
            if      (key == "name")             e.name             = parseString();
            else if (key == "display_name")     e.display_name     = parseString();
            else if (key == "speh_path")        e.speh_path        = parseString();
            else if (key == "sofa_source_url")  e.sofa_source_url  = parseString();
            else if (key == "license")          e.license          = parseString();
            else if (key == "attribution")      e.attribution      = parseString();
            else if (key == "auto_fetch")       e.auto_fetch       = parseBool();
            else if (key == "n_positions_hint") e.n_positions_hint = parseInt();
            else if (key == "ir_length_hint")   e.ir_length_hint   = parseInt();
            else skipValue();

            if (!consume(',')) break;
        }
        expect('}', "closing entry");
        return e;
    }

    std::vector<CatalogEntry> parse() {
        // Top-level object: { "hrtf_catalog": [ ... ] }
        expect('{', "top-level");
        std::vector<CatalogEntry> entries;
        while (!peek('}')) {
            std::string key = parseString();
            expect(':', "top-level key");
            if (key == "hrtf_catalog") {
                expect('[', "array");
                while (!peek(']')) {
                    entries.push_back(parseEntry());
                    if (!consume(',')) break;
                }
                expect(']', "closing array");
            } else {
                skipValue();
            }
            if (!consume(',')) break;
        }
        expect('}', "closing top-level");
        return entries;
    }
};

} // namespace

// ─── public API ───────────────────────────────────────────────────────────

CatalogResult loadCatalog(const std::string& json_path) {
    std::ifstream f(json_path);
    if (!f) {
        return {false, {}, {"cannot open: " + json_path}};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();

    std::vector<CatalogEntry> entries;
    try {
        Parser parser(src);
        entries = parser.parse();
    } catch (const std::exception& ex) {
        return {false, {}, {std::string("JSON parse error: ") + ex.what()}};
    }

    // Validate invariant: auto_fetch => license starts with "CC-BY"
    for (const auto& e : entries) {
        if (e.auto_fetch && !startsWith(e.license, "CC-BY")) {
            return {false, {},
                    {"invariant violation: entry '" + e.name +
                     "' has auto_fetch=true but license='" + e.license + "'"}};
        }
    }

    return {true, std::move(entries), {}};
}

bool HrtfCatalog::load(const std::string& json_path) {
    auto result = loadCatalog(json_path);
    if (!result.ok) {
        last_error_ = result.error.message;
        return false;
    }
    entries_    = std::move(result.entries);
    last_error_.clear();
    return true;
}

const CatalogEntry* HrtfCatalog::find(const std::string& name) const {
    for (const auto& e : entries_) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

} // namespace spe::hrtf
