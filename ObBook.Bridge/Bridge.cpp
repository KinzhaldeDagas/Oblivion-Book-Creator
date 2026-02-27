#include <msclr/marshal_cppstd.h>
#include <vector>
#include <string>

#include "../ObBook.Core/ObBookCore.h"
#include "../ObBook.RenderD2D/ObBookRenderD2D.h"
#include "Bridge.h"

using namespace msclr::interop;

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
    if (!obbook::RenderStubBgra(p, bgra, err))
        throw gcnew System::InvalidOperationException(marshal_as<System::String^>(err));

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
