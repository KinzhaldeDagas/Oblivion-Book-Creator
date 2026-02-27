#pragma once

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Windows::Media::Imaging;

namespace ObBook
{
    public enum class Severity : Byte { Info=0, Warning=1, Error=2 };

    public ref class Diagnostic sealed
    {
    public:
        property Severity SeverityLevel;
        property Int32 Offset;
        property Int32 Length;
        property String^ Message;
    };

    public ref class Engine sealed
    {
    public:
        Engine();

        void SetSourceText(String^ text);
        void Compile();

        property String^ NormalizedText { String^ get(); }
        property String^ ExportDescText { String^ get(); }

        List<Diagnostic^>^ GetDiagnostics();

        // v1 preview plumbing: returns a BitmapSource for a stub page.
        BitmapSource^ RenderPreviewPage(Int32 width, Int32 height, Single dpi);

    private:
        class Impl;
        Impl* impl_;
    };
}
