#include <msclr/marshal_cppstd.h>
#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstring>

#include "../ObBook.Core/ObBookCore.h"
#include "../ObBook.RenderD2D/ObBookRenderD2D.h"
#include "Bridge.h"

using namespace msclr::interop;


namespace
{
    namespace fs = std::filesystem;

    static std::string ToLowerAscii(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
        {
            if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
            return static_cast<char>(c);
        });
        return s;
    }

    static std::string NormalizeVirtualPath(std::string s)
    {
        for (auto& c : s) if (c == '\\') c = '/';
        s = ToLowerAscii(s);
        while (!s.empty() && s.front() == '/') s.erase(s.begin());
        return s;
    }

    static bool StartsWithNoCase(const std::string& s, size_t at, const char* lit)
    {
        size_t i = 0;
        for (; lit[i] != '\0'; ++i)
        {
            if (at + i >= s.size()) return false;
            char a = s[at + i];
            char b = lit[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    }

    static std::string ExtractFirstImgSrc(std::string src)
    {
        for (size_t i = 0; i + 4 < src.size(); ++i)
        {
            if (!StartsWithNoCase(src, i, "<img")) continue;
            size_t j = i;
            while (j < src.size() && src[j] != '>') ++j;
            if (j >= src.size()) return {};

            for (size_t k = i; k + 4 < j; ++k)
            {
                if (!StartsWithNoCase(src, k, "src=")) continue;
                k += 4;
                if (k >= j) return {};
                if (src[k] == '"')
                {
                    ++k;
                    size_t e = k;
                    while (e < j && src[e] != '"') ++e;
                    return src.substr(k, e - k);
                }
                size_t e = k;
                while (e < j && src[e] != ' ' && src[e] != '	') ++e;
                return src.substr(k, e - k);
            }
            return {};
        }
        return {};
    }

    static std::string ToTextureVirtualPath(const std::string& imgSrc)
    {
        auto p = NormalizeVirtualPath(imgSrc);
        if (p.rfind("textures/", 0) == 0) return p;
        if (p.rfind("book/", 0) == 0) return std::string("textures/menus/") + p;
        return std::string("textures/") + p;
    }

    struct BsaFileEntry
    {
        std::string path;
        uint32_t packedSize{};
        uint32_t offset{};
    };

    static bool ReadExact(std::ifstream& in, void* dst, size_t size)
    {
        in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(size));
        return static_cast<size_t>(in.gcount()) == size;
    }

    static bool ReadBsaEntries(const fs::path& bsaPath, std::vector<BsaFileEntry>& entries)
    {
        std::ifstream in(bsaPath, std::ios::binary);
        if (!in) return false;

        std::array<char,4> magic{};
        if (!ReadExact(in, magic.data(), magic.size()) || std::memcmp(magic.data(), "BSA\0", 4) != 0) return false;

        struct Header { uint32_t ver, dirOff, flags, folders, files, folderNamesLen, fileNamesLen, fileFlags; } h{};
        if (!ReadExact(in, &h, sizeof(h))) return false;
        if (h.ver != 103 && h.ver != 104) return false;

        struct Folder { uint64_t hash; uint32_t fileCount; uint32_t offset; };
        struct FileRec { uint64_t hash; uint32_t size; uint32_t offset; };

        in.seekg(static_cast<std::streamoff>(h.dirOff), std::ios::beg);
        std::vector<Folder> folders(h.folders);
        for (auto& f : folders) if (!ReadExact(in, &f, sizeof(f))) return false;

        std::vector<BsaFileEntry> raw;
        raw.reserve(h.files);
        for (const auto& f : folders)
        {
            uint8_t folderNameLen = 0;
            if (!ReadExact(in, &folderNameLen, sizeof(folderNameLen))) return false;

            std::string folderName;
            if (folderNameLen > 0)
            {
                folderName.resize(folderNameLen);
                if (!ReadExact(in, folderName.data(), folderName.size())) return false;
                if (!folderName.empty() && folderName.back() == '\0') folderName.pop_back();
            }
            folderName = NormalizeVirtualPath(folderName);

            for (uint32_t i = 0; i < f.fileCount; ++i)
            {
                FileRec r{};
                if (!ReadExact(in, &r, sizeof(r))) return false;
                raw.push_back({folderName, r.size, r.offset});
            }
        }

        const uint64_t namesOffset = static_cast<uint64_t>(h.dirOff) + static_cast<uint64_t>(h.folders) * sizeof(Folder)
            + static_cast<uint64_t>(h.folderNamesLen) + static_cast<uint64_t>(h.files) * sizeof(FileRec);
        in.seekg(static_cast<std::streamoff>(namesOffset), std::ios::beg);
        if (!in) return false;

        entries.clear();
        entries.reserve(raw.size());
        for (auto& r : raw)
        {
            std::string fileName;
            for (;;)
            {
                char c=0; if (!in.get(c)) return false; if (c=='\0') break; fileName.push_back(c);
            }
            auto fn = NormalizeVirtualPath(fileName);
            BsaFileEntry e{};
            e.path = r.path.empty() ? fn : (r.path + "/" + fn);
            e.packedSize = r.packedSize;
            e.offset = r.offset;
            entries.push_back(std::move(e));
        }
        return true;
    }

    static bool ReadAssetBytes(const std::string& dataDirUtf8, const std::string& virtualPath, std::vector<uint8_t>& bytes)
    {
        const fs::path dataDir(dataDirUtf8);
        auto loose = dataDir / fs::path(virtualPath);
        if (fs::exists(loose) && fs::is_regular_file(loose))
        {
            std::ifstream in(loose, std::ios::binary);
            if (!in) return false;
            in.seekg(0, std::ios::end);
            auto size = static_cast<size_t>(in.tellg());
            in.seekg(0, std::ios::beg);
            bytes.resize(size);
            in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
            return in.good() || in.eof();
        }

        for (const auto& entry : fs::directory_iterator(dataDir))
        {
            if (!entry.is_regular_file()) continue;
            if (ToLowerAscii(entry.path().extension().string()) != ".bsa") continue;

            std::vector<BsaFileEntry> entries;
            if (!ReadBsaEntries(entry.path(), entries)) continue;

            auto it = std::find_if(entries.begin(), entries.end(), [&](const BsaFileEntry& e)
            {
                return e.path == virtualPath;
            });
            if (it == entries.end()) continue;

            constexpr uint32_t kSizeMask = 0x3FFFFFFFu;
            constexpr uint32_t kCompressedBit = 0x40000000u;
            if ((it->packedSize & kCompressedBit) != 0u) return false;
            const uint32_t sz = (it->packedSize & kSizeMask);
            if (sz == 0) return false;

            std::ifstream in(entry.path(), std::ios::binary);
            if (!in) return false;
            in.seekg(static_cast<std::streamoff>(it->offset), std::ios::beg);
            if (!in) return false;
            bytes.resize(sz);
            in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(sz));
            return in.good() || in.eof();
        }
        return false;
    }

    static uint8_t ClampU8(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

    static void Decode565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        r = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
        g = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
        b = static_cast<uint8_t>((c & 31) * 255 / 31);
    }

    static bool DecodeDxt1(const uint8_t* data, size_t size, uint32_t w, uint32_t h, std::vector<uint8_t>& out)
    {
        const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        if (size < static_cast<size_t>(bw) * bh * 8) return false;
        out.assign(static_cast<size_t>(w) * h * 4, 0);
        size_t off = 0;
        for (uint32_t by=0; by<bh; ++by) for (uint32_t bx=0; bx<bw; ++bx)
        {
            uint16_t c0 = data[off] | (data[off+1]<<8);
            uint16_t c1 = data[off+2] | (data[off+3]<<8);
            uint32_t idx = data[off+4] | (data[off+5]<<8) | (data[off+6]<<16) | (data[off+7]<<24);
            off += 8;
            uint8_t r[4],g[4],b[4],a[4]{255,255,255,255};
            Decode565(c0,r[0],g[0],b[0]); Decode565(c1,r[1],g[1],b[1]);
            if (c0 > c1) {
                r[2]=(2*r[0]+r[1])/3; g[2]=(2*g[0]+g[1])/3; b[2]=(2*b[0]+b[1])/3;
                r[3]=(r[0]+2*r[1])/3; g[3]=(g[0]+2*g[1])/3; b[3]=(b[0]+2*b[1])/3;
            } else {
                r[2]=(r[0]+r[1])/2; g[2]=(g[0]+g[1])/2; b[2]=(b[0]+b[1])/2;
                r[3]=g[3]=b[3]=0; a[3]=0;
            }
            for (uint32_t py=0; py<4; ++py) for (uint32_t px=0; px<4; ++px)
            {
                uint32_t x=bx*4+px, y=by*4+py; if (x>=w||y>=h) continue;
                uint32_t ci=(idx >> (2*(py*4+px))) & 3;
                uint8_t* p=&out[(static_cast<size_t>(y)*w+x)*4];
                p[0]=b[ci]; p[1]=g[ci]; p[2]=r[ci]; p[3]=a[ci];
            }
        }
        return true;
    }

    static bool DecodeDxt5(const uint8_t* data, size_t size, uint32_t w, uint32_t h, std::vector<uint8_t>& out)
    {
        const uint32_t bw=(w+3)/4,bh=(h+3)/4;
        if (size < static_cast<size_t>(bw)*bh*16) return false;
        out.assign(static_cast<size_t>(w)*h*4,0);
        size_t off=0;
        for(uint32_t by=0;by<bh;++by) for(uint32_t bx=0;bx<bw;++bx)
        {
            uint8_t a0=data[off],a1=data[off+1];
            uint64_t abits=0; for(int i=0;i<6;++i) abits |= static_cast<uint64_t>(data[off+2+i])<<(8*i);
            uint16_t c0=data[off+8]|(data[off+9]<<8), c1=data[off+10]|(data[off+11]<<8);
            uint32_t cbits=data[off+12]|(data[off+13]<<8)|(data[off+14]<<16)|(data[off+15]<<24);
            off+=16;
            uint8_t aval[8]; aval[0]=a0; aval[1]=a1;
            if(a0>a1){ for(int i=1;i<=6;++i) aval[i+1]=static_cast<uint8_t>(((7-i)*a0+i*a1)/7); }
            else { for(int i=1;i<=4;++i) aval[i+1]=static_cast<uint8_t>(((5-i)*a0+i*a1)/5); aval[6]=0; aval[7]=255; }
            uint8_t r[4],g[4],b[4]; Decode565(c0,r[0],g[0],b[0]); Decode565(c1,r[1],g[1],b[1]);
            r[2]=(2*r[0]+r[1])/3; g[2]=(2*g[0]+g[1])/3; b[2]=(2*b[0]+b[1])/3;
            r[3]=(r[0]+2*r[1])/3; g[3]=(g[0]+2*g[1])/3; b[3]=(b[0]+2*b[1])/3;
            for(uint32_t py=0;py<4;++py) for(uint32_t px=0;px<4;++px)
            {
                uint32_t x=bx*4+px,y=by*4+py; if(x>=w||y>=h) continue;
                uint32_t ci=(cbits>>(2*(py*4+px)))&3;
                uint32_t ai=(abits>>(3*(py*4+px)))&7;
                uint8_t* p=&out[(static_cast<size_t>(y)*w+x)*4];
                p[0]=b[ci]; p[1]=g[ci]; p[2]=r[ci]; p[3]=aval[ai];
            }
        }
        return true;
    }

    static bool DecodeDdsToBgra(const std::vector<uint8_t>& dds, std::vector<uint8_t>& out, uint32_t& w, uint32_t& h)
    {
        if (dds.size() < 128 || std::memcmp(dds.data(), "DDS ", 4) != 0) return false;
        const uint8_t* hdr = dds.data() + 4;
        auto rd32 = [&](size_t o)->uint32_t { return *reinterpret_cast<const uint32_t*>(hdr + o); };
        if (rd32(0) != 124) return false;
        h = rd32(8); w = rd32(12);
        const uint32_t pfFlags = rd32(76);
        const uint32_t fourCC = rd32(80);
        const uint32_t rgbBits = rd32(84);
        const uint32_t rMask = rd32(88), gMask = rd32(92), bMask = rd32(96);
        const uint8_t* data = dds.data() + 128;
        const size_t size = dds.size() - 128;

        auto FCC=[&](char a,char b,char c,char d){ return static_cast<uint32_t>(a)| (static_cast<uint32_t>(b)<<8) | (static_cast<uint32_t>(c)<<16) | (static_cast<uint32_t>(d)<<24); };
        if ((pfFlags & 0x4u) && fourCC == FCC('D','X','T','1')) return DecodeDxt1(data,size,w,h,out);
        if ((pfFlags & 0x4u) && fourCC == FCC('D','X','T','5')) return DecodeDxt5(data,size,w,h,out);
        if ((pfFlags & 0x40u) && rgbBits == 32 && rMask == 0x00FF0000u && gMask == 0x0000FF00u && bMask == 0x000000FFu)
        {
            if (size < static_cast<size_t>(w) * h * 4) return false;
            out.assign(data, data + static_cast<size_t>(w) * h * 4);
            return true;
        }
        return false;
    }

    static void BlitBgra(std::vector<uint8_t>& dst, uint32_t dw, uint32_t dh, const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh)
    {
        if (dst.empty() || src.empty() || dw==0 || dh==0 || sw==0 || sh==0) return;
        const uint32_t maxW = dw / 2;
        const uint32_t maxH = dh / 2;
        const float scaleW = maxW / static_cast<float>(sw);
        const float scaleH = maxH / static_cast<float>(sh);
        float scale = (scaleW < scaleH) ? scaleW : scaleH;
        if (scale > 1.0f) scale = 1.0f;
        uint32_t tw = static_cast<uint32_t>(sw * scale);
        uint32_t th = static_cast<uint32_t>(sh * scale);
        if (tw == 0) tw = 1;
        if (th == 0) th = 1;
        uint32_t ox = (dw > tw) ? (dw - tw)/2 : 0;
        uint32_t oy = 120;
        if (oy + th > dh) oy = (dh > th) ? dh - th : 0;

        for (uint32_t y=0; y<th; ++y)
        {
            uint32_t sy = static_cast<uint32_t>((static_cast<uint64_t>(y) * sh) / th);
            for (uint32_t x=0; x<tw; ++x)
            {
                uint32_t sx = static_cast<uint32_t>((static_cast<uint64_t>(x) * sw) / tw);
                const uint8_t* sp = &src[(static_cast<size_t>(sy) * sw + sx) * 4];
                uint8_t* dp = &dst[(static_cast<size_t>(oy + y) * dw + (ox + x)) * 4];
                const float a = sp[3] / 255.0f;
                dp[0] = ClampU8(static_cast<int>(sp[0] * a + dp[0] * (1 - a)));
                dp[1] = ClampU8(static_cast<int>(sp[1] * a + dp[1] * (1 - a)));
                dp[2] = ClampU8(static_cast<int>(sp[2] * a + dp[2] * (1 - a)));
                dp[3] = 255;
            }
        }
    }

    static void TryOverlayFirstImg(std::vector<uint8_t>& page, uint32_t width, uint32_t height, const std::string& srcUtf8, const std::string& dataDirUtf8)
    {
        if (dataDirUtf8.empty()) return;
        const auto src = ExtractFirstImgSrc(srcUtf8);
        if (src.empty()) return;
        const auto path = ToTextureVirtualPath(src);

        std::vector<uint8_t> dds;
        if (!ReadAssetBytes(dataDirUtf8, path, dds)) return;

        std::vector<uint8_t> tex;
        uint32_t tw = 0, th = 0;
        if (!DecodeDdsToBgra(dds, tex, tw, th)) return;

        BlitBgra(page, width, height, tex, tw, th);
    }
}

class ObBook::EngineImpl
{
public:
    obbook::BookCompiler compiler{};
};

ObBook::Engine::Engine()
    : impl_(new EngineImpl())
{
}

ObBook::Engine::~Engine()
{
    this->!Engine();
}

ObBook::Engine::!Engine()
{
    delete impl_;
    impl_ = nullptr;
}

void ObBook::Engine::SetSourceText(System::String^ text)
{
    if (!text) text = "";
    std::string utf8 = marshal_as<std::string>(text);
    impl_->compiler.SetSourceUtf8(utf8);
}

void ObBook::Engine::SetOblivionDirectory(System::String^ path)
{
    if (!path) path = "";
    impl_->compiler.SetOblivionDirectoryUtf8(marshal_as<std::string>(path));
}

void ObBook::Engine::Compile()
{
    impl_->compiler.Compile();
}

System::String^ ObBook::Engine::NormalizedText::get()
{
    return marshal_as<System::String^>(impl_->compiler.GetNormalizedSourceUtf8());
}

System::String^ ObBook::Engine::ExportDescText::get()
{
    return marshal_as<System::String^>(impl_->compiler.ExportDescUtf8());
}

System::String^ ObBook::Engine::ResolvedDataDirectory::get()
{
    return marshal_as<System::String^>(impl_->compiler.GetResolvedDataDirectoryUtf8());
}

System::Collections::Generic::List<ObBook::Diagnostic^>^ ObBook::Engine::GetDiagnostics()
{
    auto list = gcnew System::Collections::Generic::List<Diagnostic^>();
    const auto& diags = impl_->compiler.GetDiagnostics();
    for (const auto& d : diags)
    {
        auto m = gcnew Diagnostic();
        m->SeverityLevel = static_cast<Severity>(static_cast<System::Byte>(d.severity));
        m->Offset = static_cast<System::Int32>(d.offset);
        m->Length = static_cast<System::Int32>(d.length);
        m->Message = marshal_as<System::String^>(d.message);
        list->Add(m);
    }
    return list;
}

System::Collections::Generic::List<System::String^>^ ObBook::Engine::GetBookFontAssets()
{
    auto list = gcnew System::Collections::Generic::List<System::String^>();
    const auto& assets = impl_->compiler.GetBookFontAssetsUtf8();
    for (const auto& a : assets)
        list->Add(marshal_as<System::String^>(a));
    return list;
}

System::Collections::Generic::List<System::String^>^ ObBook::Engine::GetBookTextureAssets()
{
    auto list = gcnew System::Collections::Generic::List<System::String^>();
    const auto& assets = impl_->compiler.GetBookTextureAssetsUtf8();
    for (const auto& a : assets)
        list->Add(marshal_as<System::String^>(a));
    return list;
}

System::Windows::Media::Imaging::BitmapSource^ ObBook::Engine::RenderPreviewPage(System::Int32 width, System::Int32 height, float dpi)
{
    if (width <= 0) width = 1024;
    if (height <= 0) height = 768;
    if (dpi <= 0.0f) dpi = 96.0f;

    obbook::RenderParams p{};
    p.width = static_cast<uint32_t>(width);
    p.height = static_cast<uint32_t>(height);
    p.dpi = dpi;

    std::vector<uint8_t> bgra;
    std::string err;
    const auto source = impl_->compiler.GetNormalizedSourceUtf8().empty()
        ? impl_->compiler.GetSourceUtf8()
        : impl_->compiler.GetNormalizedSourceUtf8();

    if (!obbook::RenderPreviewBgra(p, source, bgra, err))
        throw gcnew System::InvalidOperationException(marshal_as<System::String^>(err));

    TryOverlayFirstImg(bgra, p.width, p.height, source, impl_->compiler.GetResolvedDataDirectoryUtf8());

    const int stride = width * 4;
    auto pixels = gcnew array<System::Byte>(static_cast<int>(bgra.size()));
    System::Runtime::InteropServices::Marshal::Copy(
        static_cast<System::IntPtr>(static_cast<void*>(bgra.data())),
        pixels,
        0,
        pixels->Length);

    return System::Windows::Media::Imaging::BitmapSource::Create(
        width, height, dpi, dpi,
        System::Windows::Media::PixelFormats::Bgra32,
        nullptr,
        pixels,
        stride);
}
