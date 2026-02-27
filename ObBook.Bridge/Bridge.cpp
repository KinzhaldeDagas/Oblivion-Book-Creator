#include "Bridge.h"

#include <msclr/marshal_cppstd.h>
#include <vector>
#include <string>

#include "../ObBook.Core/ObBookCore.h"
#include "../ObBook.RenderD2D/ObBookRenderD2D.h"

using namespace msclr::interop;

class ObBook::Engine::Impl
{
public:
    obbook::BookCompiler compiler{};
};

ObBook::Engine::Engine()
{
    impl_ = new Impl();
}

void ObBook::Engine::SetSourceText(String^ text)
{
    if (!text) text = "";
    std::string utf8 = marshal_as<std::string>(text);
    impl_->compiler.SetSourceUtf8(utf8);
}

void ObBook::Engine::Compile()
{
    impl_->compiler.Compile();
}

String^ ObBook::Engine::NormalizedText::get()
{
    return marshal_as<String^>(impl_->compiler.GetNormalizedSourceUtf8());
}

String^ ObBook::Engine::ExportDescText::get()
{
    return marshal_as<String^>(impl_->compiler.ExportDescUtf8());
}

System::Collections::Generic::List<ObBook::Diagnostic^>^ ObBook::Engine::GetDiagnostics()
{
    auto list = gcnew List<Diagnostic^>();
    const auto& diags = impl_->compiler.GetDiagnostics();
    for (const auto& d : diags)
    {
        auto m = gcnew Diagnostic();
        m->SeverityLevel = static_cast<Severity>(static_cast<Byte>(d.severity));
        m->Offset = static_cast<Int32>(d.offset);
        m->Length = static_cast<Int32>(d.length);
        m->Message = marshal_as<String^>(d.message);
        list->Add(m);
    }
    return list;
}

BitmapSource^ ObBook::Engine::RenderPreviewPage(Int32 width, Int32 height, Single dpi)
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
    if (!obbook::RenderStubBgra(p, bgra, err))
        throw gcnew InvalidOperationException(marshal_as<String^>(err));

    const int stride = width * 4;
    return BitmapSource::Create(
        width, height, dpi, dpi,
        System::Windows::Media::PixelFormats::Bgra32,
        nullptr,
        ([&](){ auto a = gcnew array<Byte>(static_cast<int>(bgra.size())); System::Runtime::InteropServices::Marshal::Copy((IntPtr)(void*)bgra.data(), a, 0, a->Length); return a; })(),
        stride
    );
}
