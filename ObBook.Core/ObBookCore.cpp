#include "ObBookCore.h"
#include <algorithm>

namespace obbook
{
    static bool IsSmartQuote(uint32_t cp)
    {
        // Common Windows smart quote codepoints.
        return (cp == 0x2018u || cp == 0x2019u || cp == 0x201Cu || cp == 0x201Du);
    }

    // Minimal UTF-8 scan: returns codepoint and advances i.
    static uint32_t NextUtf8(const std::string& s, size_t& i)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80u) { i += 1; return c; }
        if ((c >> 5) == 0x6 && i + 1 < s.size())
        {
            uint32_t cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i+1]) & 0x3Fu);
            i += 2; return cp;
        }
        if ((c >> 4) == 0xE && i + 2 < s.size())
        {
            uint32_t cp = ((c & 0x0Fu) << 12) |
                          ((static_cast<unsigned char>(s[i+1]) & 0x3Fu) << 6) |
                          (static_cast<unsigned char>(s[i+2]) & 0x3Fu);
            i += 3; return cp;
        }
        if ((c >> 3) == 0x1E && i + 3 < s.size())
        {
            uint32_t cp = ((c & 0x07u) << 18) |
                          ((static_cast<unsigned char>(s[i+1]) & 0x3Fu) << 12) |
                          ((static_cast<unsigned char>(s[i+2]) & 0x3Fu) << 6) |
                          (static_cast<unsigned char>(s[i+3]) & 0x3Fu);
            i += 4; return cp;
        }
        // Invalid; advance 1.
        i += 1;
        return 0xFFFDu;
    }

    static void AppendUtf8(std::string& out, uint32_t cp)
    {
        if (cp < 0x80u) { out.push_back(static_cast<char>(cp)); return; }
        if (cp < 0x800u)
        {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            return;
        }
        if (cp < 0x10000u)
        {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            return;
        }
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }

    BookCompiler::BookCompiler() = default;

    void BookCompiler::SetSettings(const ProjectSettings& s) { settings_ = s; }
    const ProjectSettings& BookCompiler::GetSettings() const { return settings_; }

    void BookCompiler::SetSourceUtf8(const std::string& srcUtf8) { sourceUtf8_ = srcUtf8; }
    const std::string& BookCompiler::GetSourceUtf8() const { return sourceUtf8_; }

    void BookCompiler::AddDiag(Diagnostic::Severity sev, size_t off, size_t len, const char* msg)
    {
        Diagnostic d{};
        d.severity = sev;
        d.offset = off;
        d.length = len;
        d.message = msg ? msg : "";
        diags_.push_back(std::move(d));
    }

    static bool StartsWithNoCase(const std::string& s, size_t at, const char* lit)
    {
        const size_t n = std::strlen(lit);
        if (at + n > s.size()) return false;
        for (size_t i = 0; i < n; i++)
        {
            char a = s[at+i];
            char b = lit[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    }

    void BookCompiler::Compile()
    {
        diags_.clear();
        normalizedUtf8_.clear();

        // Normalize smart quotes to ASCII " and ' when requested.
        // Also normalize backslashes to forward slashes inside IMG src=... attributes (v1 heuristic).
        if (settings_.autoNormalizeSmartQuotes)
        {
            size_t i = 0;
            size_t outOff = 0;
            while (i < sourceUtf8_.size())
            {
                size_t inOff = i;
                uint32_t cp = NextUtf8(sourceUtf8_, i);
                if (IsSmartQuote(cp))
                {
                    // Convert curly quotes to straight quote.
                    AppendUtf8(normalizedUtf8_, static_cast<uint32_t>('\"'));
                    AddDiag(Diagnostic::Severity::Warning, inOff, i - inOff, "Smart quote normalized to straight quote (\")");
                }
                else
                {
                    AppendUtf8(normalizedUtf8_, cp);
                }
                (void)outOff;
            }
        }
        else
        {
            normalizedUtf8_ = sourceUtf8_;
        }

        // Heuristic normalization for IMG src paths: replace '\' with '/' inside src="...".
        if (settings_.autoNormalizeSlashes)
        {
            std::string out;
            out.reserve(normalizedUtf8_.size());
            bool inSrc = false;
            bool inQuote = false;

            for (size_t i = 0; i < normalizedUtf8_.size(); i++)
            {
                if (!inSrc)
                {
                    if (StartsWithNoCase(normalizedUtf8_, i, "<img"))
                    {
                        inSrc = true;
                        out.push_back(normalizedUtf8_[i]);
                    }
                    else
                    {
                        out.push_back(normalizedUtf8_[i]);
                    }
                    continue;
                }

                // inside <IMG ... >
                char c = normalizedUtf8_[i];
                out.push_back(c);

                if (c == '>' ) { inSrc = false; inQuote = false; continue; }

                // detect src=
                if (StartsWithNoCase(normalizedUtf8_, i, "src="))
                {
                    // emit current char already; let normal flow continue
                    continue;
                }

                if (c == '\"') inQuote = !inQuote;

                if (inQuote && c == '\\')
                {
                    out.back() = '/';
                    AddDiag(Diagnostic::Severity::Warning, i, 1, "Backslash normalized to forward slash in IMG src path");
                }
            }

            normalizedUtf8_.swap(out);
        }

        // Validate IMG width cap (simple regex-ish scan).
        const std::string& s = normalizedUtf8_;
        for (size_t i = 0; i + 4 < s.size(); i++)
        {
            if (!StartsWithNoCase(s, i, "<img")) continue;

            size_t j = i;
            while (j < s.size() && s[j] != '>') j++;
            if (j >= s.size()) break;

            // scan for "width="
            size_t k = i;
            while (k < j)
            {
                if (StartsWithNoCase(s, k, "width="))
                {
                    k += 6;
                    // optional quote
                    bool q = false;
                    if (k < j && s[k] == '\"') { q = true; k++; }

                    int val = 0;
                    size_t start = k;
                    while (k < j && s[k] >= '0' && s[k] <= '9')
                    {
                        val = val * 10 + (s[k] - '0');
                        k++;
                    }
                    if (q && k < j && s[k] == '\"') k++;

                    if (val > static_cast<int>(settings_.maxImageWidth))
                    {
                        AddDiag(Diagnostic::Severity::Error, start, (k>start? (k-start):1), "IMG width exceeds safe maximum (default 490). Risk: crash on open.");
                    }
                }
                k++;
            }

            i = j;
        }
    }

    const std::string& BookCompiler::GetNormalizedSourceUtf8() const { return normalizedUtf8_; }
    const std::vector<Diagnostic>& BookCompiler::GetDiagnostics() const { return diags_; }

    std::string BookCompiler::ExportDescUtf8() const
    {
        return normalizedUtf8_;
    }
}
