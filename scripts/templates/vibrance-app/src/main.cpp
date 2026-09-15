#include <vibranceUI/engine.h>

#include <utility>

int main()
{
    UiWindowOptions options {};
    options.title = "Hello Vibrance";
    options.size = { 720, 480 };
    options.contentMargin = { 24.0f, 24.0f, 24.0f, 24.0f };
    options.transparentFramebuffer = false;
    options.showTitle = false;
    options.showWindowControls = false;

    // To add fonts and translations, follow assets/README.md. The starter
    // keeps its first build font-independent so no binary font is assumed.

    options.build = [](UiWindowContext& view) {
        view.block("#11141AFF").fill().layer(0);

        const auto card = view.block("#202938FF", 20.0f)
            .at(UiAlignment::eCenter).size(420.0f, 180.0f).layer(1).entity();

        view.block("#7C5CFCFF", 4.0f).inside(card)
            .at(UiAlignment::eCenter).size(180.0f, 8.0f).layer(2);
    };

    UiWindow window(std::move(options));
    return window.run();
}
