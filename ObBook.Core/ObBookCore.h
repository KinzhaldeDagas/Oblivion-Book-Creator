#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace obbook
{
    struct Diagnostic
    {
        enum class Severity : uint8_t { Info=0, Warning=1, Error=2 };

        Severity severity{};
        size_t   offset{};
        size_t   length{};
        std::string message;
    };

    struct ProjectSettings
    {
        // Export governance defaults
        uint32_t codepage = 1252; // Windows-1252 default (English Oblivion)
        uint32_t maxImageWidth = 490;
        bool autoNormalizeSmartQuotes = true;
        bool autoNormalizeSlashes = true;

        // Optional Oblivion installation path. Can point at either the game root or Data folder.
        std::string oblivionDirectoryUtf8;
    };

    // Minimal, stable "compiler" surface for v1.
    // Later: AST, style stack, page model, exporter variants.
    class BookCompiler
    {
    public:
        BookCompiler();

        void SetSettings(const ProjectSettings& s);
        const ProjectSettings& GetSettings() const;

        // Source is stored as UTF-8 for simplicity in native; UI can pass UTF-16 and bridge converts.
        void SetSourceUtf8(const std::string& srcUtf8);
        const std::string& GetSourceUtf8() const;

        // Returns the normalized source (auto-fixes applied) and diagnostics.
        // v1: performs basic normalization and hazard detection (quotes, slashes, IMG width).
        // Also refreshes book font/texture discovery from loose files and BSA archives.
        void Compile();

        const std::string& GetNormalizedSourceUtf8() const;
        const std::vector<Diagnostic>& GetDiagnostics() const;

        // Export string suitable to paste into DESC. For v1 this is identical to normalized source.
        // Later: enforce CP1252 mapping and produce safe DESC bytes.
        std::string ExportDescUtf8() const;

        void SetOblivionDirectoryUtf8(const std::string& pathUtf8);
        const std::string& GetResolvedDataDirectoryUtf8() const;
        const std::vector<std::string>& GetBookFontAssetsUtf8() const;
        const std::vector<std::string>& GetBookTextureAssetsUtf8() const;

    private:
        ProjectSettings settings_{};
        std::string sourceUtf8_;
        std::string normalizedUtf8_;
        std::vector<Diagnostic> diags_;

        std::string resolvedDataDirUtf8_;
        std::vector<std::string> bookFontAssetsUtf8_;
        std::vector<std::string> bookTextureAssetsUtf8_;

        void AddDiag(Diagnostic::Severity sev, size_t off, size_t len, const char* msg);
        void DiscoverBookAssets();
    };
}
