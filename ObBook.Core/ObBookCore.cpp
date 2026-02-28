#include "ObBookCore.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace obbook
{
    static bool IsSmartQuote(uint32_t cp)
    {
        // Common Windows smart quote codepoints.
        return (cp == 0x2018u || cp == 0x2019u || cp == 0x201Cu || cp == 0x201Du);
    }

    static std::string ToLowerAscii(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
        {
            if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
            return static_cast<char>(c);
        });
        return s;
    }

    static std::string NormalizeVirtualPath(std::string path)
    {
        for (auto& c : path) if (c == '\\') c = '/';
        path = ToLowerAscii(path);
        while (!path.empty() && path.front() == '/') path.erase(path.begin());
        return path;
    }

    static bool IsBookTexturePath(const std::string& normalizedPath)
    {
        if (normalizedPath.rfind("textures/menus/book/", 0) != 0) return false;
        const auto dot = normalizedPath.find_last_of('.');
        if (dot == std::string::npos) return false;
        const auto ext = normalizedPath.substr(dot);
        return ext == ".dds" || ext == ".tga";
    }

    static bool IsBookFontPath(const std::string& normalizedPath)
    {
        if (normalizedPath.rfind("fonts/", 0) == 0) return true;
        return normalizedPath.rfind("textures/menus/book/fancy_font/", 0) == 0;
    }

    static std::string ReadBsaZString(std::ifstream& in)
    {
        std::string out;
        for (;;)
        {
            char c = 0;
            if (!in.get(c)) break;
            if (c == '\0') break;
            out.push_back(c);
        }
        return out;
    }

    struct BsaFolderRecord
    {
        uint64_t hash{};
        uint32_t fileCount{};
        uint32_t offset{};
    };

    struct BsaFileRecord
    {
        uint64_t hash{};
        uint32_t size{};
        uint32_t offset{};
    };

    struct BsaHeader
    {
        uint32_t version{};
        uint32_t dirOffset{};
        uint32_t archiveFlags{};
        uint32_t folderCount{};
        uint32_t fileCount{};
        uint32_t totalFolderNameLength{};
        uint32_t totalFileNameLength{};
        uint32_t fileFlags{};
    };

    static bool ReadExact(std::ifstream& in, void* dst, size_t size)
    {
        in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(size));
        return static_cast<size_t>(in.gcount()) == size;
    }

    static bool ReadBsaPaths(const fs::path& bsaPath, std::vector<std::string>& outPaths)
    {
        enum : uint32_t
        {
            kArchiveFlagIncludeDirectoryNames = 0x1,
            kArchiveFlagIncludeFileNames = 0x2,
        };

        std::ifstream in(bsaPath, std::ios::binary);
        if (!in) return false;

        std::array<char, 4> magic{};
        if (!ReadExact(in, magic.data(), magic.size())) return false;
        if (std::memcmp(magic.data(), "BSA\0", 4) != 0) return false;

        BsaHeader header{};
        if (!ReadExact(in, &header, sizeof(header))) return false;
        if (header.version != 103 && header.version != 104) return false;
        if (header.folderCount == 0 || header.fileCount == 0) return true;
        if (header.folderCount > 100000 || header.fileCount > 2000000) return false;

        constexpr size_t kFolderRecordSize = sizeof(BsaFolderRecord);
        constexpr size_t kFileRecordSize = sizeof(BsaFileRecord);

        std::vector<BsaFolderRecord> folders(header.folderCount);
        in.seekg(static_cast<std::streamoff>(header.dirOffset), std::ios::beg);
        for (auto& f : folders)
        {
            if (!ReadExact(in, &f, sizeof(f))) return false;
            if (f.fileCount > header.fileCount) return false;
        }

        std::vector<std::string> folderPrefixes;
        folderPrefixes.reserve(header.fileCount);
        uint64_t countedFiles = 0;

        if ((header.archiveFlags & kArchiveFlagIncludeDirectoryNames) == 0)
            return false;

        for (const auto& folder : folders)
        {
            uint8_t folderNameLen = 0;
            if (!ReadExact(in, &folderNameLen, sizeof(folderNameLen))) return false;

            std::string folderName;
            if (folderNameLen > 0)
            {
                folderName.resize(static_cast<size_t>(folderNameLen));
                if (!ReadExact(in, folderName.data(), folderName.size())) return false;

                if (!folderName.empty() && folderName.back() == '\0')
                    folderName.pop_back();
            }

            const auto safeFolder = NormalizeVirtualPath(folderName);
            for (uint32_t i = 0; i < folder.fileCount; ++i)
            {
                BsaFileRecord fr{};
                if (!ReadExact(in, &fr, sizeof(fr))) return false;
                folderPrefixes.push_back(safeFolder);
            }

            countedFiles += folder.fileCount;
            if (countedFiles > header.fileCount) return false;
        }

        if (countedFiles != header.fileCount) return false;

        if ((header.archiveFlags & kArchiveFlagIncludeFileNames) == 0)
            return false;

        const uint64_t namesOffset = static_cast<uint64_t>(header.dirOffset)
            + static_cast<uint64_t>(header.folderCount) * static_cast<uint64_t>(kFolderRecordSize)
            + static_cast<uint64_t>(header.totalFolderNameLength)
            + static_cast<uint64_t>(header.fileCount) * static_cast<uint64_t>(kFileRecordSize);

        in.seekg(static_cast<std::streamoff>(namesOffset), std::ios::beg);
        if (!in) return false;

        outPaths.reserve(outPaths.size() + folderPrefixes.size());
        for (const auto& prefix : folderPrefixes)
        {
            std::string fileName = ReadBsaZString(in);
            if (!in) return false;

            auto fileNorm = NormalizeVirtualPath(fileName);
            if (prefix.empty())
                outPaths.push_back(std::move(fileNorm));
            else
                outPaths.push_back(prefix + "/" + fileNorm);
        }

        return true;
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
                          ((static_cast<unsigned char>(s[i+2]) & 0x3Fu));
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

    void BookCompiler::SetOblivionDirectoryUtf8(const std::string& pathUtf8)
    {
        settings_.oblivionDirectoryUtf8 = pathUtf8;
    }

    const std::string& BookCompiler::GetResolvedDataDirectoryUtf8() const
    {
        return resolvedDataDirUtf8_;
    }

    const std::vector<std::string>& BookCompiler::GetBookFontAssetsUtf8() const
    {
        return bookFontAssetsUtf8_;
    }

    const std::vector<std::string>& BookCompiler::GetBookTextureAssetsUtf8() const
    {
        return bookTextureAssetsUtf8_;
    }

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

    void BookCompiler::DiscoverBookAssets()
    {
        bookFontAssetsUtf8_.clear();
        bookTextureAssetsUtf8_.clear();
        resolvedDataDirUtf8_.clear();

        std::vector<fs::path> candidates;
        if (!settings_.oblivionDirectoryUtf8.empty())
            candidates.emplace_back(fs::path(settings_.oblivionDirectoryUtf8));

        if (const char* envPath = std::getenv("OBLIVION_PATH"))
        {
            if (envPath[0] != '\0') candidates.emplace_back(fs::path(envPath));
        }

        for (const auto& c : candidates)
        {
            auto normalized = c;
            if (fs::exists(normalized / "Data")) normalized /= "Data";
            if (!fs::exists(normalized) || !fs::is_directory(normalized)) continue;
            resolvedDataDirUtf8_ = normalized.string();
            break;
        }

        if (resolvedDataDirUtf8_.empty()) return;

        const fs::path dataDir = fs::path(resolvedDataDirUtf8_);
        auto addVirtual = [&](const std::string& virtualPath, const std::string& source)
        {
            const auto p = NormalizeVirtualPath(virtualPath);
            if (IsBookTexturePath(p))
                bookTextureAssetsUtf8_.push_back(p + " [" + source + "]");
            if (IsBookFontPath(p))
                bookFontAssetsUtf8_.push_back(p + " [" + source + "]");
        };

        for (auto root : { std::string("Textures"), std::string("Fonts") })
        {
            fs::path absRoot = dataDir / root;
            if (!fs::exists(absRoot)) continue;
            for (auto it = fs::recursive_directory_iterator(absRoot); it != fs::recursive_directory_iterator(); ++it)
            {
                if (!it->is_regular_file()) continue;
                auto rel = fs::relative(it->path(), dataDir).string();
                addVirtual(rel, "loose");
            }
        }

        for (const auto& entry : fs::directory_iterator(dataDir))
        {
            if (!entry.is_regular_file()) continue;
            auto ext = ToLowerAscii(entry.path().extension().string());
            if (ext != ".bsa") continue;

            std::vector<std::string> bsaPaths;
            if (!ReadBsaPaths(entry.path(), bsaPaths)) continue;
            const auto source = std::string("bsa:") + entry.path().filename().string();
            for (const auto& p : bsaPaths) addVirtual(p, source);
        }

        auto dedupe = [](std::vector<std::string>& items)
        {
            std::sort(items.begin(), items.end());
            items.erase(std::unique(items.begin(), items.end()), items.end());
        };

        dedupe(bookFontAssetsUtf8_);
        dedupe(bookTextureAssetsUtf8_);
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

        // Heuristic normalization for IMG src paths: replace '\\' with '/' inside src="...".
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

        DiscoverBookAssets();
        if (!resolvedDataDirUtf8_.empty())
        {
            std::ostringstream oss;
            oss << "Asset scan complete. Fonts=" << bookFontAssetsUtf8_.size()
                << ", Textures=" << bookTextureAssetsUtf8_.size()
                << ", DataDir=" << resolvedDataDirUtf8_;
            AddDiag(Diagnostic::Severity::Info, 0, 0, oss.str().c_str());
        }
    }

    const std::string& BookCompiler::GetNormalizedSourceUtf8() const { return normalizedUtf8_; }
    const std::vector<Diagnostic>& BookCompiler::GetDiagnostics() const { return diags_; }

    std::string BookCompiler::ExportDescUtf8() const
    {
        return normalizedUtf8_;
    }
}
