Oblivion Book Creator (VS2022 Scaffold)
=================================

What this is
------------
A buildable VS2022 solution scaffold for an "Oblivion Book Creator" tool:

- Native core: markup normalization + hazard diagnostics (quotes/slashes/IMG width)
- Native renderer stub: returns a BGRA page buffer (placeholder rendering)
- C++/CLI bridge: exposes Engine API to .NET
- WPF app: source editor + diagnostics + preview pane

Build / Run
-----------
1) Open `OblivionBookCreator.sln` in Visual Studio 2022
2) Select `Debug | x64`
3) Set startup project: `ObBook.App`
4) Build + Run

Notes
-----
- The preview is a stub image (checkerboard) to validate plumbing.
- Next step is to implement:
  - Real markup parsing to AST
  - Deterministic layout/pagination
  - DirectWrite/Direct2D rendering of glyph runs
  - DDS loading + <IMG> placement
