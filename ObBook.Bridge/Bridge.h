#pragma once

namespace ObBook
{
    public enum class Severity : System::Byte { Info=0, Warning=1, Error=2 };

    public ref class Diagnostic sealed
    {
    public:
        property Severity SeverityLevel;
        property System::Int32 Offset;
        property System::Int32 Length;
        property System::String^ Message;
    };

    class EngineImpl;

    public ref class Engine sealed
    {
    public:
        Engine();
        ~Engine();
        !Engine();

        void SetSourceText(System::String^ text);
        void Compile();

        property System::String^ NormalizedText { System::String^ get(); }
        property System::String^ ExportDescText { System::String^ get(); }

        System::Collections::Generic::List<Diagnostic^>^ GetDiagnostics();

        // v1 preview plumbing: returns a BitmapSource for a stub page.
        System::Windows::Media::Imaging::BitmapSource^ RenderPreviewPage(System::Int32 width, System::Int32 height, float dpi);

    private:
        EngineImpl* impl_;
    };
}
